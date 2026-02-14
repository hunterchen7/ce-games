#ifndef EVAL_H
#define EVAL_H

#include "board.h"

/* Combined material + piece-square tables (middlegame and endgame).
   Indexed as [piece_type_0based][sq64].
   piece_type_0based: 0=pawn, 1=knight, 2=bishop, 3=rook, 4=queen, 5=king.
   sq64: 0=a8 .. 63=h1, from white's perspective.
   For black pieces, use PST_FLIP(sq64) to mirror. */
extern const int16_t mg_table[6][64];
extern const int16_t eg_table[6][64];

/* Phase weight per piece type (0-based).
   Pawn=0, Knight=1, Bishop=1, Rook=2, Queen=4, King=0.
   Total starting phase = 24 (PHASE_MAX). */
extern const int16_t phase_weight[6];

#define PHASE_MAX 24

/* Convert PIECE_TYPE (1..6) to 0-based eval table index */
#define EVAL_INDEX(type) ((type) - 1)

/* Mirror sq64 for black piece PST lookup */
#define PST_FLIP(sq64) ((sq64) ^ 56)

/* Evaluate the position from the side-to-move perspective.
   Returns score in centipawns. Positive = good for side to move. */
int16_t evaluate(const board_t *b);

#endif /* EVAL_H */
