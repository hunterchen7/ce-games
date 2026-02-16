#include "eval.h"
#include "movegen.h"
#include <string.h>

/* ========== Eval Sub-Profiling ========== */

#ifdef SEARCH_PROFILE
#include <sys/timers.h>
#include <string.h>

static eval_profile_t _ep;

void eval_profile_reset(void) {
    memset(&_ep, 0, sizeof(_ep));
}

const eval_profile_t *eval_profile_get(void) {
    return &_ep;
}

#define EP_VARS  uint32_t _ept
#define EP_B()   (_ept = timer_GetSafe(1, TIMER_UP))
#define EP_E(f)  (_ep.f += timer_GetSafe(1, TIMER_UP) - _ept)

#else

#define EP_VARS
#define EP_B()
#define EP_E(f)

#endif /* SEARCH_PROFILE */

/* ========== Phase Weights ========== */

/* Pawn=0, Knight=1, Bishop=1, Rook=2, Queen=4, King=0 */
const int16_t phase_weight[6] = { 0, 1, 1, 2, 4, 0 };

/* ========== Combined Material + PST Tables ========== */

/* PeSTO values: mg_table[i][sq] = mg_value[i] + mg_pst[i][sq]
   mg_value = {82, 337, 365, 477, 1025, 0}
   eg_value = {94, 281, 297, 512, 936, 0}
   Index 0=a8 .. 63=h1, from white's perspective. */

const int16_t mg_table[6][64] = {
    /* Pawn */
    {
          77,   77,   77,   77,   77,   77,   77,   77,
         168,  202,  133,  165,  140,  194,  108,   66,
          71,   83,  101,  105,  137,  129,  100,   58,
          63,   89,   82,   96,   98,   88,   92,   55,
          51,   75,   72,   88,   92,   82,   86,   53,
          52,   73,   73,   67,   79,   79,  107,   65,
          44,   76,   58,   55,   63,   99,  112,   56,
          77,   77,   77,   77,   77,   77,   77,   77,
    },
    /* Knight */
    {
         153,  223,  273,  259,  359,  216,  290,  207,
         238,  267,  368,  336,  324,  359,  310,  288,
         261,  358,  337,  362,  379,  420,  369,  343,
         295,  319,  321,  351,  337,  366,  320,  323,
         292,  307,  318,  315,  329,  321,  322,  296,
         283,  295,  314,  313,  321,  319,  326,  289,
         277,  256,  293,  301,  303,  320,  291,  286,
         209,  285,  251,  274,  288,  278,  286,  283,
    },
    /* Bishop */
    {
         300,  330,  253,  293,  304,  289,  332,  319,
         303,  340,  310,  315,  353,  379,  342,  284,
         312,  359,  365,  362,  357,  371,  359,  324,
         323,  331,  343,  371,  359,  359,  332,  324,
         321,  338,  338,  349,  357,  337,  335,  330,
         326,  340,  340,  340,  339,  350,  342,  335,
         330,  340,  340,  326,  332,  345,  356,  327,
         297,  324,  314,  307,  315,  315,  291,  307,
    },
    /* Rook */
    {
         445,  453,  445,  461,  472,  425,  444,  454,
         440,  445,  467,  471,  487,  475,  439,  455,
         412,  433,  439,  448,  432,  456,  470,  431,
         396,  407,  423,  439,  438,  447,  410,  399,
         385,  394,  406,  416,  425,  411,  422,  397,
         377,  395,  403,  402,  419,  417,  412,  388,
         378,  403,  399,  409,  416,  426,  411,  355,
         400,  405,  418,  432,  431,  423,  384,  394,
    },
    /* Queen */
    {
         970,  997, 1026, 1009, 1055, 1040, 1039, 1041,
         974,  960,  993,  998,  982, 1053, 1025, 1050,
         985,  981, 1004, 1005, 1026, 1052, 1043, 1053,
         971,  971,  982,  982,  996, 1014,  996,  998,
         989,  972,  989,  988,  996,  994, 1000,  995,
         984,  999,  987,  996,  993,  999, 1011, 1002,
         963,  990, 1008,  999, 1005, 1012,  995,  998,
         996,  980,  989, 1007,  983,  973,  967,  949,
    },
    /* King */
    {
         -65,   23,   16,  -15,  -56,  -34,    2,   13,
          29,   -1,  -20,   -7,   -8,   -4,  -38,  -29,
          -9,   24,    2,  -16,  -20,    6,   22,  -22,
         -17,  -20,  -12,  -27,  -30,  -25,  -23,  -36,
         -49,   -1,  -27,  -39,  -46,  -44,  -33,  -51,
         -14,  -14,  -22,  -46,  -44,  -30,  -15,  -27,
           1,    7,   -8,  -64,  -43,  -16,    9,    8,
         -15,   36,   12,  -54,    8,  -28,   24,   14,
    },
};

const int16_t eg_table[6][64] = {
    /* Pawn */
    {
         105,  105,  105,  105,  105,  105,  105,  105,
         303,  297,  280,  254,  268,  251,  288,  313,
         209,  216,  199,  179,  167,  164,  196,  198,
         140,  131,  119,  110,  102,  109,  123,  123,
         119,  115,  101,   97,   97,   96,  108,  103,
         109,  112,   98,  106,  105,   99,  103,   96,
         119,  113,  113,   93,  119,  105,  107,   97,
         105,  105,  105,  105,  105,  105,  105,  105,
    },
    /* Knight */
    {
         241,  262,  289,  273,  270,  274,  235,  196,
         276,  295,  276,  301,  293,  276,  277,  247,
         277,  282,  314,  313,  302,  293,  283,  259,
         285,  306,  327,  327,  327,  315,  312,  284,
         284,  297,  320,  330,  320,  322,  307,  284,
         278,  300,  302,  319,  314,  300,  282,  279,
         258,  282,  292,  298,  301,  282,  278,  256,
         272,  248,  278,  287,  279,  284,  249,  234,
    },
    /* Bishop */
    {
         291,  284,  294,  298,  299,  297,  288,  281,
         298,  302,  313,  293,  303,  292,  302,  291,
         308,  298,  306,  305,  304,  312,  306,  310,
         303,  315,  318,  315,  320,  316,  309,  308,
         300,  309,  319,  325,  313,  316,  303,  297,
         293,  303,  314,  316,  319,  309,  299,  290,
         291,  287,  299,  305,  310,  297,  290,  278,
         282,  297,  282,  301,  297,  289,  301,  288,
    },
    /* Rook */
    {
         575,  572,  581,  578,  574,  574,  570,  567,
         573,  575,  575,  573,  558,  564,  570,  564,
         569,  569,  569,  567,  566,  558,  556,  558,
         566,  564,  575,  562,  563,  562,  560,  563,
         564,  567,  570,  566,  556,  555,  552,  549,
         557,  561,  556,  560,  554,  548,  552,  544,
         555,  555,  561,  563,  551,  551,  549,  558,
         551,  563,  564,  560,  556,  547,  566,  539,
    },
    /* Queen */
    {
         986, 1019, 1019, 1024, 1024, 1016, 1006, 1017,
         978, 1017, 1030, 1039, 1057, 1022, 1028,  996,
         974, 1002, 1005, 1048, 1046, 1033, 1016, 1005,
         999, 1019, 1021, 1043, 1056, 1038, 1056, 1034,
         976, 1025, 1016, 1046, 1029, 1032, 1037, 1020,
         979,  967, 1012, 1002, 1005, 1014, 1006, 1001,
         972,  971,  964,  979,  979,  971,  957,  962,
         961,  966,  972,  950,  990,  962,  974,  952,
    },
    /* King */
    {
         -76,  -36,  -18,  -18,  -11,   15,    4,  -17,
         -12,   17,   14,   17,   17,   39,   24,   11,
          10,   17,   24,   15,   21,   46,   45,   13,
          -8,   23,   25,   28,   27,   34,   27,    3,
         -18,   -4,   22,   25,   28,   24,    9,  -11,
         -20,   -3,   11,   22,   24,   16,    7,   -9,
         -28,  -11,    4,   13,   14,    4,   -5,  -17,
         -54,  -35,  -22,  -11,  -29,  -14,  -25,  -44,
    },
};

/* ========== Feature Constants ========== */

#define BISHOP_PAIR_MG  19
#define BISHOP_PAIR_EG  58

/* Tempo bonus (Stockfish-tuned) */
#define TEMPO_MG  10
#define TEMPO_EG  9

/* Pawn structure penalties/bonuses */
#define DOUBLED_MG   12
#define DOUBLED_EG   3
#define ISOLATED_MG  12
#define ISOLATED_EG  17

/* Connected pawn bonus by relative rank (2nd..7th) */
static const int16_t connected_bonus_mg[] = { 0, 9, 10, 16, 39, 65 };
static const int16_t connected_bonus_eg[] = { 0, 9, 10, 16, 39, 65 };

/* Passed pawn bonus: mg = 20*rr, eg = 10*(rr+r+1) where r=relrank-2, rr=r*(r-1) */
/* Precomputed for relative ranks 2..7 (index 0..5) */
static const int16_t passed_mg[] = { 0, 0, 0, 7, 43, 85 };
static const int16_t passed_eg[] = { 13, 27, 41, 67, 131, 229 };

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

/* Build pawn file arrays and pawn attack bitmap in one pass.
   w_pawns/b_pawns: [file] = bitmask of ranks with pawns (bit 0 = row 0).
   pawn_atk: [0x88 sq] = bit 0: attacked by white pawn, bit 1: attacked by black pawn. */
static void build_pawn_info(const board_t *b,
                            uint8_t w_pawns[8], uint8_t b_pawns[8],
                            uint8_t pawn_atk[128])
{
    uint8_t i, sq, row, col, piece, a;

    memset(w_pawns, 0, 8);
    memset(b_pawns, 0, 8);
    memset(pawn_atk, 0, 128);

    /* White pawns */
    for (i = 0; i < b->piece_count[WHITE]; i++) {
        sq = b->piece_list[WHITE][i];
        piece = b->squares[sq];
        if (PIECE_TYPE(piece) == PIECE_PAWN) {
            row = SQ_TO_ROW(sq);
            col = SQ_TO_COL(sq);
            w_pawns[col] |= (uint8_t)(1u << row);
            /* White pawns attack up-left (sq-17) and up-right (sq-15) */
            a = sq - 17; if (SQ_VALID(a)) pawn_atk[a] |= 1;
            a = sq - 15; if (SQ_VALID(a)) pawn_atk[a] |= 1;
        }
    }

    /* Black pawns */
    for (i = 0; i < b->piece_count[BLACK]; i++) {
        sq = b->piece_list[BLACK][i];
        piece = b->squares[sq];
        if (PIECE_TYPE(piece) == PIECE_PAWN) {
            row = SQ_TO_ROW(sq);
            col = SQ_TO_COL(sq);
            b_pawns[col] |= (uint8_t)(1u << row);
            /* Black pawns attack down-left (sq+17) and down-right (sq+15) */
            a = sq + 17; if (SQ_VALID(a)) pawn_atk[a] |= 2;
            a = sq + 15; if (SQ_VALID(a)) pawn_atk[a] |= 2;
        }
    }
}

/* ========== Main Evaluation ========== */

int evaluate(const board_t *b)
{
    static uint8_t pawn_atk[128];
    int mg, eg, phase;
    int score;
    uint8_t i, sq, row, col, type;
    uint8_t w_pawns[8], b_pawns[8];
    EP_VARS;

#ifdef SEARCH_PROFILE
    _ep.eval_count++;
#endif

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

    /* ---- Build pawn file arrays + attack bitmap ---- */
    EP_B();
    build_pawn_info(b, w_pawns, b_pawns, pawn_atk);
    EP_E(build_cy);

    /* ---- Pawn structure + Rook files + Pawn shield ---- */
    EP_B();
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
                        if (ri < 6) { mg += connected_bonus_mg[ri]; eg += connected_bonus_eg[ri]; }
                    }
                }
#endif /* NO_PAWNS */

#ifndef NO_PASSED
                /* Passed pawn: no enemy pawns on same or adjacent files ahead */
                {
                    uint8_t passed = 1;
                    int8_t f;
                    /* Mask for rows ahead of white pawn: bits 0..(row-1) */
                    uint8_t ahead = (uint8_t)((1u << row) - 1);
                    for (f = (int8_t)col - 1; f <= (int8_t)col + 1; f++) {
                        if (f < 0 || f > 7) continue;
                        if (b_pawns[f] & ahead) { passed = 0; break; }
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
                        if (ri < 6) { mg -= connected_bonus_mg[ri]; eg -= connected_bonus_eg[ri]; }
                    }
                }
#endif /* NO_PAWNS */

#ifndef NO_PASSED
                /* Passed pawn for black: no white pawns ahead (higher rows) */
                {
                    uint8_t passed = 1;
                    int8_t f;
                    /* Mask for rows ahead of black pawn: bits (row+1)..7 */
                    uint8_t ahead = (uint8_t)(~((1u << (row + 1)) - 1));
                    for (f = (int8_t)col - 1; f <= (int8_t)col + 1; f++) {
                        if (f < 0 || f > 7) continue;
                        if (w_pawns[f] & ahead) { passed = 0; break; }
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
    EP_E(pieces_cy);

    /* ---- Mobility (knights and bishops) ---- */
    /* Use pawn_atk bitmap: bit 0 = attacked by white, bit 1 = attacked by black.
       enemy_pawn_bit = 1 << enemy_side: BLACK(1)->2, WHITE(0)->1. */
    EP_B();
#ifndef NO_MOBILITY
    {
        /* White pieces (enemy = BLACK, check bit 1 = value 2) */
        for (i = 0; i < b->piece_count[WHITE]; i++) {
            sq = b->piece_list[WHITE][i];
            type = PIECE_TYPE(b->squares[sq]);

            if (type == PIECE_KNIGHT) {
                uint8_t mob = 0, j;
                for (j = 0; j < 8; j++) {
                    uint8_t dest = sq + knight_offsets[j];
                    if (!SQ_VALID(dest)) continue;
                    uint8_t occ = b->squares[dest];
                    if (occ != PIECE_NONE && IS_WHITE(occ)) continue;
                    if (pawn_atk[dest] & 2) continue;
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
                        if (!(pawn_atk[dest] & 2)) mob++;
                        if (occ != PIECE_NONE) break;
                        dest += bishop_offsets[j];
                    }
                }
                if (mob > 13) mob = 13;
                mg += bishop_mob_mg[mob];
                eg += bishop_mob_eg[mob];
            }
        }

        /* Black pieces (enemy = WHITE, check bit 0 = value 1) */
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
                    if (pawn_atk[dest] & 1) continue;
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
                        if (!(pawn_atk[dest] & 1)) mob++;
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
    EP_E(mobility_cy);

    /* ---- Pawn shield (simplified king safety) ---- */
    EP_B();
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
    EP_E(shield_cy);

    /* Tapered eval */
    phase = b->phase;
    if (phase > PHASE_MAX) phase = PHASE_MAX;
    if (phase < 0) phase = 0;

    score = (mg * phase + eg * (PHASE_MAX - phase)) / PHASE_MAX;

    /* Return from side-to-move perspective */
    return (b->side == WHITE) ? score : -score;
}
