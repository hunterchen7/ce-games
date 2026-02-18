#include "search.h"
#include "eval.h"
#include "movegen.h"
#include "tt.h"
#include "zobrist.h"

/* ========== Search Profiling ========== */

#ifdef SEARCH_PROFILE
#include <sys/timers.h>
#include <string.h>

static search_profile_t _sp;
static volatile uint8_t _prof_active = 1;

static inline uint32_t _prof_now(void) {
    return timer_GetSafe(1, TIMER_UP);
}

void search_profile_set_active(uint8_t on) { _prof_active = on; }

void search_profile_reset(void) {
    memset(&_sp, 0, sizeof(_sp));
}

const search_profile_t *search_profile_get(void) {
    return &_sp;
}

/* PROF_B(): save current timer value into local _pt.
   PROF_E(f): accumulate elapsed time since _pt into _sp.f.
   PROF_C(f): increment counter _sp.f.
   PROF_VARS: declare the local timer variable. */
#define PROF_VARS  uint32_t _pt
#define PROF_B()   do { if (_prof_active) _pt = _prof_now(); } while(0)
#define PROF_E(f)  do { if (_prof_active) _sp.f += _prof_now() - _pt; } while(0)
#define PROF_C(f)  do { if (_prof_active) _sp.f++; } while(0)

#else

#define PROF_VARS
#define PROF_B()
#define PROF_E(f)
#define PROF_C(f)

#endif /* SEARCH_PROFILE */

/* ========== MVV-LVA Table ========== */

/* Indexed by [victim_type-1][attacker_type-1] (0-based: P=0..K=5) */
static const uint8_t mvv_lva[6][6] = {
    /* victim: P    N    B    R    Q    K  (attacker) */
    /* P */ { 15, 14, 13, 12, 11, 10 },
    /* N */ { 25, 24, 23, 22, 21, 20 },
    /* B */ { 25, 24, 23, 22, 21, 20 },
    /* R */ { 35, 34, 33, 32, 31, 30 },
    /* Q */ { 45, 44, 43, 42, 41, 40 },
    /* K */ {  0,  0,  0,  0,  0,  0 },
};

/* ========== Move Ordering Scores ========== */

#define SCORE_TT_MOVE     30000
#define SCORE_CAPTURE_BASE 10000
#define SCORE_KILLER_1     9000
#define SCORE_KILLER_2     8000

/* ========== Search State ========== */

/* Killer moves: 2 per ply */
static move_t killers[MAX_PLY][2];

/* History heuristic: history[side][to_sq88] */
static int16_t history[2][128];

/* Position history for repetition detection */
#define MAX_GAME_PLY 512
static zhash_t pos_history[MAX_GAME_PLY];
static uint16_t pos_history_count;
static uint16_t pos_history_irreversible;

/* Search globals */
static uint32_t search_nodes;
static volatile uint8_t  search_stopped;
static uint32_t search_deadline;
static uint32_t search_max_nodes;
static time_ms_fn search_time_fn;
static move_t   search_best_root_move;
static int      search_eval_noise;     /* max random noise added at root (0 = off) */
static uint32_t search_rng_state;

/* Simple xorshift PRNG — returns value in [-noise, +noise] */
static int search_rand_noise(void)
{
    uint32_t x = search_rng_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    search_rng_state = x;
    if (search_eval_noise == 0) return 0;
    return (int)(x % (2 * (unsigned)search_eval_noise + 1)) - search_eval_noise;
}

/* Quiescence search depth limit */
#define QS_MAX_DEPTH 8

/* Global move pool — avoids ~2KB stack per ply.
   Search is depth-first, so plies share this pool via a stack pointer. */
static scored_move_t move_pool[MOVE_POOL_SIZE];
static uint16_t move_sp;

/* Temp buffer for raw move generation (overwritten at each ply, but
   results are copied into the pool before recursing) */
static move_t raw_moves_buf[MAX_MOVES];

/* ========== Position History ========== */

void search_history_push(zhash_t hash)
{
    if (pos_history_count < MAX_GAME_PLY)
        pos_history[pos_history_count++] = hash;
}

void search_history_pop(void)
{
    if (pos_history_count > 0)
        pos_history_count--;
}

void search_history_clear(void)
{
    pos_history_count = 0;
    pos_history_irreversible = 0;
}

void search_history_set_irreversible(void)
{
    pos_history_irreversible = pos_history_count;
}

static uint8_t is_repetition(zhash_t hash)
{
    int i;
    if (pos_history_count < 3) return 0;
    /* Current position is at pos_history[count-1].
       Same side to move = positions at count-3, count-5, etc. */
    for (i = (int)pos_history_count - 3;
         i >= (int)pos_history_irreversible; i -= 2) {
        if (pos_history[i] == hash) return 1;
    }
    return 0;
}

/* ========== Time Check ========== */

static void check_time(void)
{
    if (search_time_fn && search_deadline) {
        if ((search_nodes & 255) == 0) {
            if (search_time_fn() >= search_deadline)
                search_stopped = 1;
        }
    }
    if (search_max_nodes && search_nodes >= search_max_nodes) {
        search_stopped = 1;
    }
}

/* ========== Initialization ========== */

void search_init(void)
{
    int i, j;
    tt_clear();
    search_history_clear();
    move_sp = 0;
    for (i = 0; i < MAX_PLY; i++) {
        killers[i][0] = MOVE_NONE;
        killers[i][1] = MOVE_NONE;
    }
    for (i = 0; i < 2; i++)
        for (j = 0; j < 128; j++)
            history[i][j] = 0;
}

/* ========== Move Scoring ========== */

static void score_moves(const board_t *b, scored_move_t *moves, uint8_t count,
                        uint8_t ply, move_t tt_move)
{
    uint8_t i;
    for (i = 0; i < count; i++) {
        move_t m = moves[i].move;

        /* TT move gets highest priority.
           Compare from/to and promotion flags only — TT moves don't
           store capture/castle/EP/double-push flags. */
        if (m.from == tt_move.from && m.to == tt_move.to &&
            (m.flags & (FLAG_PROMOTION | FLAG_PROMO_MASK)) ==
            (tt_move.flags & (FLAG_PROMOTION | FLAG_PROMO_MASK))) {
            moves[i].score = SCORE_TT_MOVE;
            continue;
        }

        if (m.flags & FLAG_CAPTURE) {
            /* MVV-LVA for captures */
            uint8_t victim_type = PIECE_TYPE(b->squares[m.to]);
            uint8_t attacker_type = PIECE_TYPE(b->squares[m.from]);

            /* For EP captures, victim is always a pawn */
            if (m.flags & FLAG_EN_PASSANT)
                victim_type = PIECE_PAWN;

            if (victim_type >= PIECE_PAWN && victim_type <= PIECE_KING &&
                attacker_type >= PIECE_PAWN && attacker_type <= PIECE_KING) {
                moves[i].score = SCORE_CAPTURE_BASE +
                    mvv_lva[victim_type - 1][attacker_type - 1];
            } else {
                moves[i].score = SCORE_CAPTURE_BASE;
            }
        } else if (ply < MAX_PLY && MOVE_EQ(m, killers[ply][0])) {
            moves[i].score = SCORE_KILLER_1;
        } else if (ply < MAX_PLY && MOVE_EQ(m, killers[ply][1])) {
            moves[i].score = SCORE_KILLER_2;
        } else {
            /* History heuristic */
            moves[i].score = history[b->side][m.to];
        }

        /* Bonus for promotions */
        if (m.flags & FLAG_PROMOTION) {
            if ((m.flags & FLAG_PROMO_MASK) == FLAG_PROMO_Q)
                moves[i].score += 5000;
            else
                moves[i].score += 1000;
        }
    }
}

/* Capture-only scoring used in quiescence (non-check nodes). */
static void score_capture_moves(const board_t *b, scored_move_t *moves, uint8_t count)
{
    uint8_t i;
    for (i = 0; i < count; i++) {
        move_t m = moves[i].move;
        int16_t score = SCORE_CAPTURE_BASE;

        if (m.flags & FLAG_CAPTURE) {
            uint8_t victim_type = PIECE_TYPE(b->squares[m.to]);
            uint8_t attacker_type = PIECE_TYPE(b->squares[m.from]);

            if (m.flags & FLAG_EN_PASSANT)
                victim_type = PIECE_PAWN;

            if (victim_type >= PIECE_PAWN && victim_type <= PIECE_KING &&
                attacker_type >= PIECE_PAWN && attacker_type <= PIECE_KING) {
                score += mvv_lva[victim_type - 1][attacker_type - 1];
            }
        }

        if (m.flags & FLAG_PROMOTION) {
            if ((m.flags & FLAG_PROMO_MASK) == FLAG_PROMO_Q)
                score += 5000;
            else
                score += 1000;
        }

        moves[i].score = score;
    }
}

/* Selection sort: swap best move to position 'index' */
static void pick_move(scored_move_t *moves, uint8_t count, uint8_t index)
{
    uint8_t best = index;
    int16_t best_score = moves[index].score;
    uint8_t i;
    scored_move_t tmp;

    for (i = index + 1; i < count; i++) {
        if (moves[i].score > best_score) {
            best = i;
            best_score = moves[i].score;
        }
    }

    if (best != index) {
        tmp = moves[index];
        moves[index] = moves[best];
        moves[best] = tmp;
    }
}

/* ========== Update Killer and History ========== */

static void update_killers(uint8_t ply, move_t m)
{
    if (ply >= MAX_PLY) return;
    if (!MOVE_EQ(m, killers[ply][0])) {
        killers[ply][1] = killers[ply][0];
        killers[ply][0] = m;
    }
}

static void update_history(uint8_t side, move_t m, int8_t depth)
{
    int bonus = (int)depth * depth;
    int val = history[side][m.to];
    /* Gravity: prevent overflow, trend toward 0 */
    val += bonus - val * bonus / 16384;
    if (val > 4000) val = 4000;
    if (val < -4000) val = -4000;
    history[side][m.to] = val;
}

/* ========== Legality Fast Path ========== */

typedef struct {
    uint8_t in_check;
    uint8_t num_checkers;
    uint8_t checker_sq[2];
    uint8_t pinned_count;
    uint8_t pinned_sq[8];
} legal_info_t;

static const int8_t atk_knight_offsets[8] = { -33, -31, -18, -14, 14, 18, 31, 33 };
static const int8_t atk_king_offsets[8]   = { -17, -16, -15, -1, 1, 15, 16, 17 };
static const int8_t atk_ray_offsets[8]    = { -17, -16, -15, -1, 1, 15, 16, 17 };

static inline void add_checker(legal_info_t *li, uint8_t sq)
{
    if (li->num_checkers < 2)
        li->checker_sq[li->num_checkers] = sq;
    li->num_checkers++;
    li->in_check = 1;
}

static inline uint8_t is_sq_pinned(const legal_info_t *li, uint8_t sq)
{
    uint8_t i;
    for (i = 0; i < li->pinned_count; i++) {
        if (li->pinned_sq[i] == sq) return 1;
    }
    return 0;
}

/* Compute check/pin information once per node from the king's perspective. */
static void compute_legal_info(const board_t *b, legal_info_t *li)
{
    uint8_t side = b->side;
    uint8_t opp = side ^ 1;
    uint8_t king_sq = b->king_sq[side];
    uint8_t attacker_color = (opp == WHITE) ? COLOR_WHITE : COLOR_BLACK;
    uint8_t i;
    uint8_t target;

    li->in_check = 0;
    li->num_checkers = 0;
    li->pinned_count = 0;

    /* Knight checkers */
    for (i = 0; i < 8; i++) {
        target = king_sq + atk_knight_offsets[i];
        if (SQ_VALID(target)) {
            uint8_t p = b->squares[target];
            if (p != PIECE_NONE &&
                PIECE_COLOR(p) == attacker_color &&
                PIECE_TYPE(p) == PIECE_KNIGHT) {
                add_checker(li, target);
            }
        }
    }

    /* Pawn checkers */
    {
        int8_t pawn_dir = (opp == WHITE) ? 16 : -16;
        uint8_t pawn = MAKE_PIECE(attacker_color, PIECE_PAWN);

        target = king_sq + pawn_dir - 1;
        if (SQ_VALID(target) && b->squares[target] == pawn)
            add_checker(li, target);
        target = king_sq + pawn_dir + 1;
        if (SQ_VALID(target) && b->squares[target] == pawn)
            add_checker(li, target);
    }

    /* Adjacent king checker (illegal positions, but keep robust) */
    for (i = 0; i < 8; i++) {
        target = king_sq + atk_king_offsets[i];
        if (SQ_VALID(target)) {
            uint8_t p = b->squares[target];
            if (p != PIECE_NONE &&
                PIECE_COLOR(p) == attacker_color &&
                PIECE_TYPE(p) == PIECE_KING) {
                add_checker(li, target);
            }
        }
    }

    /* Sliding checkers and pinned friendly pieces */
    for (i = 0; i < 8; i++) {
        int8_t dir = atk_ray_offsets[i];
        uint8_t pinned_sq = SQ_NONE;
        uint8_t is_orth = (dir == -16 || dir == -1 || dir == 1 || dir == 16);
        uint8_t p;

        /* Walk empty squares (sentinel stops at off-board) */
        target = king_sq + dir;
        while (b->squares[target] == PIECE_NONE) target += dir;

        /* First non-empty: real piece or off-board sentinel? */
        if (!SQ_VALID(target)) continue;
        p = b->squares[target];

        if (PIECE_COLOR(p) != attacker_color) {
            /* Friendly piece — could be pinned. Keep walking. */
            pinned_sq = target;
            target += dir;
            while (b->squares[target] == PIECE_NONE) target += dir;
            if (!SQ_VALID(target)) continue;
            p = b->squares[target];
        }

        if (PIECE_COLOR(p) == attacker_color) {
            uint8_t type = PIECE_TYPE(p);
            uint8_t slider = is_orth ?
                (type == PIECE_ROOK || type == PIECE_QUEEN) :
                (type == PIECE_BISHOP || type == PIECE_QUEEN);

            if (slider) {
                if (pinned_sq == SQ_NONE) {
                    add_checker(li, target);
                } else if (li->pinned_count < 8) {
                    li->pinned_sq[li->pinned_count++] = pinned_sq;
                }
            }
        }
    }
}

static inline uint8_t move_needs_legality_check(const board_t *b,
                                                const legal_info_t *li,
                                                move_t m)
{
    uint8_t type;

    if (li->in_check) return 1;
    if (m.flags & FLAG_EN_PASSANT) return 1;

    type = PIECE_TYPE(b->squares[m.from]);
    if (type == PIECE_KING) return 1;
    if (is_sq_pinned(li, m.from)) return 1;

    return 0;
}

static int8_t ray_dir_between(uint8_t from, uint8_t to)
{
    uint8_t i;
    for (i = 0; i < 8; i++) {
        int8_t dir = atk_ray_offsets[i];
        uint8_t sq = from + dir;
        while (SQ_VALID(sq)) {
            if (sq == to) return dir;
            sq += dir;
        }
    }
    return 0;
}

/* Fast reject for in-check nodes: keep only possible evasions. */
static uint8_t is_evasion_candidate(const board_t *b,
                                    const legal_info_t *li,
                                    move_t m)
{
    uint8_t mover_type;
    uint8_t checker_sq;
    uint8_t checker_type;
    int8_t dir;
    uint8_t sq;

    if (!li->in_check) return 1;

    mover_type = PIECE_TYPE(b->squares[m.from]);
    if (mover_type == PIECE_KING) return 1;

    if (li->num_checkers >= 2) return 0; /* only king moves can evade */

    checker_sq = li->checker_sq[0];

    /* Normal capture of checker */
    if (m.to == checker_sq) return 1;

    /* EP can capture the checking pawn while landing elsewhere */
    if (m.flags & FLAG_EN_PASSANT) {
        uint8_t cap_sq = (b->side == WHITE) ? (m.to + 16) : (m.to - 16);
        if (cap_sq == checker_sq) return 1;
    }

    checker_type = PIECE_TYPE(b->squares[checker_sq]);
    if (checker_type != PIECE_BISHOP &&
        checker_type != PIECE_ROOK &&
        checker_type != PIECE_QUEEN) {
        return 0; /* non-slider checks cannot be blocked */
    }

    /* Single slider check: allow blocks on the king-checker ray. */
    dir = ray_dir_between(b->king_sq[b->side], checker_sq);
    if (dir == 0) return 0;

    sq = b->king_sq[b->side] + dir;
    while (sq != checker_sq) {
        if (m.to == sq) return 1;
        sq += dir;
    }

    return 0;
}

/* ========== Quiescence Search ========== */

static int quiescence(board_t *b, int alpha, int beta,
                      uint8_t ply, uint8_t qs_depth)
{
    int stand_pat;
    int score;
    uint8_t in_check;
    uint8_t count, i;
    uint16_t base;
    scored_move_t *moves;
    undo_t undo;
    legal_info_t linfo;
    PROF_VARS;

    if (search_stopped) return 0;
    search_nodes++;
    check_time();
    if (search_stopped) return 0;

    if (ply >= MAX_PLY || qs_depth >= QS_MAX_DEPTH) {
        PROF_B(); stand_pat = evaluate(b); PROF_E(eval_cy); PROF_C(eval_cnt);
        return stand_pat;
    }

    PROF_B();
    compute_legal_info(b, &linfo);
    PROF_E(legal_info_cy);
    in_check = linfo.in_check;

    if (in_check) {
        /* In check: must search all moves (evasions) */
        uint8_t legal_found = 0;
        PROF_B();
        count = generate_moves(b, raw_moves_buf, GEN_ALL);
        PROF_E(movegen_cy); PROF_C(movegen_cnt);

        /* Claim pool space */
        base = move_sp;
        if (base + count > MOVE_POOL_SIZE) return evaluate(b);
        moves = &move_pool[base];
        for (i = 0; i < count; i++) {
            moves[i].move = raw_moves_buf[i];
            moves[i].score = 0;
        }
        move_sp = base + count;
        PROF_B();
        score_moves(b, moves, count, ply, MOVE_NONE);
        PROF_E(moveorder_cy);

        /* Keep caller's alpha bound (do NOT reset to -SCORE_INF) */
        for (i = 0; i < count; i++) {
            PROF_B();
            pick_move(moves, count, i);
            PROF_E(moveorder_cy);
            if (!is_evasion_candidate(b, &linfo, moves[i].move))
                continue;
            PROF_B();
            board_make(b, moves[i].move, &undo);
            PROF_E(make_unmake_cy);
            PROF_B();
            if (!board_is_legal(b)) {
                PROF_E(is_legal_cy); PROF_C(legal_cnt);
                PROF_B();
                board_unmake(b, moves[i].move, &undo);
                PROF_E(make_unmake_cy);
                continue;
            }
            PROF_E(is_legal_cy); PROF_C(legal_cnt);
            PROF_C(make_cnt);
            legal_found = 1;
            score = -quiescence(b, -beta, -alpha, ply + 1, qs_depth + 1);
            PROF_B();
            board_unmake(b, moves[i].move, &undo);
            PROF_E(make_unmake_cy);

            if (search_stopped) { move_sp = base; return 0; }
            if (score > alpha) {
                alpha = score;
                if (alpha >= beta) { move_sp = base; return beta; }
            }
        }

        move_sp = base;

        /* No legal moves while in check = checkmate */
        if (!legal_found) return -SCORE_MATE + ply;

        return alpha;
    }

    /* Not in check: stand pat */
    PROF_B();
    stand_pat = evaluate(b);
    PROF_E(eval_cy); PROF_C(eval_cnt);
    if (stand_pat >= beta) return beta;
    if (stand_pat > alpha) alpha = stand_pat;

    /* Delta pruning: if even capturing a queen can't raise alpha */
    if (stand_pat + 1100 < alpha) return alpha;

    /* Generate captures only */
    PROF_B();
    count = generate_moves(b, raw_moves_buf, GEN_CAPTURES);
    PROF_E(movegen_cy); PROF_C(movegen_cnt);

    /* Claim pool space */
    base = move_sp;
    if (base + count > MOVE_POOL_SIZE) return alpha;
    moves = &move_pool[base];
    for (i = 0; i < count; i++) {
        moves[i].move = raw_moves_buf[i];
        moves[i].score = 0;
    }
    move_sp = base + count;
    PROF_B();
    score_capture_moves(b, moves, count);
    PROF_E(moveorder_cy);

    for (i = 0; i < count; i++) {
        uint8_t need_legality_check;
        PROF_B();
        pick_move(moves, count, i);
        PROF_E(moveorder_cy);
        need_legality_check = move_needs_legality_check(b, &linfo, moves[i].move);
        PROF_B();
        board_make(b, moves[i].move, &undo);
        PROF_E(make_unmake_cy);
        if (need_legality_check) {
            PROF_B();
            if (!board_is_legal(b)) {
                PROF_E(is_legal_cy); PROF_C(legal_cnt);
                PROF_B();
                board_unmake(b, moves[i].move, &undo);
                PROF_E(make_unmake_cy);
                continue;
            }
            PROF_E(is_legal_cy); PROF_C(legal_cnt);
        }
        PROF_C(make_cnt);
        score = -quiescence(b, -beta, -alpha, ply + 1, qs_depth + 1);
        PROF_B();
        board_unmake(b, moves[i].move, &undo);
        PROF_E(make_unmake_cy);

        if (search_stopped) { move_sp = base; return 0; }
        if (score > alpha) {
            alpha = score;
            if (alpha >= beta) { move_sp = base; return beta; }
        }
    }

    move_sp = base;
    return alpha;
}

/* ========== Negamax with Alpha-Beta ========== */

static int negamax(board_t *b, int8_t depth, int alpha, int beta,
                   uint8_t ply, uint8_t do_null, uint8_t ext)
{
    int score, best_score;
    uint8_t in_check;
    uint8_t legal_moves;
    tt_move16_t tt_best_packed;
    move_t tt_move;
    int tt_score;
    int8_t tt_depth;
    uint8_t tt_flag;
    uint16_t base;
    scored_move_t *moves;
    uint8_t count, i, stage, cutoff;
    undo_t undo;
    uint8_t best_flag;
    move_t best_move;
    int8_t new_depth;
    legal_info_t linfo;
    uint8_t can_futility;
    PROF_VARS;

    if (search_stopped) return 0;
    search_nodes++;
    check_time();
    if (search_stopped) return 0;

    /* Draw by repetition or 50-move rule */
    if (ply > 0 && (is_repetition(b->hash) || b->halfmove >= 100))
        return SCORE_DRAW;

    /* Quiescence at leaf */
    if (depth <= 0)
        return quiescence(b, alpha, beta, ply, 0);

    if (ply >= MAX_PLY)
        return evaluate(b);

    /* TT probe */
    tt_move = MOVE_NONE;
    PROF_B();
    if (tt_probe(b->hash, b->lock, &tt_score, &tt_best_packed, &tt_depth, &tt_flag)) {
        /* Adjust mate scores */
        if (tt_score > SCORE_MATE - MAX_PLY)
            tt_score -= ply;
        else if (tt_score < -SCORE_MATE + MAX_PLY)
            tt_score += ply;

        if (tt_depth >= depth) {
            if (tt_flag == TT_EXACT) { PROF_E(tt_cy); PROF_C(tt_cnt); return tt_score; }
            if (tt_flag == TT_BETA && tt_score >= beta) { PROF_E(tt_cy); PROF_C(tt_cnt); return beta; }
            if (tt_flag == TT_ALPHA && tt_score <= alpha) { PROF_E(tt_cy); PROF_C(tt_cnt); return alpha; }
        }
        if (tt_best_packed != TT_MOVE_NONE)
            tt_move = tt_unpack_move(tt_best_packed);
    }
    PROF_E(tt_cy); PROF_C(tt_cnt);

    PROF_B();
    compute_legal_info(b, &linfo);
    PROF_E(legal_info_cy);
    in_check = linfo.in_check;

    /* Check extension — limited to 2 per search path */
    if (in_check && ext < 2) { depth++; ext++; }

    /* Futility pruning setup: at low depths, skip quiet moves
       that have no chance of raising alpha */
    can_futility = 0;
    if (!in_check && depth <= 2 && ply > 0) {
        int static_eval;
        int futility_margin = (depth == 1) ? 200 : 500;
        PROF_B();
        static_eval = evaluate(b);
        PROF_E(eval_cy); PROF_C(eval_cnt);
        if (static_eval + futility_margin <= alpha)
            can_futility = 1;
    }

    /* Null move pruning */
    if (do_null && !in_check && depth >= 3 && ply > 0) {
        /* Verify we have non-pawn material */
        uint8_t has_pieces = 0;
        for (i = 0; i < b->piece_count[b->side]; i++) {
            uint8_t t = PIECE_TYPE(b->squares[b->piece_list[b->side][i]]);
            if (t != PIECE_PAWN && t != PIECE_KING) { has_pieces = 1; break; }
        }

        if (has_pieces) {
            /* Make null move */
            uint8_t old_ep = b->ep_square;
            zhash_t old_hash = b->hash;
            uint16_t old_lock = b->lock;

            /* Flip side, clear EP */
            b->side ^= 1;
            b->hash ^= zobrist_side;
            b->lock ^= lock_side;
            if (old_ep != SQ_NONE) {
                b->hash ^= zobrist_ep_file[SQ_TO_COL(old_ep)];
                b->lock ^= lock_ep_file[SQ_TO_COL(old_ep)];
            }
            b->ep_square = SQ_NONE;

            search_history_push(b->hash);
            score = -negamax(b, depth - 1 - 2, -beta, -beta + 1, ply + 1, 0, ext);
            search_history_pop();

            /* Unmake null move */
            b->side ^= 1;
            b->hash = old_hash;
            b->lock = old_lock;
            b->ep_square = old_ep;

            if (search_stopped) return 0;
            if (score >= beta) return beta;
        }
    }

    best_score = -SCORE_INF;
    best_flag = TT_ALPHA;
    best_move = MOVE_NONE;
    legal_moves = 0;
    cutoff = 0;

    /* Staged generation: captures first, then quiets. */
    for (stage = 0; stage < 2 && !cutoff; stage++) {
        uint8_t mode = (stage == 0) ? GEN_CAPTURES : GEN_QUIETS;

        PROF_B();
        count = generate_moves(b, raw_moves_buf, mode);
        PROF_E(movegen_cy); PROF_C(movegen_cnt);
        base = move_sp;
        if (base + count > MOVE_POOL_SIZE) return evaluate(b);
        moves = &move_pool[base];
        for (i = 0; i < count; i++) {
            moves[i].move = raw_moves_buf[i];
            moves[i].score = 0;
        }
        move_sp = base + count;
        PROF_B();
        score_moves(b, moves, count, ply, tt_move);
        PROF_E(moveorder_cy);

        for (i = 0; i < count; i++) {
            move_t m;
            uint8_t need_legality_check;

            PROF_B();
            pick_move(moves, count, i);
            PROF_E(moveorder_cy);
            m = moves[i].move;
            if (!is_evasion_candidate(b, &linfo, m))
                continue;

            /* Futility pruning: skip quiet moves after at least one
               legal move has been searched */
            if (can_futility && legal_moves > 0 &&
                !(m.flags & (FLAG_CAPTURE | FLAG_PROMOTION)))
                continue;

            need_legality_check = move_needs_legality_check(b, &linfo, m);

            PROF_B();
            board_make(b, m, &undo);
            PROF_E(make_unmake_cy);
            if (need_legality_check) {
                PROF_B();
                if (!board_is_legal(b)) {
                    PROF_E(is_legal_cy); PROF_C(legal_cnt);
                    PROF_B();
                    board_unmake(b, m, &undo);
                    PROF_E(make_unmake_cy);
                    continue;
                }
                PROF_E(is_legal_cy); PROF_C(legal_cnt);
            }
            PROF_C(make_cnt);
            legal_moves++;

            /* Save first legal root move as fallback in case the search
               times out before any move is fully evaluated (can happen
               on slow hardware when check extensions deepen the tree) */
            if (ply == 0 && search_best_root_move.from == SQ_NONE)
                search_best_root_move = m;

            /* Record position for repetition detection */
            search_history_push(b->hash);

            /* PVS + Late move reductions */
            new_depth = depth - 1;
            if (legal_moves == 1) {
                /* First move: full window */
                score = -negamax(b, new_depth, -beta, -alpha, ply + 1, 1, ext);
            } else if (!in_check && legal_moves > 4 && depth >= 3 &&
                       !(m.flags & FLAG_CAPTURE) && !(m.flags & FLAG_PROMOTION)) {
                /* LMR: reduced null-window search */
                score = -negamax(b, new_depth - 1, -alpha - 1, -alpha, ply + 1, 1, ext);
                /* Re-search at full depth + full window if it beats alpha */
                if (score > alpha && !search_stopped)
                    score = -negamax(b, new_depth, -beta, -alpha, ply + 1, 1, ext);
            } else {
                /* PVS: null-window search */
                score = -negamax(b, new_depth, -alpha - 1, -alpha, ply + 1, 1, ext);
                /* Re-search with full window if it beats alpha but not beta */
                if (score > alpha && score < beta && !search_stopped)
                    score = -negamax(b, new_depth, -beta, -alpha, ply + 1, 1, ext);
            }

            search_history_pop();
            PROF_B();
            board_unmake(b, m, &undo);
            PROF_E(make_unmake_cy);

            if (search_stopped) { move_sp = base; return 0; }

            /* Add random noise at root in early opening for variety */
            if (ply == 0 && search_eval_noise && b->fullmove <= 6)
                score += search_rand_noise();

            if (score > best_score) {
                best_score = score;
                best_move = m;

                if (ply == 0)
                    search_best_root_move = m;

                if (score > alpha) {
                    alpha = score;
                    best_flag = TT_EXACT;

                    if (alpha >= beta) {
                        best_flag = TT_BETA;

                        /* Update killers and history for quiet moves */
                        if (!(m.flags & FLAG_CAPTURE)) {
                            update_killers(ply, m);
                            update_history(b->side, m, depth);
                        }
                        cutoff = 1;
                        break;
                    }
                }
            }
        }

        move_sp = base;
    }

    /* Checkmate or stalemate */
    if (legal_moves == 0) {
        if (in_check)
            return -SCORE_MATE + ply;
        else
            return SCORE_DRAW;
    }

    /* TT store */
    {
        int store_score = best_score;
        /* Adjust mate scores for TT storage */
        if (store_score > SCORE_MATE - MAX_PLY)
            store_score += ply;
        else if (store_score < -SCORE_MATE + MAX_PLY)
            store_score -= ply;

        PROF_B();
        tt_store(b->hash, b->lock, store_score,
                 tt_pack_move(best_move), depth, best_flag);
        PROF_E(tt_cy); PROF_C(tt_cnt);
    }

    return best_score;
}

/* ========== Iterative Deepening ========== */

search_result_t search_go(board_t *b, const search_limits_t *limits)
{
    search_result_t result;
    uint8_t max_depth;
    int8_t d;
    int score;

    /* Reset search state */
    search_nodes = 0;
    search_stopped = 0;
    search_best_root_move = MOVE_NONE;
    move_sp = 0;

    /* Time management */
    search_time_fn = limits->time_fn;
    if (limits->max_time_ms && search_time_fn) {
        search_deadline = search_time_fn() + limits->max_time_ms;
    } else {
        search_deadline = 0;
    }
    search_max_nodes = limits->max_nodes;
    search_eval_noise = limits->eval_noise;
    if (search_eval_noise) {
        search_rng_state = b->hash ^ 0xDEAD;
        if (search_time_fn)
            search_rng_state ^= search_time_fn();
    }

    max_depth = limits->max_depth;
    if (max_depth == 0 && limits->max_time_ms == 0 && limits->max_nodes == 0) max_depth = 1;
    if (max_depth == 0) max_depth = MAX_PLY - 1;

    result.best_move = MOVE_NONE;
    result.score = 0;
    result.depth = 0;
    result.nodes = 0;

    for (d = 1; d <= (int8_t)max_depth; d++) {
        int asp_alpha, asp_beta;
        search_best_root_move = MOVE_NONE;

        /* Aspiration windows: narrow search around previous score */
        if (d > 1 && result.best_move.from != SQ_NONE) {
            asp_alpha = result.score - 25;
            asp_beta  = result.score + 25;
        } else {
            asp_alpha = -SCORE_INF;
            asp_beta  =  SCORE_INF;
        }

        score = negamax(b, d, asp_alpha, asp_beta, 0, 1, 0);

        /* Re-search with full window on fail */
        if (!search_stopped && (score <= asp_alpha || score >= asp_beta)) {
            search_best_root_move = MOVE_NONE;
            score = negamax(b, d, -SCORE_INF, SCORE_INF, 0, 1, 0);
        }

        if (search_stopped) {
            /* If we timed out without finding ANY move yet (can happen
               when check extensions deepen the tree on slow hardware),
               extend the deadline and continue the current iteration. */
            if (result.best_move.from == SQ_NONE &&
                search_best_root_move.from == SQ_NONE &&
                search_deadline && search_time_fn) {
                search_deadline = search_time_fn() + 5000;
                search_stopped = 0;
                d--;  /* retry same depth */
                continue;
            }
            break;
        }

        /* Completed iteration — save result */
        if (search_best_root_move.from != SQ_NONE) {
            result.best_move = search_best_root_move;
            result.score = score;
            result.depth = (uint8_t)d;
            result.nodes = search_nodes;
        }
    }

    /* If no completed iteration, try to return any move found */
    if (result.best_move.from == SQ_NONE && search_best_root_move.from != SQ_NONE) {
        result.best_move = search_best_root_move;
        result.score = 0;
        result.depth = 0;
        result.nodes = search_nodes;
    }

    return result;
}
