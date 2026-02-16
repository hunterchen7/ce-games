#include "eval.h"
#include "movegen.h"

/* ========== Phase Weights ========== */

/* Pawn=0, Knight=1, Bishop=1, Rook=2, Queen=4, King=0 */
const int16_t phase_weight[6] = { 0, 1, 1, 2, 4, 0 };

/* ========== Combined Material + PST Tables ========== */

/* PeSTO values: mg_table[i][sq] = mg_value[i] + mg_pst[i][sq]
   mg_value = {82, 337, 365, 477, 1025, 0}
   eg_value = {94, 281, 297, 512, 936, 0}
   Index 0=a8 .. 63=h1, from white's perspective. */

const int16_t mg_table[6][64] = {
    /* Pawn (base 82) */
    {
         82,  82,  82,  82,  82,  82,  82,  82,
        180, 216, 143, 177, 150, 208, 116,  71,
         76,  89, 108, 113, 147, 138, 107,  62,
         68,  95,  88, 103, 105,  94,  99,  59,
         55,  80,  77,  94,  99,  88,  92,  57,
         56,  78,  78,  72,  85,  85, 115,  70,
         47,  81,  62,  59,  67, 106, 120,  60,
         82,  82,  82,  82,  82,  82,  82,  82,
    },
    /* Knight (base 337) */
    {
        170, 248, 303, 288, 398, 240, 322, 230,
        264, 296, 409, 373, 360, 399, 344, 320,
        290, 397, 374, 402, 421, 466, 410, 381,
        328, 354, 356, 390, 374, 406, 355, 359,
        324, 341, 353, 350, 365, 356, 358, 329,
        314, 328, 349, 347, 356, 354, 362, 321,
        308, 284, 325, 334, 336, 355, 323, 318,
        232, 316, 279, 304, 320, 309, 318, 314,
    },
    /* Bishop (base 365) */
    {
        336, 369, 283, 328, 340, 323, 372, 357,
        339, 381, 347, 352, 395, 424, 383, 318,
        349, 402, 408, 405, 400, 415, 402, 363,
        361, 370, 384, 415, 402, 402, 372, 363,
        359, 378, 378, 391, 399, 377, 375, 369,
        365, 380, 380, 380, 379, 392, 383, 375,
        369, 380, 381, 365, 372, 386, 398, 366,
        332, 362, 351, 344, 352, 353, 326, 344,
    },
    /* Rook (base 477) */
    {
        509, 519, 509, 528, 540, 486, 508, 520,
        504, 509, 535, 539, 557, 544, 503, 521,
        472, 496, 503, 513, 494, 522, 538, 493,
        453, 466, 484, 503, 501, 512, 469, 457,
        441, 451, 465, 476, 486, 470, 483, 454,
        432, 452, 461, 460, 480, 477, 472, 444,
        433, 461, 457, 468, 476, 488, 471, 406,
        458, 464, 478, 494, 493, 484, 440, 451,
    },
    /* Queen (base 1025) */
    {
         997, 1025, 1054, 1037, 1084, 1069, 1068, 1070,
        1001,  986, 1020, 1026, 1009, 1082, 1053, 1079,
        1012, 1008, 1032, 1033, 1054, 1081, 1072, 1082,
         998,  998, 1009, 1009, 1024, 1042, 1023, 1026,
        1016,  999, 1016, 1015, 1023, 1021, 1028, 1022,
        1011, 1027, 1014, 1023, 1020, 1027, 1039, 1030,
         990, 1017, 1036, 1027, 1033, 1040, 1022, 1026,
        1024, 1007, 1016, 1035, 1010, 1000,  994,  975,
    },
    /* King (base 0) */
    {
        -65,  23,  16, -15, -56, -34,   2,  13,
         29,  -1, -20,  -7,  -8,  -4, -38, -29,
         -9,  24,   2, -16, -20,   6,  22, -22,
        -17, -20, -12, -27, -30, -25, -23, -36,
        -49,  -1, -27, -39, -46, -44, -33, -51,
        -14, -14, -22, -46, -44, -30, -15, -27,
          1,   7,  -8, -64, -43, -16,   9,   8,
        -15,  36,  12, -54,   8, -28,  24,  14,
    },
};

const int16_t eg_table[6][64] = {
    /* Pawn (base 94) */
    {
         94,  94,  94,  94,  94,  94,  94,  94,
        272, 267, 252, 228, 241, 226, 259, 281,
        188, 194, 179, 161, 150, 147, 176, 178,
        126, 118, 107,  99,  92,  98, 111, 111,
        107, 103,  91,  87,  87,  86,  97,  93,
         98, 101,  88,  95,  94,  89,  93,  86,
        107, 102, 102,  84, 107,  94,  96,  87,
         94,  94,  94,  94,  94,  94,  94,  94,
    },
    /* Knight (base 281) */
    {
        223, 243, 268, 253, 250, 254, 218, 182,
        256, 273, 256, 279, 272, 256, 257, 229,
        257, 261, 291, 290, 280, 272, 262, 240,
        264, 284, 303, 303, 303, 292, 289, 263,
        263, 275, 297, 306, 297, 298, 285, 263,
        258, 278, 280, 296, 291, 278, 261, 259,
        239, 261, 271, 276, 279, 261, 258, 237,
        252, 230, 258, 266, 259, 263, 231, 217,
    },
    /* Bishop (base 297) */
    {
        283, 276, 286, 289, 290, 288, 280, 273,
        289, 293, 304, 285, 294, 284, 293, 283,
        299, 289, 297, 296, 295, 303, 297, 301,
        294, 306, 309, 306, 311, 307, 300, 299,
        291, 300, 310, 316, 304, 307, 294, 288,
        285, 294, 305, 307, 310, 300, 290, 282,
        283, 279, 290, 296, 301, 288, 282, 270,
        274, 288, 274, 292, 288, 281, 292, 280,
    },
    /* Rook (base 512) */
    {
        525, 522, 530, 527, 524, 524, 520, 517,
        523, 525, 525, 523, 509, 515, 520, 515,
        519, 519, 519, 517, 516, 509, 507, 509,
        516, 515, 525, 513, 514, 513, 511, 514,
        515, 517, 520, 516, 507, 506, 504, 501,
        508, 512, 507, 511, 505, 500, 504, 496,
        506, 506, 512, 514, 503, 503, 501, 509,
        503, 514, 515, 511, 507, 499, 516, 492,
    },
    /* Queen (base 936) */
    {
        927, 958, 958, 963, 963, 955, 946, 956,
        919, 956, 968, 977, 994, 961, 966, 936,
        916, 942, 945, 985, 983, 971, 955, 945,
        939, 958, 960, 981, 993, 976, 993, 972,
        918, 964, 955, 983, 967, 970, 975, 959,
        920, 909, 951, 942, 945, 953, 946, 941,
        914, 913, 906, 920, 920, 913, 900, 904,
        903, 908, 914, 893, 931, 904, 916, 895,
    },
    /* King (base 0) */
    {
        -74, -35, -18, -18, -11,  15,   4, -17,
        -12,  17,  14,  17,  17,  38,  23,  11,
         10,  17,  23,  15,  20,  45,  44,  13,
         -8,  22,  24,  27,  26,  33,  26,   3,
        -18,  -4,  21,  24,  27,  23,   9, -11,
        -19,  -3,  11,  21,  23,  16,   7,  -9,
        -27, -11,   4,  13,  14,   4,  -5, -17,
        -53, -34, -21, -11, -28, -14, -24, -43,
    },
};

/* ========== Feature Constants ========== */

#define BISHOP_PAIR_MG  19
#define BISHOP_PAIR_EG  56

/* Tempo bonus (Stockfish-tuned) */
#define TEMPO_MG  10
#define TEMPO_EG  9

/* Pawn structure penalties/bonuses */
#define DOUBLED_MG   12
#define DOUBLED_EG   3
#define ISOLATED_MG  12
#define ISOLATED_EG  17

/* Connected pawn bonus by relative rank (2nd..7th) */
static const int16_t connected_bonus[] = { 0, 9, 10, 16, 39, 65, 117 };

/* Passed pawn bonus: mg = 20*rr, eg = 10*(rr+r+1) where r=relrank-2, rr=r*(r-1) */
/* Precomputed for relative ranks 2..7 (index 0..5) */
static const int16_t passed_mg[] = { 0, 0, 0, 7, 43, 85 };
static const int16_t passed_eg[] = { 13, 27, 40, 67, 135, 229 };

/* Rook file bonuses */
#define ROOK_OPEN_MG      38
#define ROOK_OPEN_EG      24
#define ROOK_SEMIOPEN_MG  23
#define ROOK_SEMIOPEN_EG  11

/* Pawn shield bonus per pawn in front of king */
#define SHIELD_MG  6
#define SHIELD_EG   0

/* Knight mobility bonus table (0..8 safe squares) — from Stockfish classical */
static const int16_t knight_mob_mg[] = { -19, -13, -6, 0, 6, 13, 16, 17, 19 };
static const int16_t knight_mob_eg[] = { -61, -43, -24, -2, 13, 26, 41, 45, 50 };

/* Bishop mobility bonus table (0..13 safe squares) */
static const int16_t bishop_mob_mg[] = { -12, -6, 2, 9, 11, 16, 18, 21, 25, 27, 29, 30, 32, 37 };
static const int16_t bishop_mob_eg[] = { -17, -9, -1, 7, 12, 17, 23, 27, 32, 35, 37, 39, 41, 40 };

/* Knight move offsets (0x88) */
static const int8_t knight_offsets[] = { -33, -31, -18, -14, 14, 18, 31, 33 };

/* Bishop (diagonal) ray offsets */
static const int8_t bishop_offsets[] = { -17, -15, 15, 17 };

/* ========== Evaluation Helpers ========== */

/* Build per-file pawn presence: pawns[file] = bitmask of ranks with pawns.
   Bit 0 = rank 0 (row 0 = rank 8), bit 7 = rank 7 (row 7 = rank 1). */
static void build_pawn_files(const board_t *b, uint8_t side,
                             uint8_t pawn_files[8])
{
    uint8_t i, sq, row, col, piece;
    for (i = 0; i < 8; i++) pawn_files[i] = 0;
    for (i = 0; i < b->piece_count[side]; i++) {
        sq = b->piece_list[side][i];
        piece = b->squares[sq];
        if (PIECE_TYPE(piece) == PIECE_PAWN) {
            row = SQ_TO_ROW(sq);
            col = SQ_TO_COL(sq);
            pawn_files[col] |= (uint8_t)(1u << row);
        }
    }
}

/* Check if square is attacked by an enemy pawn */
static uint8_t pawn_attacks_sq(const board_t *b, uint8_t sq, uint8_t by_side)
{
    uint8_t piece;
    if (by_side == WHITE) {
        /* White pawns attack upward-diagonally; they'd be on row+1 */
        uint8_t s1 = sq + 17; /* down-left from sq = white pawn attacking up-right */
        uint8_t s2 = sq + 15; /* down-right from sq = white pawn attacking up-left */
        if (SQ_VALID(s1)) { piece = b->squares[s1]; if (piece == MAKE_PIECE(COLOR_WHITE, PIECE_PAWN)) return 1; }
        if (SQ_VALID(s2)) { piece = b->squares[s2]; if (piece == MAKE_PIECE(COLOR_WHITE, PIECE_PAWN)) return 1; }
    } else {
        /* Black pawns attack downward-diagonally; they'd be on row-1 */
        uint8_t s1 = sq - 17;
        uint8_t s2 = sq - 15;
        if (SQ_VALID(s1)) { piece = b->squares[s1]; if (piece == MAKE_PIECE(COLOR_BLACK, PIECE_PAWN)) return 1; }
        if (SQ_VALID(s2)) { piece = b->squares[s2]; if (piece == MAKE_PIECE(COLOR_BLACK, PIECE_PAWN)) return 1; }
    }
    return 0;
}

/* ========== Main Evaluation ========== */

int16_t evaluate(const board_t *b)
{
    int16_t mg, eg, phase;
    int32_t score;
    uint8_t i, sq, row, col, type;
    uint8_t w_pawns[8], b_pawns[8];

    /* Material + PST from white's perspective (maintained incrementally) */
    mg = b->mg[WHITE] - b->mg[BLACK];
    eg = b->eg[WHITE] - b->eg[BLACK];

    /* Bishop pair bonus */
    if (b->bishop_count[WHITE] >= 2) { mg += BISHOP_PAIR_MG; eg += BISHOP_PAIR_EG; }
    if (b->bishop_count[BLACK] >= 2) { mg -= BISHOP_PAIR_MG; eg -= BISHOP_PAIR_EG; }

    /* ---- Tempo ---- */
#ifndef NO_TEMPO
    if (b->side == WHITE) { mg += TEMPO_MG; eg += TEMPO_EG; }
    else                   { mg -= TEMPO_MG; eg -= TEMPO_EG; }
#endif

    /* ---- Build pawn file arrays ---- */
    build_pawn_files(b, WHITE, w_pawns);
    build_pawn_files(b, BLACK, b_pawns);

    /* ---- Pawn structure + Rook files + Pawn shield ---- */
    {
        /* Iterate white pieces */
        for (i = 0; i < b->piece_count[WHITE]; i++) {
            sq = b->piece_list[WHITE][i];
            type = PIECE_TYPE(b->squares[sq]);
            row = SQ_TO_ROW(sq);
            col = SQ_TO_COL(sq);

            if (type == PIECE_PAWN) {
                uint8_t rel_rank = 7 - row; /* white pawn: row 6=rank2, row 1=rank7 */
                uint8_t ri; /* index into passed/connected arrays */

#ifndef NO_PAWNS
                /* Doubled: another white pawn on same file (any other rank) */
                if (w_pawns[col] & ~(1u << row))
                    { mg -= DOUBLED_MG; eg -= DOUBLED_EG; }

                /* Isolated: no friendly pawns on adjacent files */
                {
                    uint8_t adj = 0;
                    if (col > 0) adj |= w_pawns[col - 1];
                    if (col < 7) adj |= w_pawns[col + 1];
                    if (!adj) { mg -= ISOLATED_MG; eg -= ISOLATED_EG; }
                }

                /* Connected: supported by a friendly pawn diagonally behind */
                {
                    uint8_t supported = 0;
                    uint8_t s1 = sq + 17; /* row+1, col+1 */
                    uint8_t s2 = sq + 15; /* row+1, col-1 */
                    if (SQ_VALID(s1) && b->squares[s1] == MAKE_PIECE(COLOR_WHITE, PIECE_PAWN)) supported = 1;
                    if (SQ_VALID(s2) && b->squares[s2] == MAKE_PIECE(COLOR_WHITE, PIECE_PAWN)) supported = 1;
                    if (supported && rel_rank >= 2 && rel_rank <= 7) {
                        ri = rel_rank - 2;
                        if (ri < 7) { mg += connected_bonus[ri]; eg += connected_bonus[ri]; }
                    }
                }
#endif /* NO_PAWNS */

#ifndef NO_PASSED
                /* Passed pawn: no enemy pawns on same or adjacent files ahead */
                {
                    uint8_t passed = 1;
                    int8_t f;
                    for (f = (int8_t)col - 1; f <= (int8_t)col + 1; f++) {
                        if (f < 0 || f > 7) continue;
                        /* Check if any black pawn is on rank ahead (lower row for white) */
                        uint8_t mask = b_pawns[f];
                        uint8_t r;
                        for (r = 0; r < row; r++) {
                            if (mask & (1u << r)) { passed = 0; break; }
                        }
                        if (!passed) break;
                    }
                    if (passed && rel_rank >= 2) {
                        ri = rel_rank - 2;
                        if (ri < 6) { mg += passed_mg[ri]; eg += passed_eg[ri]; }
                    }
                }
#endif /* NO_PASSED */
            }

#ifndef NO_ROOK_FILES
            else if (type == PIECE_ROOK) {
                /* Open file: no pawns of either color */
                if (!w_pawns[col] && !b_pawns[col]) {
                    mg += ROOK_OPEN_MG; eg += ROOK_OPEN_EG;
                }
                /* Semi-open: no friendly pawns but enemy pawns present */
                else if (!w_pawns[col] && b_pawns[col]) {
                    mg += ROOK_SEMIOPEN_MG; eg += ROOK_SEMIOPEN_EG;
                }
            }
#endif /* NO_ROOK_FILES */
        }

        /* Iterate black pieces */
        for (i = 0; i < b->piece_count[BLACK]; i++) {
            sq = b->piece_list[BLACK][i];
            type = PIECE_TYPE(b->squares[sq]);
            row = SQ_TO_ROW(sq);
            col = SQ_TO_COL(sq);

            if (type == PIECE_PAWN) {
                uint8_t rel_rank = row; /* black pawn: row 1=rank7 (their 2nd), row 6=rank2 (their 7th) */
                uint8_t ri;

#ifndef NO_PAWNS
                /* Doubled */
                if (b_pawns[col] & ~(1u << row))
                    { mg += DOUBLED_MG; eg += DOUBLED_EG; }

                /* Isolated */
                {
                    uint8_t adj = 0;
                    if (col > 0) adj |= b_pawns[col - 1];
                    if (col < 7) adj |= b_pawns[col + 1];
                    if (!adj) { mg += ISOLATED_MG; eg += ISOLATED_EG; }
                }

                /* Connected */
                {
                    uint8_t supported = 0;
                    uint8_t s1 = sq - 17; /* row-1, col-1 */
                    uint8_t s2 = sq - 15; /* row-1, col+1 */
                    if (SQ_VALID(s1) && b->squares[s1] == MAKE_PIECE(COLOR_BLACK, PIECE_PAWN)) supported = 1;
                    if (SQ_VALID(s2) && b->squares[s2] == MAKE_PIECE(COLOR_BLACK, PIECE_PAWN)) supported = 1;
                    if (supported && rel_rank >= 2 && rel_rank <= 7) {
                        ri = rel_rank - 2;
                        if (ri < 7) { mg -= connected_bonus[ri]; eg -= connected_bonus[ri]; }
                    }
                }
#endif /* NO_PAWNS */

#ifndef NO_PASSED
                /* Passed pawn for black: no white pawns ahead (higher rows) */
                {
                    uint8_t passed = 1;
                    int8_t f;
                    for (f = (int8_t)col - 1; f <= (int8_t)col + 1; f++) {
                        if (f < 0 || f > 7) continue;
                        uint8_t mask = w_pawns[f];
                        uint8_t r;
                        for (r = row + 1; r < 8; r++) {
                            if (mask & (1u << r)) { passed = 0; break; }
                        }
                        if (!passed) break;
                    }
                    if (passed && rel_rank >= 2) {
                        ri = rel_rank - 2;
                        if (ri < 6) { mg -= passed_mg[ri]; eg -= passed_eg[ri]; }
                    }
                }
#endif /* NO_PASSED */
            }

#ifndef NO_ROOK_FILES
            else if (type == PIECE_ROOK) {
                if (!b_pawns[col] && !w_pawns[col]) {
                    mg -= ROOK_OPEN_MG; eg -= ROOK_OPEN_EG;
                }
                else if (!b_pawns[col] && w_pawns[col]) {
                    mg -= ROOK_SEMIOPEN_MG; eg -= ROOK_SEMIOPEN_EG;
                }
            }
#endif /* NO_ROOK_FILES */
        }
    }

    /* ---- Mobility (knights and bishops) ---- */
#ifndef NO_MOBILITY
    {
        uint8_t enemy_side;
        /* White pieces */
        enemy_side = BLACK;
        for (i = 0; i < b->piece_count[WHITE]; i++) {
            sq = b->piece_list[WHITE][i];
            type = PIECE_TYPE(b->squares[sq]);

            if (type == PIECE_KNIGHT) {
                uint8_t mob = 0, j;
                for (j = 0; j < 8; j++) {
                    uint8_t dest = sq + knight_offsets[j];
                    if (!SQ_VALID(dest)) continue;
                    uint8_t occ = b->squares[dest];
                    /* Skip squares with friendly pieces */
                    if (occ != PIECE_NONE && IS_WHITE(occ)) continue;
                    /* Skip squares attacked by enemy pawns */
                    if (pawn_attacks_sq(b, dest, enemy_side)) continue;
                    mob++;
                }
                if (mob > 8) mob = 8;
                mg += knight_mob_mg[mob];
                eg += knight_mob_eg[mob];
            }
            else if (type == PIECE_BISHOP) {
                uint8_t mob = 0, j;
                for (j = 0; j < 4; j++) {
                    uint8_t dest = sq + bishop_offsets[j];
                    while (SQ_VALID(dest)) {
                        uint8_t occ = b->squares[dest];
                        if (occ != PIECE_NONE && IS_WHITE(occ)) break;
                        if (!pawn_attacks_sq(b, dest, enemy_side)) mob++;
                        if (occ != PIECE_NONE) break; /* stop at any piece */
                        dest += bishop_offsets[j];
                    }
                }
                if (mob > 13) mob = 13;
                mg += bishop_mob_mg[mob];
                eg += bishop_mob_eg[mob];
            }
        }

        /* Black pieces */
        enemy_side = WHITE;
        for (i = 0; i < b->piece_count[BLACK]; i++) {
            sq = b->piece_list[BLACK][i];
            type = PIECE_TYPE(b->squares[sq]);

            if (type == PIECE_KNIGHT) {
                uint8_t mob = 0, j;
                for (j = 0; j < 8; j++) {
                    uint8_t dest = sq + knight_offsets[j];
                    if (!SQ_VALID(dest)) continue;
                    uint8_t occ = b->squares[dest];
                    if (occ != PIECE_NONE && IS_BLACK(occ)) continue;
                    if (pawn_attacks_sq(b, dest, enemy_side)) continue;
                    mob++;
                }
                if (mob > 8) mob = 8;
                mg -= knight_mob_mg[mob];
                eg -= knight_mob_eg[mob];
            }
            else if (type == PIECE_BISHOP) {
                uint8_t mob = 0, j;
                for (j = 0; j < 4; j++) {
                    uint8_t dest = sq + bishop_offsets[j];
                    while (SQ_VALID(dest)) {
                        uint8_t occ = b->squares[dest];
                        if (occ != PIECE_NONE && IS_BLACK(occ)) break;
                        if (!pawn_attacks_sq(b, dest, enemy_side)) mob++;
                        if (occ != PIECE_NONE) break;
                        dest += bishop_offsets[j];
                    }
                }
                if (mob > 13) mob = 13;
                mg -= bishop_mob_mg[mob];
                eg -= bishop_mob_eg[mob];
            }
        }
    }
#endif /* NO_MOBILITY */

    /* ---- Pawn shield (simplified king safety) ---- */
#ifndef NO_SHIELD
    {
        uint8_t ksq, krow, kcol;
        uint8_t shield;

        /* White king */
        ksq = b->king_sq[WHITE];
        krow = SQ_TO_ROW(ksq);
        kcol = SQ_TO_COL(ksq);
        shield = 0;
        /* Check 3 squares directly in front of king (row-1) */
        if (krow > 0) {
            int8_t c;
            for (c = (int8_t)kcol - 1; c <= (int8_t)kcol + 1; c++) {
                if (c < 0 || c > 7) continue;
                uint8_t fsq = RC_TO_SQ(krow - 1, c);
                if (b->squares[fsq] == MAKE_PIECE(COLOR_WHITE, PIECE_PAWN))
                    shield++;
            }
        }
        mg += shield * SHIELD_MG;
        eg += shield * SHIELD_EG;

        /* Black king */
        ksq = b->king_sq[BLACK];
        krow = SQ_TO_ROW(ksq);
        kcol = SQ_TO_COL(ksq);
        shield = 0;
        if (krow < 7) {
            int8_t c;
            for (c = (int8_t)kcol - 1; c <= (int8_t)kcol + 1; c++) {
                if (c < 0 || c > 7) continue;
                uint8_t fsq = RC_TO_SQ(krow + 1, c);
                if (b->squares[fsq] == MAKE_PIECE(COLOR_BLACK, PIECE_PAWN))
                    shield++;
            }
        }
        mg -= shield * SHIELD_MG;
        eg -= shield * SHIELD_EG;
    }
#endif /* NO_SHIELD */

    /* Tapered eval */
    phase = b->phase;
    if (phase > PHASE_MAX) phase = PHASE_MAX;
    if (phase < 0) phase = 0;

    score = ((int32_t)mg * phase + (int32_t)eg * (PHASE_MAX - phase)) / PHASE_MAX;

    /* Return from side-to-move perspective */
    return (b->side == WHITE) ? (int16_t)score : (int16_t)(-score);
}
