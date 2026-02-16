#include "movegen.h"

/* ========== Direction Tables ========== */

static const int8_t knight_offsets[8] = {
    -33, -31, -18, -14, 14, 18, 31, 33
};
static const int8_t bishop_offsets[4] = { -17, -15, 15, 17 };
static const int8_t rook_offsets[4]   = { -16, -1, 1, 16 };
static const int8_t king_offsets[8]   = { -17, -16, -15, -1, 1, 15, 16, 17 };

/* ========== Helpers ========== */

static inline uint8_t is_enemy(uint8_t piece, uint8_t side)
{
    if (piece == PIECE_NONE) return 0;
    return (IS_BLACK(piece) ? BLACK : WHITE) != side;
}

static inline move_t make_move(uint8_t from, uint8_t to, uint8_t flags)
{
    move_t m;
    m.from = from;
    m.to = to;
    m.flags = flags;
    return m;
}

/* ========== Pawn Moves ========== */

static uint8_t gen_pawn_moves(const board_t *b, uint8_t sq, uint8_t side,
                              move_t *list, uint8_t mode)
{
    uint8_t count = 0;
    int8_t dir    = (side == WHITE) ? -16 : 16;
    uint8_t start_row = (side == WHITE) ? 6 : 1;
    uint8_t promo_row = (side == WHITE) ? 0 : 7;
    uint8_t row = SQ_TO_ROW(sq);
    uint8_t target;
    uint8_t to_row;

    /* Single push */
    if (mode != GEN_CAPTURES) {
        target = sq + dir;
        if (SQ_VALID(target) && b->squares[target] == PIECE_NONE) {
            to_row = SQ_TO_ROW(target);
            if (to_row == promo_row) {
                list[count++] = make_move(sq, target, FLAG_PROMOTION | FLAG_PROMO_Q);
                list[count++] = make_move(sq, target, FLAG_PROMOTION | FLAG_PROMO_R);
                list[count++] = make_move(sq, target, FLAG_PROMOTION | FLAG_PROMO_B);
                list[count++] = make_move(sq, target, FLAG_PROMOTION | FLAG_PROMO_N);
            } else {
                list[count++] = make_move(sq, target, 0);
            }

            /* Double push */
            if (row == start_row) {
                target = sq + dir + dir;
                if (SQ_VALID(target) && b->squares[target] == PIECE_NONE) {
                    list[count++] = make_move(sq, target, FLAG_DOUBLE_PUSH);
                }
            }
        }
    }

    /* Captures (diagonal) */
    if (mode != GEN_QUIETS) {
        int8_t cap_dirs[2];
        int i;
        cap_dirs[0] = dir - 1;
        cap_dirs[1] = dir + 1;

        for (i = 0; i < 2; i++) {
            target = sq + cap_dirs[i];
            if (!SQ_VALID(target)) continue;

            if (is_enemy(b->squares[target], side)) {
                to_row = SQ_TO_ROW(target);
                if (to_row == promo_row) {
                    list[count++] = make_move(sq, target, FLAG_CAPTURE | FLAG_PROMOTION | FLAG_PROMO_Q);
                    list[count++] = make_move(sq, target, FLAG_CAPTURE | FLAG_PROMOTION | FLAG_PROMO_R);
                    list[count++] = make_move(sq, target, FLAG_CAPTURE | FLAG_PROMOTION | FLAG_PROMO_B);
                    list[count++] = make_move(sq, target, FLAG_CAPTURE | FLAG_PROMOTION | FLAG_PROMO_N);
                } else {
                    list[count++] = make_move(sq, target, FLAG_CAPTURE);
                }
            } else if (target == b->ep_square) {
                list[count++] = make_move(sq, target, FLAG_CAPTURE | FLAG_EN_PASSANT);
            }
        }
    }

    return count;
}

/* ========== Knight Moves ========== */

static uint8_t gen_knight_moves(const board_t *b, uint8_t sq, uint8_t side,
                                move_t *list, uint8_t mode)
{
    uint8_t count = 0;
    int i;

    for (i = 0; i < 8; i++) {
        uint8_t target = sq + knight_offsets[i];
        uint8_t occ;
        if (!SQ_VALID(target)) continue;
        occ = b->squares[target];

        if (occ == PIECE_NONE) {
            if (mode != GEN_CAPTURES)
                list[count++] = make_move(sq, target, 0);
        } else if (is_enemy(occ, side)) {
            if (mode != GEN_QUIETS)
                list[count++] = make_move(sq, target, FLAG_CAPTURE);
        }
    }
    return count;
}

/* ========== Sliding Moves (Bishop, Rook, Queen) ========== */

static uint8_t gen_sliding_moves(const board_t *b, uint8_t sq, uint8_t side,
                                 const int8_t *offsets, uint8_t num_dirs,
                                 move_t *list, uint8_t mode)
{
    uint8_t count = 0;
    uint8_t d;

    for (d = 0; d < num_dirs; d++) {
        int8_t dir = offsets[d];
        uint8_t target = sq + dir;

        while (SQ_VALID(target)) {
            uint8_t occ = b->squares[target];
            if (occ == PIECE_NONE) {
                if (mode != GEN_CAPTURES)
                    list[count++] = make_move(sq, target, 0);
            } else {
                if (is_enemy(occ, side) && mode != GEN_QUIETS)
                    list[count++] = make_move(sq, target, FLAG_CAPTURE);
                break;  /* blocked */
            }
            target += dir;
        }
    }
    return count;
}

/* ========== King Moves ========== */

static uint8_t gen_king_moves(const board_t *b, uint8_t sq, uint8_t side,
                              move_t *list, uint8_t mode)
{
    uint8_t count = 0;
    int i;

    /* Normal king moves */
    for (i = 0; i < 8; i++) {
        uint8_t target = sq + king_offsets[i];
        uint8_t occ;
        if (!SQ_VALID(target)) continue;
        occ = b->squares[target];

        if (occ == PIECE_NONE) {
            if (mode != GEN_CAPTURES)
                list[count++] = make_move(sq, target, 0);
        } else if (is_enemy(occ, side)) {
            if (mode != GEN_QUIETS)
                list[count++] = make_move(sq, target, FLAG_CAPTURE);
        }
    }

    /* Castling (quiet moves only, king must be on starting square) */
    if (mode != GEN_CAPTURES) {
        uint8_t w_rook = MAKE_PIECE(COLOR_WHITE, PIECE_ROOK);
        uint8_t b_rook = MAKE_PIECE(COLOR_BLACK, PIECE_ROOK);
        uint8_t in_check;

        if (side == WHITE && sq == SQ_E1 &&
            (b->castling & (CASTLE_WK | CASTLE_WQ))) {
            in_check = is_square_attacked(b, SQ_E1, BLACK);
            if (in_check) return count;
            /* Kingside: e1-g1, rook on h1 */
            if ((b->castling & CASTLE_WK) &&
                b->squares[SQ_H1] == w_rook &&
                b->squares[SQ_E1 + 1] == PIECE_NONE &&
                b->squares[SQ_E1 + 2] == PIECE_NONE &&
                !is_square_attacked(b, SQ_E1 + 1, BLACK) &&
                !is_square_attacked(b, SQ_E1 + 2, BLACK)) {
                list[count++] = make_move(SQ_E1, SQ_E1 + 2, FLAG_CASTLE);
            }
            /* Queenside: e1-c1, rook on a1 */
            if ((b->castling & CASTLE_WQ) &&
                b->squares[SQ_A1] == w_rook &&
                b->squares[SQ_E1 - 1] == PIECE_NONE &&
                b->squares[SQ_E1 - 2] == PIECE_NONE &&
                b->squares[SQ_E1 - 3] == PIECE_NONE &&
                !is_square_attacked(b, SQ_E1 - 1, BLACK) &&
                !is_square_attacked(b, SQ_E1 - 2, BLACK)) {
                list[count++] = make_move(SQ_E1, SQ_E1 - 2, FLAG_CASTLE);
            }
        } else if (side == BLACK && sq == SQ_E8 &&
                   (b->castling & (CASTLE_BK | CASTLE_BQ))) {
            in_check = is_square_attacked(b, SQ_E8, WHITE);
            if (in_check) return count;
            /* Kingside: e8-g8, rook on h8 */
            if ((b->castling & CASTLE_BK) &&
                b->squares[SQ_H8] == b_rook &&
                b->squares[SQ_E8 + 1] == PIECE_NONE &&
                b->squares[SQ_E8 + 2] == PIECE_NONE &&
                !is_square_attacked(b, SQ_E8 + 1, WHITE) &&
                !is_square_attacked(b, SQ_E8 + 2, WHITE)) {
                list[count++] = make_move(SQ_E8, SQ_E8 + 2, FLAG_CASTLE);
            }
            /* Queenside: e8-c8, rook on a8 */
            if ((b->castling & CASTLE_BQ) &&
                b->squares[SQ_A8] == b_rook &&
                b->squares[SQ_E8 - 1] == PIECE_NONE &&
                b->squares[SQ_E8 - 2] == PIECE_NONE &&
                b->squares[SQ_E8 - 3] == PIECE_NONE &&
                !is_square_attacked(b, SQ_E8 - 1, WHITE) &&
                !is_square_attacked(b, SQ_E8 - 2, WHITE)) {
                list[count++] = make_move(SQ_E8, SQ_E8 - 2, FLAG_CASTLE);
            }
        }
    }

    return count;
}

/* ========== Generate Moves For One Piece ========== */

static uint8_t gen_piece_moves(const board_t *b, uint8_t sq, uint8_t side,
                               move_t *list, uint8_t mode)
{
    uint8_t piece = b->squares[sq];
    uint8_t type = PIECE_TYPE(piece);

    switch (type) {
        case PIECE_PAWN:
            return gen_pawn_moves(b, sq, side, list, mode);
        case PIECE_KNIGHT:
            return gen_knight_moves(b, sq, side, list, mode);
        case PIECE_BISHOP:
            return gen_sliding_moves(b, sq, side, bishop_offsets, 4, list, mode);
        case PIECE_ROOK:
            return gen_sliding_moves(b, sq, side, rook_offsets, 4, list, mode);
        case PIECE_QUEEN:
            /* Queen = bishop + rook directions */
            {
                uint8_t n = gen_sliding_moves(b, sq, side, bishop_offsets, 4, list, mode);
                n += gen_sliding_moves(b, sq, side, rook_offsets, 4, list + n, mode);
                return n;
            }
        case PIECE_KING:
            return gen_king_moves(b, sq, side, list, mode);
        default:
            return 0;
    }
}

/* ========== Public: Generate All Moves ========== */

uint8_t generate_moves(const board_t *b, move_t *list, uint8_t mode)
{
    uint8_t count = 0;
    uint8_t side = b->side;
    uint8_t i;

    for (i = 0; i < b->piece_count[side]; i++) {
        uint8_t sq = b->piece_list[side][i];
        count += gen_piece_moves(b, sq, side, list + count, mode);
    }

    return count;
}

/* ========== Public: Generate Moves From Square ========== */

uint8_t generate_moves_from(const board_t *b, uint8_t from_sq, move_t *list)
{
    uint8_t piece = b->squares[from_sq];
    uint8_t side = b->side;

    if (piece == PIECE_NONE) return 0;

    /* Verify piece belongs to side to move */
    if ((IS_BLACK(piece) ? BLACK : WHITE) != side) return 0;

    return gen_piece_moves(b, from_sq, side, list, GEN_ALL);
}

/* ========== Public: Is Square Attacked ========== */

uint8_t is_square_attacked(const board_t *b, uint8_t sq, uint8_t by_side)
{
    uint8_t attacker_color = (by_side == WHITE) ? COLOR_WHITE : COLOR_BLACK;
    int i;
    uint8_t target;

    /* Check knight attacks */
    for (i = 0; i < 8; i++) {
        target = sq + knight_offsets[i];
        if (SQ_VALID(target)) {
            uint8_t p = b->squares[target];
            if (p != PIECE_NONE &&
                PIECE_COLOR(p) == attacker_color &&
                PIECE_TYPE(p) == PIECE_KNIGHT)
                return 1;
        }
    }

    /* Check pawn attacks */
    {
        /* Pawns attack diagonally. If by_side is WHITE, white pawns are
           below the target square (higher 0x88 index). */
        int8_t pawn_dir = (by_side == WHITE) ? 16 : -16;
        uint8_t pawn = MAKE_PIECE(attacker_color, PIECE_PAWN);

        target = sq + pawn_dir - 1;
        if (SQ_VALID(target) && b->squares[target] == pawn) return 1;
        target = sq + pawn_dir + 1;
        if (SQ_VALID(target) && b->squares[target] == pawn) return 1;
    }

    /* Check king attacks */
    for (i = 0; i < 8; i++) {
        target = sq + king_offsets[i];
        if (SQ_VALID(target)) {
            uint8_t p = b->squares[target];
            if (p != PIECE_NONE &&
                PIECE_COLOR(p) == attacker_color &&
                PIECE_TYPE(p) == PIECE_KING)
                return 1;
        }
    }

    /* Check bishop/queen attacks (diagonal) */
    for (i = 0; i < 4; i++) {
        int8_t dir = bishop_offsets[i];
        target = sq + dir;
        while (SQ_VALID(target)) {
            uint8_t p = b->squares[target];
            if (p != PIECE_NONE) {
                if (PIECE_COLOR(p) == attacker_color) {
                    uint8_t t = PIECE_TYPE(p);
                    if (t == PIECE_BISHOP || t == PIECE_QUEEN) return 1;
                }
                break;
            }
            target += dir;
        }
    }

    /* Check rook/queen attacks (straight) */
    for (i = 0; i < 4; i++) {
        int8_t dir = rook_offsets[i];
        target = sq + dir;
        while (SQ_VALID(target)) {
            uint8_t p = b->squares[target];
            if (p != PIECE_NONE) {
                if (PIECE_COLOR(p) == attacker_color) {
                    uint8_t t = PIECE_TYPE(p);
                    if (t == PIECE_ROOK || t == PIECE_QUEEN) return 1;
                }
                break;
            }
            target += dir;
        }
    }

    return 0;
}
