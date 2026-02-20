#ifndef ZOBRIST_H
#define ZOBRIST_H

#include "types.h"

/* Primary hash keys — kept as uint32_t for fast 4-byte array stride.
   Only the lower 24 bits are used when XOR'd into zhash_t state.
   Use ZHASH() to load 3 bytes as zhash_t, avoiding 32-bit __ixor calls. */
extern uint32_t zobrist_piece[12][64];
extern uint32_t zobrist_castle[16];
extern uint32_t zobrist_ep_file[8];
extern uint32_t zobrist_side;

/* Load only the lower 24 bits from a uint32_t Zobrist entry.
   Keeps 4-byte array stride for fast indexing. */
#define ZHASH(entry) (*(const zhash_t *)&(entry))

/* Memory-to-memory 24-bit XOR via inline asm (eZ80 only).
   Avoids __ixor library call (~45 cy) which uses expensive stack
   manipulation to extract the upper byte of 24-bit registers.
   This version uses ld a,(de); xor a,(hl); ld (hl),a per byte
   for ~24 cycles total — direct byte access, no stack tricks. */
#ifdef __ez80__
static inline void zhash_xor_asm(void *dest, const void *src)
{
    /* Memory-to-memory 24-bit XOR: *dest ^= *src (3 bytes).
       Saves/restores HL and DE to avoid register pressure issues.
       4 push/pop for save/restore, 2 push/pop to marshal args into HL/DE. */
    (void)dest; (void)src;
    __asm__ volatile(
        "push hl\n\t"
        "push de\n\t"
        "push %1\n\t"
        "push %0\n\t"
        "pop hl\n\t"
        "pop de\n\t"
        "ld a, (de)\n\t"
        "xor a, (hl)\n\t"
        "ld (hl), a\n\t"
        "inc de\n\t"
        "inc hl\n\t"
        "ld a, (de)\n\t"
        "xor a, (hl)\n\t"
        "ld (hl), a\n\t"
        "inc de\n\t"
        "inc hl\n\t"
        "ld a, (de)\n\t"
        "xor a, (hl)\n\t"
        "ld (hl), a\n\t"
        "pop de\n\t"
        "pop hl"
        :
        : "r"(dest), "r"(src)
        : "a", "memory"
    );
}
#define ZHASH_XOR(dest, src) zhash_xor_asm(&(dest), &(src))
#else
#define ZHASH_XOR(dest, src) ((dest) ^= (src))
#endif

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
