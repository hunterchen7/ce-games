#ifndef TYPES_H
#define TYPES_H

#include <stdint.h>

/* ========== Zobrist Hash Type ========== */

/* On the eZ80, unsigned int is 24 bits (native word size).
   XOR/shift on unsigned int compiles to single inline instructions,
   while uint32_t operations require expensive library calls (__lxor).
   24-bit hash + 16-bit lock = 40 bits of collision resistance. */
typedef unsigned int zhash_t;

/* ========== Piece Encoding ========== */

#define PIECE_NONE    0
#define OFFBOARD      0xFF   /* sentinel in off-board squares[]; non-zero, invalid type */
#define PIECE_PAWN    1
#define PIECE_KNIGHT  2
#define PIECE_BISHOP  3
#define PIECE_ROOK    4
#define PIECE_QUEEN   5
#define PIECE_KING    6

#define COLOR_WHITE   0x00
#define COLOR_BLACK   0x80
#define COLOR_MASK    0x80
#define TYPE_MASK     0x07

#define MAKE_PIECE(color, type)  ((color) | (type))
#define PIECE_TYPE(p)            ((p) & TYPE_MASK)
#define PIECE_COLOR(p)           ((p) & COLOR_MASK)
#define IS_WHITE(p)              (!((p) & COLOR_MASK))
#define IS_BLACK(p)              ((p) & COLOR_MASK)

#define WHITE  0
#define BLACK  1

/* ========== 0x88 Board Indexing ========== */

#define RC_TO_SQ(r, c)   ((r) * 16 + (c))
#define SQ_TO_ROW(sq)    ((sq) >> 4)
#define SQ_TO_COL(sq)    ((sq) & 7)
#define SQ_VALID(sq)     (!((sq) & 0x88))
#define SQ_TO_SQ64(sq)   ((((sq) >> 1) & 0x38) | ((sq) & 7))
#define SQ64_TO_SQ(s64)  (((s64) & 0x38) << 1 | ((s64) & 7))

/* Named squares (0x88) */
#define SQ_A8  0x00
#define SQ_E8  0x04
#define SQ_H8  0x07
#define SQ_A1  0x70
#define SQ_E1  0x74
#define SQ_H1  0x77

#define SQ_NONE 0xFF

/* ========== Castling Rights ========== */

#define CASTLE_WK  0x01
#define CASTLE_WQ  0x02
#define CASTLE_BK  0x04
#define CASTLE_BQ  0x08
#define CASTLE_ALL 0x0F

/* ========== Move Flags ========== */

#define FLAG_CAPTURE     0x01
#define FLAG_CASTLE      0x02
#define FLAG_EN_PASSANT  0x04
#define FLAG_PROMOTION   0x08
#define FLAG_PROMO_Q     0x00
#define FLAG_PROMO_R     0x10
#define FLAG_PROMO_B     0x20
#define FLAG_PROMO_N     0x30
#define FLAG_PROMO_MASK  0x30
#define FLAG_DOUBLE_PUSH 0x40

/* ========== Move Type ========== */

typedef struct {
    uint8_t from;
    uint8_t to;
    uint8_t flags;
} move_t;

#define MOVE_NONE ((move_t){SQ_NONE, SQ_NONE, 0})
#define MOVE_EQ(a, b) ((a).from == (b).from && (a).to == (b).to && (a).flags == (b).flags)

/* ========== Scored Move (for search) ========== */

typedef struct {
    move_t move;
    int16_t score;
} scored_move_t;

/* ========== Score Constants ========== */

#define SCORE_INF    30000
#define SCORE_MATE   29000
#define SCORE_DRAW   0

/* ========== Max Limits ========== */

#define MAX_PLY         64
#define MAX_MOVES       256  /* max legal moves in any position */

#ifndef MOVE_POOL_SIZE
#define MOVE_POOL_SIZE  2048
#endif

#endif /* TYPES_H */
