#include "tt.h"
#include <string.h>

/* ========== Transposition Table ========== */

static tt_entry_t tt[TT_SIZE];

void tt_clear(void)
{
    memset(tt, 0, sizeof(tt));
}

uint8_t tt_probe(zhash_t hash, uint16_t lock,
                 int *score, tt_move16_t *best_move,
                 int8_t *depth, uint8_t *flag)
{
    tt_entry_t *e = &tt[hash & TT_MASK];

    if (e->flag == TT_NONE) return 0;
    if (e->lock16 != lock) return 0;

    *score = e->score;
    *best_move = e->best_move;
    *depth = e->depth;
    *flag = e->flag;
    return 1;
}

void tt_store(zhash_t hash, uint16_t lock,
              int score, tt_move16_t best_move,
              int8_t depth, uint8_t flag)
{
    tt_entry_t *e = &tt[hash & TT_MASK];

    e->lock16 = lock;
    e->score = score;
    e->best_move = best_move;
    e->depth = depth;
    e->flag = flag;
}

/* ========== Move Packing ========== */

tt_move16_t tt_pack_move(move_t m)
{
    uint16_t packed;
    uint8_t from64 = SQ_TO_SQ64(m.from);
    uint8_t to64   = SQ_TO_SQ64(m.to);

    packed = (uint16_t)from64 | ((uint16_t)to64 << 6);

    if (m.flags & FLAG_PROMOTION) {
        packed |= (1u << 12);
        packed |= (uint16_t)((m.flags & FLAG_PROMO_MASK) >> 4) << 13;
    }

    return packed;
}

move_t tt_unpack_move(tt_move16_t packed)
{
    move_t m;
    uint8_t from64 = packed & 0x3F;
    uint8_t to64   = (packed >> 6) & 0x3F;

    m.from = SQ64_TO_SQ(from64);
    m.to   = SQ64_TO_SQ(to64);
    m.flags = 0;

    if (packed & (1u << 12)) {
        m.flags |= FLAG_PROMOTION;
        m.flags |= (uint8_t)(((packed >> 13) & 3) << 4);
    }

    return m;
}
