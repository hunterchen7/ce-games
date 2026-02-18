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
const uint8_t phase_weight[6] = { 0, 1, 1, 2, 4, 0 };

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

#ifndef PAWN_CACHE_SIZE
#define PAWN_CACHE_SIZE 32
#endif

#if (PAWN_CACHE_SIZE & (PAWN_CACHE_SIZE - 1)) != 0
#error "PAWN_CACHE_SIZE must be a power of two"
#endif

#define PAWN_CACHE_WAYS 4
#if (PAWN_CACHE_SIZE % PAWN_CACHE_WAYS) != 0
#error "PAWN_CACHE_SIZE must be divisible by PAWN_CACHE_WAYS"
#endif

#define PAWN_CACHE_SETS (PAWN_CACHE_SIZE / PAWN_CACHE_WAYS)
#if (PAWN_CACHE_SETS & (PAWN_CACHE_SETS - 1)) != 0
#error "PAWN_CACHE_SETS must be a power of two"
#endif

#define PAWN_CACHE_SET_MASK (PAWN_CACHE_SETS - 1)

typedef struct {
    zhash_t  key;           /* board pawn_hash */
    int16_t pawn_mg;        /* pawn-only mg contribution (white - black) */
    int16_t pawn_eg;        /* pawn-only eg contribution (white - black) */
    uint8_t w_pawns[8];     /* [file] -> rank bitmask */
    uint8_t b_pawns[8];     /* [file] -> rank bitmask */
    uint8_t pawn_atk[128];  /* bit0 white attacks, bit1 black attacks */
} pawn_cache_entry_t;

static pawn_cache_entry_t pawn_cache[PAWN_CACHE_SIZE];
static uint8_t pawn_cache_victim[PAWN_CACHE_SETS];

/* Build pawn-only derived data and scores once per pawn structure. */
static void build_pawn_cache(const board_t *b, pawn_cache_entry_t *e)
{
    const uint8_t white_pawn = MAKE_PIECE(COLOR_WHITE, PIECE_PAWN);
    const uint8_t black_pawn = MAKE_PIECE(COLOR_BLACK, PIECE_PAWN);
    uint8_t w_sq[8], b_sq[8], wn = 0, bn = 0;
    uint8_t i, sq, row, col, piece, a;
    int16_t mg = 0;
    int16_t eg = 0;

    memset(e->w_pawns, 0, sizeof(e->w_pawns));
    memset(e->b_pawns, 0, sizeof(e->b_pawns));
    memset(e->pawn_atk, 0, sizeof(e->pawn_atk));

    /* White pawns: file masks + pawn attack map */
    for (i = 0; i < b->piece_count[WHITE]; i++) {
        sq = b->piece_list[WHITE][i];
        piece = b->squares[sq];
        if (PIECE_TYPE(piece) != PIECE_PAWN) continue;
        row = SQ_TO_ROW(sq);
        col = SQ_TO_COL(sq);
        w_sq[wn++] = sq;
        e->w_pawns[col] |= (uint8_t)(1u << row);
        a = sq - 17; if (SQ_VALID(a)) e->pawn_atk[a] |= 1;
        a = sq - 15; if (SQ_VALID(a)) e->pawn_atk[a] |= 1;
    }

    /* Black pawns: file masks + pawn attack map */
    for (i = 0; i < b->piece_count[BLACK]; i++) {
        sq = b->piece_list[BLACK][i];
        piece = b->squares[sq];
        if (PIECE_TYPE(piece) != PIECE_PAWN) continue;
        row = SQ_TO_ROW(sq);
        col = SQ_TO_COL(sq);
        b_sq[bn++] = sq;
        e->b_pawns[col] |= (uint8_t)(1u << row);
        a = sq + 17; if (SQ_VALID(a)) e->pawn_atk[a] |= 2;
        a = sq + 15; if (SQ_VALID(a)) e->pawn_atk[a] |= 2;
    }

    /* White pawn structure terms */
    for (i = 0; i < wn; i++) {
        uint8_t rel_rank, ri;
        uint8_t ahead;
        sq = w_sq[i];
        row = SQ_TO_ROW(sq);
        col = SQ_TO_COL(sq);
        rel_rank = 7 - row;
        ri = rel_rank - 2;

#ifndef NO_PAWNS
        if (e->w_pawns[col] & (uint8_t)~(1u << row)) {
            mg -= DOUBLED_MG;
            eg -= DOUBLED_EG;
        }
        {
            uint8_t adj = 0;
            if (col > 0) adj |= e->w_pawns[col - 1];
            if (col < 7) adj |= e->w_pawns[col + 1];
            if (!adj) {
                mg -= ISOLATED_MG;
                eg -= ISOLATED_EG;
            }
        }
        {
            uint8_t supported = 0;
            uint8_t s1 = sq + 17;
            uint8_t s2 = sq + 15;
            if (SQ_VALID(s1) && b->squares[s1] == white_pawn) supported = 1;
            if (SQ_VALID(s2) && b->squares[s2] == white_pawn) supported = 1;
            if (supported && rel_rank >= 2) {
                mg += connected_bonus_mg[ri];
                eg += connected_bonus_eg[ri];
            }
        }
#endif /* NO_PAWNS */

#ifndef NO_PASSED
        ahead = (uint8_t)((1u << row) - 1);
        if (rel_rank >= 2) {
            if (!(e->b_pawns[col] & ahead) &&
                (col == 0 || !(e->b_pawns[col - 1] & ahead)) &&
                (col == 7 || !(e->b_pawns[col + 1] & ahead))) {
                mg += passed_mg[ri];
                eg += passed_eg[ri];
            }
        }
#endif /* NO_PASSED */
    }

    /* Black pawn structure terms */
    for (i = 0; i < bn; i++) {
        uint8_t rel_rank, ri;
        uint8_t ahead;
        sq = b_sq[i];
        row = SQ_TO_ROW(sq);
        col = SQ_TO_COL(sq);
        rel_rank = row;
        ri = rel_rank - 2;

#ifndef NO_PAWNS
        if (e->b_pawns[col] & (uint8_t)~(1u << row)) {
            mg += DOUBLED_MG;
            eg += DOUBLED_EG;
        }
        {
            uint8_t adj = 0;
            if (col > 0) adj |= e->b_pawns[col - 1];
            if (col < 7) adj |= e->b_pawns[col + 1];
            if (!adj) {
                mg += ISOLATED_MG;
                eg += ISOLATED_EG;
            }
        }
        {
            uint8_t supported = 0;
            uint8_t s1 = sq - 17;
            uint8_t s2 = sq - 15;
            if (SQ_VALID(s1) && b->squares[s1] == black_pawn) supported = 1;
            if (SQ_VALID(s2) && b->squares[s2] == black_pawn) supported = 1;
            if (supported && rel_rank >= 2) {
                mg -= connected_bonus_mg[ri];
                eg -= connected_bonus_eg[ri];
            }
        }
#endif /* NO_PAWNS */

#ifndef NO_PASSED
        ahead = (uint8_t)(~((1u << (row + 1)) - 1));
        if (rel_rank >= 2) {
            if (!(e->w_pawns[col] & ahead) &&
                (col == 0 || !(e->w_pawns[col - 1] & ahead)) &&
                (col == 7 || !(e->w_pawns[col + 1] & ahead))) {
                mg -= passed_mg[ri];
                eg -= passed_eg[ri];
            }
        }
#endif /* NO_PASSED */
    }

    e->pawn_mg = mg;
    e->pawn_eg = eg;
    e->key = b->pawn_hash;
}

/* ========== Main Evaluation ========== */

int evaluate(const board_t *b)
{
    const pawn_cache_entry_t *pc;
    const uint8_t *w_pawns;
    const uint8_t *b_pawns;
    const uint8_t *pawn_atk;
    pawn_cache_entry_t *slot;
    int mg, eg, phase;
    int score;
    uint8_t i, sq, col, type;
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

    /* ---- Probe/build pawn cache ---- */
    EP_B();
    {
        uint8_t set = (uint8_t)(b->pawn_hash & PAWN_CACHE_SET_MASK);
        pawn_cache_entry_t *set_slots = &pawn_cache[(uint8_t)(set * PAWN_CACHE_WAYS)];
        if (set_slots[0].key == b->pawn_hash) {
            slot = &set_slots[0];
        } else if (set_slots[1].key == b->pawn_hash) {
            slot = &set_slots[1];
        } else if (set_slots[2].key == b->pawn_hash) {
            slot = &set_slots[2];
        } else if (set_slots[3].key == b->pawn_hash) {
            slot = &set_slots[3];
        } else {
            uint8_t victim = pawn_cache_victim[set];
            slot = &set_slots[victim];
            pawn_cache_victim[set] = (uint8_t)((victim + 1u) & (PAWN_CACHE_WAYS - 1u));
            build_pawn_cache(b, slot);
        }
    }
    pc = slot;
    w_pawns = pc->w_pawns;
    b_pawns = pc->b_pawns;
    pawn_atk = pc->pawn_atk;
    mg += pc->pawn_mg;
    eg += pc->pawn_eg;
    EP_E(build_cy);

    /* ---- Rook file bonuses ---- */
    EP_B();
    {
        /* Iterate white pieces */
        for (i = 0; i < b->piece_count[WHITE]; i++) {
            sq = b->piece_list[WHITE][i];
            type = PIECE_TYPE(b->squares[sq]);
            col = SQ_TO_COL(sq);
#ifndef NO_ROOK_FILES
            if (type == PIECE_ROOK) {
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
            col = SQ_TO_COL(sq);
#ifndef NO_ROOK_FILES
            if (type == PIECE_ROOK) {
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
                    uint8_t occ;
                    while ((occ = b->squares[dest]) == PIECE_NONE) {
                        if (!(pawn_atk[dest] & 2)) mob++;
                        dest += bishop_offsets[j];
                    }
                    /* Stopped on piece or off-board sentinel */
                    if (SQ_VALID(dest) && !IS_WHITE(occ)) {
                        if (!(pawn_atk[dest] & 2)) mob++;
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
                    uint8_t occ;
                    while ((occ = b->squares[dest]) == PIECE_NONE) {
                        if (!(pawn_atk[dest] & 1)) mob++;
                        dest += bishop_offsets[j];
                    }
                    /* Stopped on piece or off-board sentinel */
                    if (SQ_VALID(dest) && !IS_BLACK(occ)) {
                        if (!(pawn_atk[dest] & 1)) mob++;
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

    score = (mg * phase + eg * (PHASE_MAX - phase)) / PHASE_MAX;

    /* Return from side-to-move perspective */
    return (b->side == WHITE) ? score : -score;
}
