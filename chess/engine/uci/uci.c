#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "../src/engine.h"

/* ========== Polyglot Opening Book ========== */

#include "polyglot_randoms.h"

#define POLY_CASTLE_BASE 768
#define POLY_EP_BASE     772
#define POLY_TURN_KEY    780
#define POLY_ENTRY_SIZE  16

static uint8_t *book_data;
static uint32_t book_entries;

static int book_load(const char *path)
{
    FILE *f = fopen(path, "rb");
    long size;
    if (!f) return 0;
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size < POLY_ENTRY_SIZE || size % POLY_ENTRY_SIZE != 0) {
        fclose(f);
        return 0;
    }
    book_data = malloc((size_t)size);
    if (!book_data) { fclose(f); return 0; }
    if (fread(book_data, 1, (size_t)size, f) != (size_t)size) {
        free(book_data); book_data = NULL; fclose(f); return 0;
    }
    fclose(f);
    book_entries = (uint32_t)(size / POLY_ENTRY_SIZE);
    return 1;
}

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

/* Map engine piece (1..6 white, -1..-6 black) to Polyglot piece index.
   Polyglot: BlackPawn=0, WhitePawn=1, BlackKnight=2, WhiteKnight=3, ... */
static uint8_t piece_to_poly(int8_t piece)
{
    uint8_t type = (uint8_t)(piece > 0 ? piece : -piece); /* 1=P..6=K */
    uint8_t is_white = piece > 0;
    return (type - 1) * 2 + (is_white ? 1 : 0);
}

static uint64_t compute_poly_hash(const engine_position_t *pos)
{
    uint64_t hash = 0;
    int r, c;

    /* Pieces */
    for (r = 0; r < 8; r++) {
        for (c = 0; c < 8; c++) {
            int8_t piece = pos->board[r][c];
            if (piece == 0) continue;
            uint8_t poly_piece = piece_to_poly(piece);
            uint8_t poly_row = 7 - r; /* engine row 0 = rank 8 */
            uint8_t poly_sq = poly_row * 8 + c;
            hash ^= poly_randoms[poly_piece * 64 + poly_sq];
        }
    }

    /* Castling */
    if (pos->castling & ENGINE_CASTLE_WK) hash ^= poly_randoms[POLY_CASTLE_BASE + 0];
    if (pos->castling & ENGINE_CASTLE_WQ) hash ^= poly_randoms[POLY_CASTLE_BASE + 1];
    if (pos->castling & ENGINE_CASTLE_BK) hash ^= poly_randoms[POLY_CASTLE_BASE + 2];
    if (pos->castling & ENGINE_CASTLE_BQ) hash ^= poly_randoms[POLY_CASTLE_BASE + 3];

    /* En passant — only if an enemy pawn can actually capture */
    if (pos->ep_row != ENGINE_EP_NONE && pos->ep_col != ENGINE_EP_NONE) {
        uint8_t ep_col = pos->ep_col;
        uint8_t ep_row = pos->ep_row;
        int8_t enemy_pawn = (pos->turn == 1) ? 1 : -1; /* side to move's pawn */
        uint8_t attacker_row = (pos->turn == 1) ? ep_row + 1 : ep_row - 1;
        uint8_t can_capture = 0;

        if (ep_col > 0 && pos->board[attacker_row][ep_col - 1] == enemy_pawn)
            can_capture = 1;
        if (ep_col < 7 && pos->board[attacker_row][ep_col + 1] == enemy_pawn)
            can_capture = 1;

        if (can_capture)
            hash ^= poly_randoms[POLY_EP_BASE + ep_col];
    }

    /* Turn — XOR when white to move */
    if (pos->turn == 1)
        hash ^= poly_randoms[POLY_TURN_KEY];

    return hash;
}

/* Binary search for first entry matching key */
static uint32_t book_find_first(uint64_t key)
{
    uint32_t lo = 0, hi = book_entries, mid;
    while (lo < hi) {
        mid = lo + (hi - lo) / 2;
        uint64_t mid_key = read_be64(book_data + mid * POLY_ENTRY_SIZE);
        if (mid_key < key)
            lo = mid + 1;
        else
            hi = mid;
    }
    return lo;
}

/* Probe book: returns 1 if a move was found, 0 otherwise.
   Writes the move as a UCI string (e.g. "e2e4") to out. */
static int book_probe(const engine_position_t *pos, char *out)
{
    uint64_t key;
    uint32_t idx;
    uint16_t moves[32];
    uint16_t weights[32];
    uint8_t count = 0;
    uint32_t total_weight, pick, cumulative;
    uint16_t poly_move;
    uint8_t from_file, from_row_p, to_file, to_row_p, promo;

    if (!book_data || book_entries == 0)
        return 0;

    key = compute_poly_hash(pos);
    idx = book_find_first(key);

    /* Collect all entries for this key */
    while (idx < book_entries && count < 32) {
        const uint8_t *entry = book_data + idx * POLY_ENTRY_SIZE;
        if (read_be64(entry) != key) break;
        moves[count] = read_be16(entry + 8);
        weights[count] = read_be16(entry + 10);
        count++;
        idx++;
    }

    if (count == 0) return 0;

    /* Sum weights */
    total_weight = 0;
    for (uint8_t i = 0; i < count; i++)
        total_weight += weights[i];
    if (total_weight == 0) return 0;

    /* Weighted random selection */
    pick = (uint32_t)rand() % total_weight;
    cumulative = 0;
    poly_move = moves[0];
    for (uint8_t i = 0; i < count; i++) {
        cumulative += weights[i];
        if (cumulative > pick) {
            poly_move = moves[i];
            break;
        }
    }

    /* Decode Polyglot move */
    to_file    = (poly_move >> 0) & 0x7;
    to_row_p   = (poly_move >> 3) & 0x7;
    from_file  = (poly_move >> 6) & 0x7;
    from_row_p = (poly_move >> 9) & 0x7;
    promo      = (poly_move >> 12) & 0x7;

    /* Polyglot castling: king moves to rook square; convert to standard */
    int8_t piece_at_from = pos->board[7 - from_row_p][from_file];
    if ((piece_at_from == 6 || piece_at_from == -6) && from_file == 4) {
        if (to_file == 7) to_file = 6;       /* kingside */
        else if (to_file == 0) to_file = 2;  /* queenside */
    }

    out[0] = (char)('a' + from_file);
    out[1] = (char)('1' + from_row_p);
    out[2] = (char)('a' + to_file);
    out[3] = (char)('1' + to_row_p);
    out[4] = '\0';

    if (promo) {
        const char promo_chars[] = " nbrq";
        out[4] = promo_chars[promo];
        out[5] = '\0';
    }

    return 1;
}

/* ========== Time Function ========== */

static uint32_t uci_time_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

/* Track side to move for wtime/btime allocation */
static int8_t current_side = 1; /* 1=white, -1=black */

/* ========== FEN Parsing ========== */

static void parse_fen(const char *fen)
{
    engine_position_t pos;
    int r = 0, c = 0;
    const char *p = fen;

    memset(&pos, 0, sizeof(pos));
    pos.ep_row = ENGINE_EP_NONE;
    pos.ep_col = ENGINE_EP_NONE;

    /* Piece placement */
    while (*p && *p != ' ') {
        if (*p == '/') {
            r++;
            c = 0;
        } else if (*p >= '1' && *p <= '8') {
            c += *p - '0';
        } else {
            int8_t piece = 0;
            switch (*p) {
                case 'P': piece =  1; break;
                case 'N': piece =  2; break;
                case 'B': piece =  3; break;
                case 'R': piece =  4; break;
                case 'Q': piece =  5; break;
                case 'K': piece =  6; break;
                case 'p': piece = -1; break;
                case 'n': piece = -2; break;
                case 'b': piece = -3; break;
                case 'r': piece = -4; break;
                case 'q': piece = -5; break;
                case 'k': piece = -6; break;
            }
            if (r < 8 && c < 8)
                pos.board[r][c] = piece;
            c++;
        }
        p++;
    }

    /* Side to move */
    if (*p) p++; /* skip space */
    pos.turn = (*p == 'b') ? -1 : 1;
    current_side = pos.turn;
    if (*p) p++;

    /* Castling */
    if (*p) p++; /* skip space */
    pos.castling = 0;
    while (*p && *p != ' ') {
        switch (*p) {
            case 'K': pos.castling |= ENGINE_CASTLE_WK; break;
            case 'Q': pos.castling |= ENGINE_CASTLE_WQ; break;
            case 'k': pos.castling |= ENGINE_CASTLE_BK; break;
            case 'q': pos.castling |= ENGINE_CASTLE_BQ; break;
        }
        p++;
    }

    /* En passant */
    if (*p) p++; /* skip space */
    if (*p && *p != '-') {
        pos.ep_col = (uint8_t)(*p - 'a');
        p++;
        if (*p) {
            pos.ep_row = (uint8_t)(8 - (*p - '0'));
            p++;
        }
    } else if (*p) {
        p++; /* skip '-' */
    }

    /* Halfmove clock */
    if (*p) p++; /* skip space */
    pos.halfmove_clock = 0;
    while (*p && *p != ' ') {
        pos.halfmove_clock = pos.halfmove_clock * 10 + (uint8_t)(*p - '0');
        p++;
    }

    /* Fullmove number */
    if (*p) p++; /* skip space */
    pos.fullmove_number = 0;
    while (*p && *p >= '0' && *p <= '9') {
        pos.fullmove_number = pos.fullmove_number * 10 + (uint16_t)(*p - '0');
        p++;
    }
    if (pos.fullmove_number == 0) pos.fullmove_number = 1;

    engine_set_position(&pos);
}

/* ========== Move Parsing (UCI algebraic) ========== */

static engine_move_t parse_uci_move(const char *s)
{
    engine_move_t em;
    em.from_col = (uint8_t)(s[0] - 'a');
    em.from_row = (uint8_t)(8 - (s[1] - '0'));
    em.to_col = (uint8_t)(s[2] - 'a');
    em.to_row = (uint8_t)(8 - (s[3] - '0'));
    em.flags = 0;

    if (s[4] && s[4] != ' ' && s[4] != '\n') {
        em.flags |= ENGINE_FLAG_PROMOTION;
        switch (s[4]) {
            case 'r': em.flags |= ENGINE_FLAG_PROMO_R; break;
            case 'b': em.flags |= ENGINE_FLAG_PROMO_B; break;
            case 'n': em.flags |= ENGINE_FLAG_PROMO_N; break;
            default:  em.flags |= ENGINE_FLAG_PROMO_Q; break;
        }
    }

    return em;
}

static void move_to_uci(engine_move_t em, char *out)
{
    out[0] = (char)('a' + em.from_col);
    out[1] = (char)('0' + (8 - em.from_row));
    out[2] = (char)('a' + em.to_col);
    out[3] = (char)('0' + (8 - em.to_row));
    out[4] = '\0';

    if (em.flags & ENGINE_FLAG_PROMOTION) {
        switch (em.flags & ENGINE_FLAG_PROMO_MASK) {
            case ENGINE_FLAG_PROMO_R: out[4] = 'r'; break;
            case ENGINE_FLAG_PROMO_B: out[4] = 'b'; break;
            case ENGINE_FLAG_PROMO_N: out[4] = 'n'; break;
            default:                  out[4] = 'q'; break;
        }
        out[5] = '\0';
    }
}

/* ========== UCI Protocol ========== */

static int move_count = 0;

static void handle_position(char *line)
{
    char *p = line;
    char *moves;

    if (strncmp(p, "startpos", 8) == 0) {
        engine_new_game();
        current_side = 1; /* white */
        move_count = 0;
        p += 8;
    } else if (strncmp(p, "fen ", 4) == 0) {
        p += 4;
        /* Find where "moves" starts (if present) */
        moves = strstr(p, " moves ");
        if (moves) {
            *moves = '\0'; /* terminate FEN string */
            parse_fen(p);
            p = moves + 1;
            *moves = ' ';
            move_count = 0;
        } else {
            parse_fen(p);
            return;
        }
    }

    /* Apply moves if present */
    moves = strstr(p, "moves ");
    if (moves) {
        char *tok;
        moves += 6;
        tok = strtok(moves, " \n");
        while (tok) {
            engine_move_t em = parse_uci_move(tok);
            engine_make_move(em);
            current_side = -current_side;
            move_count++;
            tok = strtok(NULL, " \n");
        }
    }
}

static void handle_go(char *line)
{
    uint8_t depth = 0;
    uint32_t movetime = 0;
    char *p;
    char move_str[6];
    engine_move_t em;

    /* Try opening book first */
    {
        engine_position_t pos;
        char book_move_str[6];
        engine_get_position(&pos);
        if (book_probe(&pos, book_move_str)) {
            printf("bestmove %s\n", book_move_str);
            fflush(stdout);
            return;
        }
    }

    p = strstr(line, "depth ");
    if (p) depth = (uint8_t)atoi(p + 6);

    p = strstr(line, "movetime ");
    if (p) movetime = (uint32_t)atoi(p + 9);

    /* If wtime/btime are specified, use simple time allocation */
    if (!movetime && !depth) {
        p = strstr(line, current_side == 1 ? "wtime " : "btime ");
        if (p) {
            uint32_t time_left = (uint32_t)atoi(p + 6);
            movetime = time_left / 30;
            if (movetime < 100) movetime = 100;
        }
    }

    if (depth == 0 && movetime == 0)
        depth = 6;

    em = engine_think(depth, movetime);

    if (em.from_row != ENGINE_SQ_NONE) {
        move_to_uci(em, move_str);
        printf("bestmove %s\n", move_str);
    } else {
        printf("bestmove 0000\n");
    }
    fflush(stdout);
}

/* ========== Main Loop ========== */

int main(int argc, char *argv[])
{
    char line[4096];
    engine_hooks_t hooks;

    setbuf(stdin, NULL);
    setbuf(stdout, NULL);

    srand((unsigned)time(NULL));

    hooks.time_ms = uci_time_ms;
    engine_init(&hooks);

    /* Node limit: 0 = unlimited (time-based), or set via -DNODE_LIMIT=N */
#ifdef NODE_LIMIT
    engine_set_max_nodes(NODE_LIMIT);
#endif

    /* Parse command-line flags */
    {
        const char *book_path = NULL;
        int i;
        for (i = 1; i < argc - 1; i++) {
            if (strcmp(argv[i], "-book") == 0) {
                book_path = argv[i + 1];
                i++;
            } else if (strcmp(argv[i], "-variance") == 0) {
                engine_set_move_variance(atoi(argv[i + 1]));
                i++;
            }
        }
        if (book_path && book_load(book_path)) {
            fprintf(stderr, "info string Book loaded: %u entries\n", book_entries);
        }
    }

    while (fgets(line, sizeof(line), stdin)) {
        line[strcspn(line, "\n")] = '\0';

        if (strcmp(line, "uci") == 0) {
            printf("id name TI84Chess\n");
            printf("id author hunterchen\n");
            printf("uciok\n");
            fflush(stdout);
        } else if (strcmp(line, "isready") == 0) {
            printf("readyok\n");
            fflush(stdout);
        } else if (strcmp(line, "ucinewgame") == 0) {
            engine_new_game();
        } else if (strncmp(line, "position ", 9) == 0) {
            handle_position(line + 9);
        } else if (strncmp(line, "go", 2) == 0) {
            handle_go(line + 2);
        } else if (strcmp(line, "quit") == 0) {
            break;
        }
    }

    free(book_data);
    return 0;
}
