#include "zobrist.h"

/* ========== Key Tables ========== */

uint32_t zobrist_piece[12][64];
uint32_t zobrist_castle[16];
uint32_t zobrist_ep_file[8];
uint32_t zobrist_side;

uint16_t lock_piece[12][64];
uint16_t lock_castle[16];
uint16_t lock_ep_file[8];
uint16_t lock_side;

/* ========== PRNG (xorshift32) ========== */

static uint32_t prng_state;

static uint32_t prng_next(void)
{
    uint32_t x = prng_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    prng_state = x;
    return x;
}

static uint16_t prng_next16(void)
{
    return (uint16_t)(prng_next() >> 16);
}

/* ========== Initialization ========== */

static uint8_t zobrist_initialized = 0;

uint8_t zobrist_is_initialized(void)
{
    return zobrist_initialized;
}

void zobrist_init(uint32_t seed)
{
    int i, j;

    prng_state = seed ? seed : 0x12345678u;
    zobrist_initialized = 1;

    for (i = 0; i < 12; i++)
        for (j = 0; j < 64; j++)
            zobrist_piece[i][j] = prng_next();

    for (i = 0; i < 16; i++)
        zobrist_castle[i] = prng_next();

    for (i = 0; i < 8; i++)
        zobrist_ep_file[i] = prng_next();

    zobrist_side = prng_next();

    /* Independent lock keys */
    for (i = 0; i < 12; i++)
        for (j = 0; j < 64; j++)
            lock_piece[i][j] = prng_next16();

    for (i = 0; i < 16; i++)
        lock_castle[i] = prng_next16();

    for (i = 0; i < 8; i++)
        lock_ep_file[i] = prng_next16();

    lock_side = prng_next16();
}
