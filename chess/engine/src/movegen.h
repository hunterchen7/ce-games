#ifndef MOVEGEN_H
#define MOVEGEN_H

#include "board.h"

/* Generation modes */
#define GEN_ALL      0
#define GEN_CAPTURES 1
#define GEN_QUIETS   2

/* Generate pseudo-legal moves for the side to move.
   Returns the number of moves written to list[].
   Moves are NOT legality-checked (king may be left in check). */
uint8_t generate_moves(const board_t *b, move_t *list, uint8_t mode);

/* Generate pseudo-legal moves from a specific square.
   Returns the number of moves written to list[]. */
uint8_t generate_moves_from(const board_t *b, uint8_t from_sq, move_t *list);

/* Check if sq is attacked by the given side.
   Does not require move generation — pure board query. */
uint8_t is_square_attacked(const board_t *b, uint8_t sq, uint8_t by_side);

/* Check if the current position is legal (side that just moved
   did not leave their king in check). */
static inline uint8_t board_is_legal(const board_t *b)
{
    /* After a move, b->side has flipped. The side that just moved
       is the opponent — check if their king is still attacked. */
    uint8_t prev_side = b->side ^ 1;
    return !is_square_attacked(b, b->king_sq[prev_side], b->side);
}

#endif /* MOVEGEN_H */
