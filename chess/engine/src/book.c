#include "book.h"

#ifndef NO_BOOK

#include "movegen.h"
#include "chdata.h"
#include <fileioc.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ========== Multi-AppVar Book Layout ==========
 *
 * TI-OS limits AppVars to ~65KB, so books are split across files:
 *
 * CHDATA (shared data, see chdata.h):
 *   Polyglot random uint64s at CHDATA_RND_OFFSET (6,248 bytes, LE)
 *
 * Book data AppVars (tier-specific, CHxBnn where x=tier, nn=01..99):
 *   [4 bytes]      uint32_t entry_count (little-endian)
 *   [N * 16 bytes] Polyglot book entries, sorted by key (big-endian)
 *
 * Polyglot entry format (16 bytes, big-endian):
 *   uint64 key     (Zobrist hash)
 *   uint16 move    (encoded move)
 *   uint16 weight  (move quality)
 *   uint32 learn   (unused)
 */

/* Tier IDs for detected_tier */
#define TIER_XXL 0
#define TIER_XL  1
#define TIER_L   2
#define TIER_M   3
#define TIER_S   4
#define TIER_NONE 5

/* Randoms now loaded from CHDATA appvar at CHDATA_RND_OFFSET */
#define POLY_RANDOM_COUNT   781
#define POLY_RANDOM_BYTES   (POLY_RANDOM_COUNT * 8)  /* 6,248 */
#define POLY_ENTRY_SIZE     16
#define MAX_BOOK_SEGMENTS   40

/* ========== Internal State ========== */

typedef struct {
    const uint8_t *data;     /* flash pointer to entries */
    uint32_t       count;    /* number of entries in this segment */
} book_segment_t;

static const uint64_t *poly_randoms;        /* 781 random values in flash */
static book_segment_t  segments[MAX_BOOK_SEGMENTS];
static uint8_t         num_segments;
static uint32_t        total_entries;        /* sum of all segment counts */
static uint8_t         book_ready;
static uint8_t         detected_tier;        /* TIER_XXL..TIER_S, or TIER_NONE */
uint32_t               book_random_seed;    /* set externally for variety */

/* ========== Polyglot Random Number Indices ==========
 *
 * The 781 random values are laid out as:
 *   [0..767]   piece-square: 12 pieces * 64 squares
 *              Piece order: BlackPawn=0, WhitePawn=1, BlackKnight=2,
 *                           WhiteKnight=3, ... BlackKing=10, WhiteKing=11
 *              Index = piece_polyglot * 64 + square_polyglot
 *   [768]      castling: white kingside
 *   [769]      castling: white queenside
 *   [770]      castling: black kingside
 *   [771]      castling: black queenside
 *   [772..779] en passant file a..h
 *   [780]      turn (XOR when white to move)
 */

#define POLY_CASTLE_BASE  768
#define POLY_EP_BASE      772
#define POLY_TURN_KEY     780

/* ========== Big-Endian Read Helpers ========== */

static uint64_t read_be64(const uint8_t *p)
{
    return ((uint64_t)p[0] << 56) | ((uint64_t)p[1] << 48) |
           ((uint64_t)p[2] << 40) | ((uint64_t)p[3] << 32) |
           ((uint64_t)p[4] << 24) | ((uint64_t)p[5] << 16) |
           ((uint64_t)p[6] << 8)  | (uint64_t)p[7];
}

static uint16_t read_be16(const uint8_t *p)
{
    return ((uint16_t)p[0] << 8) | p[1];
}

static uint32_t read_le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* ========== Polyglot Hash Computation ========== */

static uint8_t engine_piece_to_poly(uint8_t piece)
{
    uint8_t type = PIECE_TYPE(piece);
    uint8_t is_white = IS_WHITE(piece);
    return (type - 1) * 2 + (is_white ? 1 : 0);
}

static uint64_t compute_polyglot_hash(const board_t *b)
{
    uint64_t hash = 0;
    uint8_t r, c, piece, poly_piece, poly_row, poly_sq;

    /* Pieces */
    for (r = 0; r < 8; r++) {
        for (c = 0; c < 8; c++) {
            piece = b->squares[RC_TO_SQ(r, c)];
            if (piece == PIECE_NONE) continue;
            poly_piece = engine_piece_to_poly(piece);
            poly_row = 7 - r;  /* flip row: engine row 0 = rank 8 */
            poly_sq = poly_row * 8 + c;
            hash ^= poly_randoms[poly_piece * 64 + poly_sq];
        }
    }

    /* Castling */
    if (b->castling & CASTLE_WK) hash ^= poly_randoms[POLY_CASTLE_BASE + 0];
    if (b->castling & CASTLE_WQ) hash ^= poly_randoms[POLY_CASTLE_BASE + 1];
    if (b->castling & CASTLE_BK) hash ^= poly_randoms[POLY_CASTLE_BASE + 2];
    if (b->castling & CASTLE_BQ) hash ^= poly_randoms[POLY_CASTLE_BASE + 3];

    /* En passant — only if an enemy pawn can actually capture */
    if (b->ep_square != SQ_NONE) {
        uint8_t ep_col = SQ_TO_COL(b->ep_square);
        uint8_t ep_row = SQ_TO_ROW(b->ep_square);
        uint8_t attacker_row, enemy_pawn;
        uint8_t can_capture = 0;

        if (b->side == WHITE) {
            attacker_row = ep_row + 1;
            enemy_pawn = MAKE_PIECE(COLOR_WHITE, PIECE_PAWN);
        } else {
            attacker_row = ep_row - 1;
            enemy_pawn = MAKE_PIECE(COLOR_BLACK, PIECE_PAWN);
        }

        if (ep_col > 0 && b->squares[RC_TO_SQ(attacker_row, ep_col - 1)] == enemy_pawn)
            can_capture = 1;
        if (ep_col < 7 && b->squares[RC_TO_SQ(attacker_row, ep_col + 1)] == enemy_pawn)
            can_capture = 1;

        if (can_capture)
            hash ^= poly_randoms[POLY_EP_BASE + ep_col];
    }

    /* Turn — XOR when white to move */
    if (b->side == WHITE)
        hash ^= poly_randoms[POLY_TURN_KEY];

    return hash;
}

/* ========== Segmented Binary Search ========== */

/* Binary search within a single segment for the first entry matching key.
 * Returns the local index, or seg->count if not found. */
static uint32_t segment_find_first(const book_segment_t *seg, uint64_t key)
{
    uint32_t lo = 0, hi = seg->count, mid;
    uint64_t mid_key;

    while (lo < hi) {
        mid = lo + (hi - lo) / 2;
        mid_key = read_be64(seg->data + mid * POLY_ENTRY_SIZE);
        if (mid_key < key)
            lo = mid + 1;
        else
            hi = mid;
    }

    if (lo < seg->count &&
        read_be64(seg->data + lo * POLY_ENTRY_SIZE) == key)
        return lo;

    return seg->count;  /* not found */
}

/* Find all entries matching key across segments.
 * The book is globally sorted, so matching entries can only be in
 * one or two adjacent segments (if the key falls on a boundary).
 * Returns the segment index and local offset, or -1 if not found. */
static uint8_t find_key_segment(uint64_t key, uint8_t *out_seg, uint32_t *out_idx)
{
    uint8_t s;

    for (s = 0; s < num_segments; s++) {
        uint32_t idx = segment_find_first(&segments[s], key);
        if (idx < segments[s].count) {
            *out_seg = s;
            *out_idx = idx;
            return 1;
        }
    }

    return 0;
}

/* ========== Move Conversion ========== */

static move_t poly_move_to_engine(const board_t *b, uint16_t poly_move)
{
    uint8_t to_file   = (poly_move >> 0) & 0x7;
    uint8_t to_row_p  = (poly_move >> 3) & 0x7;
    uint8_t from_file = (poly_move >> 6) & 0x7;
    uint8_t from_row_p = (poly_move >> 9) & 0x7;
    uint8_t promo     = (poly_move >> 12) & 0x7;

    uint8_t from_row = 7 - from_row_p;
    uint8_t to_row = 7 - to_row_p;
    uint8_t from_sq = RC_TO_SQ(from_row, from_file);
    uint8_t to_sq;

    /* Handle castling: Polyglot encodes king-to-rook */
    if (PIECE_TYPE(b->squares[from_sq]) == PIECE_KING) {
        if (from_file == 4 && to_file == 7)
            to_sq = RC_TO_SQ(to_row, 6);   /* Kingside */
        else if (from_file == 4 && to_file == 0)
            to_sq = RC_TO_SQ(to_row, 2);   /* Queenside */
        else
            to_sq = RC_TO_SQ(to_row, to_file);
    } else {
        to_sq = RC_TO_SQ(to_row, to_file);
    }

    /* Find matching generated move to get correct flags */
    {
        move_t moves[MAX_MOVES];
        uint8_t count, i;
        undo_t undo;

        count = generate_moves_from(b, from_sq, moves);

        for (i = 0; i < count; i++) {
            if (moves[i].to != to_sq) continue;

            if (promo) {
                static const uint8_t promo_flags[] = {
                    0, FLAG_PROMO_N, FLAG_PROMO_B, FLAG_PROMO_R, FLAG_PROMO_Q
                };
                if (!(moves[i].flags & FLAG_PROMOTION)) continue;
                if ((moves[i].flags & FLAG_PROMO_MASK) != promo_flags[promo])
                    continue;
            } else if (moves[i].flags & FLAG_PROMOTION) {
                continue;
            }

            board_make((board_t *)b, moves[i], &undo);
            if (board_is_legal(b)) {
                board_unmake((board_t *)b, moves[i], &undo);
                return moves[i];
            }
            board_unmake((board_t *)b, moves[i], &undo);
        }
    }

    return MOVE_NONE;
}

/* ========== Iterator: walk entries matching a key across segments ========== */

/* Iterate entries with the given key starting at segment seg_idx, local
 * offset local_idx. Calls the callback for each entry's (move, weight).
 * This handles the case where matching entries span a segment boundary. */
typedef struct {
    uint16_t move;
    uint16_t weight;
} book_entry_t;

static uint8_t iterate_key_entries(uint64_t key, uint8_t seg_idx,
                                   uint32_t local_idx,
                                   book_entry_t *entries, uint8_t max_entries)
{
    uint8_t count = 0;
    uint8_t s = seg_idx;
    uint32_t i = local_idx;

    while (s < num_segments && count < max_entries) {
        const uint8_t *base = segments[s].data;
        uint32_t seg_count = segments[s].count;

        while (i < seg_count && count < max_entries) {
            const uint8_t *entry = base + i * POLY_ENTRY_SIZE;
            if (read_be64(entry) != key)
                return count;
            entries[count].move = read_be16(entry + 8);
            entries[count].weight = read_be16(entry + 10);
            count++;
            i++;
        }

        /* Continue to next segment if entries might span boundary */
        s++;
        i = 0;
    }

    return count;
}

/* ========== Public API ========== */

/* Try to load all segments for a given 4-char prefix (e.g. "CHBX").
   Returns 1 if at least one segment was loaded, 0 otherwise. */
/* Load all segments matching a given prefix string (e.g. "CHBX").
   prefix must be exactly 4 chars. Appends to segments[]. */
static void load_segments(const char *prefix)
{
    uint8_t handle;
    uint8_t *data_ptr;
    char name[9];
    uint8_t seg;

    name[0] = prefix[0];
    name[1] = prefix[1];
    name[2] = prefix[2];
    name[3] = prefix[3];

    for (seg = 1; seg <= 99 && num_segments < MAX_BOOK_SEGMENTS; seg++) {
        uint32_t count;

        name[4] = '0' + (seg / 10);
        name[5] = '0' + (seg % 10);
        name[6] = '\0';
        handle = ti_Open(name, "r");
        if (!handle)
            break;

        data_ptr = (uint8_t *)ti_GetDataPtr(handle);
        count = read_le32(data_ptr);
        ti_Close(handle);

        if (count == 0)
            continue;

        segments[num_segments].data = data_ptr + 4;
        segments[num_segments].count = count;
        total_entries += count;
        num_segments++;
    }
}

/* Check if a specific AppVar exists */
static uint8_t appvar_exists(const char *name)
{
    uint8_t handle = ti_Open(name, "r");
    if (handle) {
        ti_Close(handle);
        return 1;
    }
    return 0;
}

uint8_t book_init(void)
{
    uint8_t handle;
    uint8_t *data_ptr;

    num_segments = 0;
    total_entries = 0;
    book_ready = 0;
    detected_tier = TIER_NONE;

    /* Load randoms from CHDATA appvar */
    handle = ti_Open(CHDATA_APPVAR, "r");
    if (!handle)
        return 0;
    data_ptr = (uint8_t *)ti_GetDataPtr(handle);
    poly_randoms = (const uint64_t *)(data_ptr + CHDATA_RND_OFFSET);
    ti_Close(handle);

    /* Try each tier from largest to smallest.
       Use literal AppVar names to avoid any pointer/array issues on eZ80. */
    if (appvar_exists("CHBY01")) {
        load_segments("CHBY");
        detected_tier = TIER_XXL;
    } else if (appvar_exists("CHBX01")) {
        load_segments("CHBX");
        detected_tier = TIER_XL;
    } else if (appvar_exists("CHBL01")) {
        load_segments("CHBL");
        detected_tier = TIER_L;
    } else if (appvar_exists("CHBM01")) {
        load_segments("CHBM");
        detected_tier = TIER_M;
    } else if (appvar_exists("CHBS01")) {
        load_segments("CHBS");
        detected_tier = TIER_S;
    }

    if (num_segments == 0)
        return 0;

    book_ready = 1;
    return 1;
}

uint8_t book_probe(board_t *b, move_t *out)
{
    uint64_t key;
    uint8_t seg_idx;
    uint32_t local_idx;
    book_entry_t entries[32];  /* max alternatives per position */
    uint8_t n_entries, i;
    uint32_t total_weight, pick, cumulative;
    move_t candidate;

    if (!book_ready)
        return 0;

    key = compute_polyglot_hash(b);

    if (!find_key_segment(key, &seg_idx, &local_idx))
        return 0;

    /* Collect all entries for this key */
    n_entries = iterate_key_entries(key, seg_idx, local_idx, entries, 32);
    if (n_entries == 0)
        return 0;

    /* Sum weights */
    total_weight = 0;
    for (i = 0; i < n_entries; i++)
        total_weight += entries[i].weight;

    if (total_weight == 0)
        return 0;

    /* Weighted random selection using seed mixed with position hash */
    {
        uint32_t h = book_random_seed ^ (uint32_t)key ^ (uint32_t)(key >> 32);
        h ^= h >> 16;
        h *= 0x45d9f3bUL;
        h ^= h >> 16;
        pick = h % total_weight;
    }
    cumulative = 0;

    for (i = 0; i < n_entries; i++) {
        cumulative += entries[i].weight;
        if (cumulative > pick) {
            candidate = poly_move_to_engine(b, entries[i].move);
            if (candidate.from != SQ_NONE) {
                *out = candidate;
                return 1;
            }
        }
    }

    /* Fallback: try all entries if weighted pick failed validation */
    for (i = 0; i < n_entries; i++) {
        candidate = poly_move_to_engine(b, entries[i].move);
        if (candidate.from != SQ_NONE) {
            *out = candidate;
            return 1;
        }
    }

    return 0;
}

void book_get_info(uint8_t *ready, uint8_t *n_seg, uint32_t *n_entries)
{
    *ready = book_ready;
    *n_seg = num_segments;
    *n_entries = total_entries;
}

void book_close(void)
{
    /* Flash pointers don't need cleanup; file handles already closed */
    book_ready = 0;
    num_segments = 0;
    total_entries = 0;
}

const char *book_get_tier_name(void)
{
    switch (detected_tier) {
    case TIER_XXL: return "XXL";
    case TIER_XL:  return "XL";
    case TIER_L:   return "L";
    case TIER_M:   return "M";
    case TIER_S:   return "S";
    default:       return "";
    }
}

#endif /* NO_BOOK */
