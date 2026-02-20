#ifndef DIRECTIONS_H
#define DIRECTIONS_H

/* Shared direction offset arrays for move generation and attack detection */

static const int8_t knight_offsets[8] = { -33, -31, -18, -14, 14, 18, 31, 33 };
static const int8_t bishop_offsets[4] = { -17, -15, 15, 17 };
static const int8_t rook_offsets[4]   = { -16, -1, 1, 16 };
static const int8_t king_offsets[8]   = { -17, -16, -15, -1, 1, 15, 16, 17 };

#endif /* DIRECTIONS_H */
