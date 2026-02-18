#ifndef TT_H
#define TT_H

#include "types.h"

/* TT entry flags */
#define TT_NONE  0
#define TT_EXACT 1
#define TT_ALPHA 2  /* upper bound (fail-low) */
#define TT_BETA  3  /* lower bound (fail-high) */

/* Packed move: 16-bit compact representation for TT storage.
   bits 0-5:   from square (0..63, sq64 encoding)
   bits 6-11:  to square (0..63, sq64 encoding)
   bit  12:    is_promotion
   bits 13-14: promo piece (0=Q, 1=R, 2=B, 3=N)
   bit  15:    reserved */
typedef uint16_t tt_move16_t;

#define TT_MOVE_NONE 0

/* TT entry: 8 bytes */
typedef struct {
    uint16_t lock16;      /* independent 16-bit verification key */
    int16_t  score;       /* evaluation score */
    tt_move16_t best_move;/* packed move */
    int8_t   depth;       /* search depth */
    uint8_t  flag;        /* TT_EXACT, TT_ALPHA, TT_BETA */
} tt_entry_t;

/* Table size (power of 2) */
#ifndef TT_SIZE
#define TT_SIZE  4096
#endif
#define TT_MASK  (TT_SIZE - 1)

/* Initialize (clear) the transposition table */
void tt_clear(void);

/* Probe the TT. Returns non-zero if entry found and valid.
   On hit, *score, *best_move, *depth, *flag are filled in.
   The caller must adjust mate scores by ply. */
uint8_t tt_probe(zhash_t hash, uint16_t lock,
                 int *score, tt_move16_t *best_move,
                 int8_t *depth, uint8_t *flag);

/* Store an entry in the TT (always-replace). */
void tt_store(zhash_t hash, uint16_t lock,
              int score, tt_move16_t best_move,
              int8_t depth, uint8_t flag);

/* Pack a move_t into tt_move16_t */
tt_move16_t tt_pack_move(move_t m);

/* Unpack a tt_move16_t back to move_t (flags are partial — no capture/EP/double push bits).
   The caller must verify legality before using the unpacked move. */
move_t tt_unpack_move(tt_move16_t packed);

#endif /* TT_H */
