#ifndef ZOBRIST_H
#define ZOBRIST_H

#include "types.h"

/* Primary hash keys — kept as uint32_t for fast 4-byte array stride.
   Only the lower 24 bits are used when XOR'd into zhash_t state. */
extern uint32_t zobrist_piece[12][64];
extern uint32_t zobrist_castle[16];
extern uint32_t zobrist_ep_file[8];
extern uint32_t zobrist_side;

/* Independent 16-bit lock keys for TT verification */
extern uint16_t lock_piece[12][64];
extern uint16_t lock_castle[16];
extern uint16_t lock_ep_file[8];
extern uint16_t lock_side;

/* Initialize all Zobrist keys from a PRNG seed.
   Must be called once before any hashing.
   board_init() calls this automatically with the default seed
   if not already initialized. */
void zobrist_init(uint32_t seed);

/* Returns non-zero if zobrist_init() has been called. */
uint8_t zobrist_is_initialized(void);

/* Piece index for Zobrist tables: maps engine piece to 0..11.
   white pawn=0, white knight=1, ..., white king=5,
   black pawn=6, black knight=7, ..., black king=11.
   PIECE_NONE returns an undefined value — caller must not hash empty squares. */
static inline uint8_t zobrist_piece_index(uint8_t piece)
{
    uint8_t type = PIECE_TYPE(piece);   /* 1..6 */
    uint8_t side = IS_BLACK(piece) ? 6 : 0;
    return side + type - 1;
}

#endif /* ZOBRIST_H */
