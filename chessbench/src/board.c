#include "board.h"
#include "zobrist.h"
#include <string.h>

/* ========== Castling Rights Table ========== */

/* After any move touching square sq, castling rights are AND'd with this mask.
   Indexed by 0x88 square. Non-special squares map to 0xFF (preserve all). */
static const uint8_t castling_mask[128] = {
    /* rank 8 (row 0): a8=0x00 .. h8=0x07 */
    (uint8_t)~CASTLE_BQ, 0xFF, 0xFF, 0xFF,
    (uint8_t)~(CASTLE_BK | CASTLE_BQ), 0xFF, 0xFF, (uint8_t)~CASTLE_BK,
    /* 0x08..0x0F (off-board) */
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,

    /* rank 7 (row 1) */
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,

    /* rank 6 (row 2) */
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,

    /* rank 5 (row 3) */
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,

    /* rank 4 (row 4) */
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,

    /* rank 3 (row 5) */
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,

    /* rank 2 (row 6) */
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,

    /* rank 1 (row 7): a1=0x70 .. h1=0x77 */
    (uint8_t)~CASTLE_WQ, 0xFF, 0xFF, 0xFF,
    (uint8_t)~(CASTLE_WK | CASTLE_WQ), 0xFF, 0xFF, (uint8_t)~CASTLE_WK,
    /* 0x78..0x7F (off-board) */
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
};

/* ========== Piece Translation ========== */

uint8_t ui_to_engine_piece(int8_t ui_piece)
{
    if (ui_piece == 0) return PIECE_NONE;
    if (ui_piece > 0) return MAKE_PIECE(COLOR_WHITE, (uint8_t)ui_piece);
    return MAKE_PIECE(COLOR_BLACK, (uint8_t)(-ui_piece));
}

int8_t engine_to_ui_piece(uint8_t piece)
{
    uint8_t type;
    if (piece == PIECE_NONE) return 0;
    type = PIECE_TYPE(piece);
    return IS_BLACK(piece) ? -(int8_t)type : (int8_t)type;
}

/* ========== Hash Computation ========== */

/* Compute full Zobrist hash and lock from scratch for a board position. */
static void board_compute_hash(board_t *b)
{
    uint32_t h = 0;
    uint16_t l = 0;
    int sq;
    uint8_t piece, pidx, sq64;

    for (sq = 0; sq < 128; sq++) {
        if (!SQ_VALID(sq)) continue;
        piece = b->squares[sq];
        if (piece == PIECE_NONE) continue;
        pidx = zobrist_piece_index(piece);
        sq64 = SQ_TO_SQ64(sq);
        h ^= zobrist_piece[pidx][sq64];
        l ^= lock_piece[pidx][sq64];
    }

    h ^= zobrist_castle[b->castling];
    l ^= lock_castle[b->castling];

    if (b->ep_square != SQ_NONE) {
        uint8_t file = SQ_TO_COL(b->ep_square);
        h ^= zobrist_ep_file[file];
        l ^= lock_ep_file[file];
    }

    if (b->side == BLACK) {
        h ^= zobrist_side;
        l ^= lock_side;
    }

    b->hash = h;
    b->lock = l;
}

/* ========== Board Initialization ========== */

void board_init(board_t *b)
{
    if (!zobrist_is_initialized())
        zobrist_init(0);

    memset(b, 0, sizeof(board_t));
    b->ep_square = SQ_NONE;
    b->king_sq[WHITE] = SQ_NONE;
    b->king_sq[BLACK] = SQ_NONE;
}

/* Add a piece to the board (squares array + piece list).
   Does NOT update hash — caller must handle that. */
static void board_add_piece(board_t *b, uint8_t sq, uint8_t piece)
{
    uint8_t side = IS_BLACK(piece) ? BLACK : WHITE;
    b->squares[sq] = piece;
    if (PIECE_TYPE(piece) == PIECE_KING) {
        b->king_sq[side] = sq;
    }
    b->piece_list[side][b->piece_count[side]] = sq;
    b->piece_count[side]++;
}

/* Remove a piece from the piece list (swap with last).
   Does NOT clear squares[] — caller must handle that. */
static void plist_remove(board_t *b, uint8_t side, uint8_t sq)
{
    uint8_t i;
    for (i = 0; i < b->piece_count[side]; i++) {
        if (b->piece_list[side][i] == sq) {
            b->piece_count[side]--;
            b->piece_list[side][i] = b->piece_list[side][b->piece_count[side]];
            return;
        }
    }
}

/* Update piece list: change a piece's square from old_sq to new_sq. */
static void plist_move(board_t *b, uint8_t side, uint8_t old_sq, uint8_t new_sq)
{
    uint8_t i;
    for (i = 0; i < b->piece_count[side]; i++) {
        if (b->piece_list[side][i] == old_sq) {
            b->piece_list[side][i] = new_sq;
            return;
        }
    }
}

/* ========== Position Setup ========== */

void board_set_from_ui(board_t *b,
                       const int8_t ui_board[8][8],
                       int8_t turn,
                       uint8_t castling,
                       uint8_t ep_row, uint8_t ep_col,
                       uint8_t halfmove_clock,
                       uint16_t fullmove_number)
{
    int r, c;
    uint8_t piece;

    board_init(b);

    for (r = 0; r < 8; r++) {
        for (c = 0; c < 8; c++) {
            piece = ui_to_engine_piece(ui_board[r][c]);
            if (piece != PIECE_NONE) {
                board_add_piece(b, RC_TO_SQ(r, c), piece);
            }
        }
    }

    b->side = (turn == 1) ? WHITE : BLACK;
    b->castling = castling;
    b->halfmove = halfmove_clock;
    b->fullmove = fullmove_number;

    if (ep_row != 0xFF && ep_col != 0xFF) {
        b->ep_square = RC_TO_SQ(ep_row, ep_col);
    }

    board_compute_hash(b);
}

void board_startpos(board_t *b)
{
    static const int8_t start[8][8] = {
        { -4, -2, -3, -5, -6, -3, -2, -4 },  /* row 0: black back rank */
        { -1, -1, -1, -1, -1, -1, -1, -1 },  /* row 1: black pawns */
        {  0,  0,  0,  0,  0,  0,  0,  0 },
        {  0,  0,  0,  0,  0,  0,  0,  0 },
        {  0,  0,  0,  0,  0,  0,  0,  0 },
        {  0,  0,  0,  0,  0,  0,  0,  0 },
        {  1,  1,  1,  1,  1,  1,  1,  1 },  /* row 6: white pawns */
        {  4,  2,  3,  5,  6,  3,  2,  4 },  /* row 7: white back rank */
    };

    board_set_from_ui(b, start, 1, CASTLE_ALL, 0xFF, 0xFF, 0, 1);
}

/* ========== Make Move ========== */

void board_make(board_t *b, move_t m, undo_t *u)
{
    uint8_t from = m.from;
    uint8_t to   = m.to;
    uint8_t flags = m.flags;
    uint8_t piece = b->squares[from];
    uint8_t captured = b->squares[to];
    uint8_t side = b->side;
    uint8_t opp  = side ^ 1;
    uint8_t type = PIECE_TYPE(piece);
    uint8_t from64 = SQ_TO_SQ64(from);
    uint8_t to64   = SQ_TO_SQ64(to);
    uint8_t pidx   = zobrist_piece_index(piece);

    /* Save undo state */
    u->captured = captured;
    u->castling = b->castling;
    u->ep_square = b->ep_square;
    u->halfmove = b->halfmove;
    u->fullmove = b->fullmove;
    u->hash = b->hash;
    u->lock = b->lock;
    u->moved_piece = piece;
    u->flags = flags;

    /* Update halfmove clock */
    if (type == PIECE_PAWN || (flags & FLAG_CAPTURE))
        b->halfmove = 0;
    else
        b->halfmove++;

    /* Remove piece from origin (hash) */
    b->hash ^= zobrist_piece[pidx][from64];
    b->lock ^= lock_piece[pidx][from64];

    /* Handle capture */
    if (flags & FLAG_EN_PASSANT) {
        /* en passant: captured pawn is on a different square */
        uint8_t cap_sq = (side == WHITE) ? (to + 16) : (to - 16);
        uint8_t cap_piece = b->squares[cap_sq];

        if (cap_piece != PIECE_NONE) {
            uint8_t cap64 = SQ_TO_SQ64(cap_sq);
            uint8_t cap_pidx = zobrist_piece_index(cap_piece);

            b->hash ^= zobrist_piece[cap_pidx][cap64];
            b->lock ^= lock_piece[cap_pidx][cap64];

            b->squares[cap_sq] = PIECE_NONE;
            plist_remove(b, opp, cap_sq);
        }

        /* Store the actual captured piece for unmake reference
           (u->captured was set to PIECE_NONE since to square was empty) */
        u->captured = cap_piece;
    } else if (captured != PIECE_NONE) {
        /* Normal capture */
        uint8_t cap_pidx = zobrist_piece_index(captured);

        b->hash ^= zobrist_piece[cap_pidx][to64];
        b->lock ^= lock_piece[cap_pidx][to64];

        plist_remove(b, opp, to);
    }

    /* Move piece on board */
    b->squares[from] = PIECE_NONE;
    b->squares[to] = piece;

    /* Update piece list */
    plist_move(b, side, from, to);

    /* Place piece at destination (hash) */
    b->hash ^= zobrist_piece[pidx][to64];
    b->lock ^= lock_piece[pidx][to64];

    /* Handle promotion */
    if (flags & FLAG_PROMOTION) {
        uint8_t promo_type;
        uint8_t promo_piece;
        uint8_t promo_pidx;

        switch (flags & FLAG_PROMO_MASK) {
            case FLAG_PROMO_R: promo_type = PIECE_ROOK;   break;
            case FLAG_PROMO_B: promo_type = PIECE_BISHOP; break;
            case FLAG_PROMO_N: promo_type = PIECE_KNIGHT; break;
            default:           promo_type = PIECE_QUEEN;  break;
        }
        promo_piece = MAKE_PIECE(side == WHITE ? COLOR_WHITE : COLOR_BLACK, promo_type);
        promo_pidx = zobrist_piece_index(promo_piece);

        /* Remove pawn hash at destination, add promoted piece hash */
        b->hash ^= zobrist_piece[pidx][to64];
        b->lock ^= lock_piece[pidx][to64];
        b->hash ^= zobrist_piece[promo_pidx][to64];
        b->lock ^= lock_piece[promo_pidx][to64];

        b->squares[to] = promo_piece;
    }

    /* Handle castling (move the rook) */
    if (flags & FLAG_CASTLE) {
        uint8_t rook_from, rook_to;
        uint8_t rook, rook_pidx, rf64, rt64;

        if (to > from) {
            /* Kingside */
            rook_from = from + 3;  /* h-file */
            rook_to   = from + 1;  /* f-file */
        } else {
            /* Queenside */
            rook_from = from - 4;  /* a-file */
            rook_to   = from - 1;  /* d-file */
        }

        rook = b->squares[rook_from];
        rook_pidx = zobrist_piece_index(rook);
        rf64 = SQ_TO_SQ64(rook_from);
        rt64 = SQ_TO_SQ64(rook_to);

        b->hash ^= zobrist_piece[rook_pidx][rf64];
        b->hash ^= zobrist_piece[rook_pidx][rt64];
        b->lock ^= lock_piece[rook_pidx][rf64];
        b->lock ^= lock_piece[rook_pidx][rt64];

        b->squares[rook_from] = PIECE_NONE;
        b->squares[rook_to] = rook;
        plist_move(b, side, rook_from, rook_to);
    }

    /* Update king square */
    if (type == PIECE_KING) {
        b->king_sq[side] = to;
    }

    /* Update castling rights */
    {
        uint8_t old_castling = b->castling;
        b->castling &= castling_mask[from];
        b->castling &= castling_mask[to];
        if (old_castling != b->castling) {
            b->hash ^= zobrist_castle[old_castling];
            b->hash ^= zobrist_castle[b->castling];
            b->lock ^= lock_castle[old_castling];
            b->lock ^= lock_castle[b->castling];
        }
    }

    /* Update en passant square */
    {
        uint8_t old_ep = b->ep_square;
        if (flags & FLAG_DOUBLE_PUSH) {
            b->ep_square = (side == WHITE) ? (from - 16) : (from + 16);
        } else {
            b->ep_square = SQ_NONE;
        }
        /* Hash out old EP, hash in new EP */
        if (old_ep != SQ_NONE) {
            b->hash ^= zobrist_ep_file[SQ_TO_COL(old_ep)];
            b->lock ^= lock_ep_file[SQ_TO_COL(old_ep)];
        }
        if (b->ep_square != SQ_NONE) {
            b->hash ^= zobrist_ep_file[SQ_TO_COL(b->ep_square)];
            b->lock ^= lock_ep_file[SQ_TO_COL(b->ep_square)];
        }
    }

    /* Flip side to move */
    b->side ^= 1;
    b->hash ^= zobrist_side;
    b->lock ^= lock_side;

    /* Update fullmove counter (increments after black moves) */
    if (side == BLACK)
        b->fullmove++;
}

/* ========== Unmake Move ========== */

void board_unmake(board_t *b, move_t m, const undo_t *u)
{
    uint8_t from = m.from;
    uint8_t to   = m.to;
    uint8_t flags = u->flags;
    uint8_t piece = u->moved_piece;
    uint8_t side;

    /* Flip side back */
    b->side ^= 1;
    side = b->side;

    /* Handle promotion: restore pawn on destination before moving back */
    if (flags & FLAG_PROMOTION) {
        b->squares[to] = piece;  /* put the pawn back */
    }

    /* Move piece back */
    b->squares[from] = piece;
    b->squares[to] = PIECE_NONE;
    plist_move(b, side, to, from);

    /* Restore king square */
    if (PIECE_TYPE(piece) == PIECE_KING) {
        b->king_sq[side] = from;
    }

    /* Handle castling (move the rook back) */
    if (flags & FLAG_CASTLE) {
        uint8_t rook_from, rook_to;

        if (to > from) {
            /* Kingside */
            rook_from = from + 3;
            rook_to   = from + 1;
        } else {
            /* Queenside */
            rook_from = from - 4;
            rook_to   = from - 1;
        }

        b->squares[rook_from] = b->squares[rook_to];
        b->squares[rook_to] = PIECE_NONE;
        plist_move(b, side, rook_to, rook_from);
    }

    /* Restore captured piece */
    if (flags & FLAG_EN_PASSANT) {
        uint8_t cap_sq = (side == WHITE) ? (to + 16) : (to - 16);
        uint8_t opp = side ^ 1;
        b->squares[cap_sq] = u->captured;
        b->piece_list[opp][b->piece_count[opp]] = cap_sq;
        b->piece_count[opp]++;
    } else if (u->captured != PIECE_NONE) {
        uint8_t opp = side ^ 1;
        b->squares[to] = u->captured;
        b->piece_list[opp][b->piece_count[opp]] = to;
        b->piece_count[opp]++;
    }

    /* Restore state from undo */
    b->castling = u->castling;
    b->ep_square = u->ep_square;
    b->halfmove = u->halfmove;
    b->fullmove = u->fullmove;
    b->hash = u->hash;
    b->lock = u->lock;
}
