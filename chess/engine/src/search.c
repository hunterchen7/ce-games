#include "search.h"
#include "eval.h"
#include "movegen.h"
#include "tt.h"
#include "zobrist.h"

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
static uint32_t pos_history[MAX_GAME_PLY];
static uint16_t pos_history_count;
static uint16_t pos_history_irreversible;

/* Search globals */
static uint32_t search_nodes;
static uint8_t  search_stopped;
static uint32_t search_deadline;
static uint32_t search_max_nodes;
static time_ms_fn search_time_fn;
static move_t   search_best_root_move;

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

void search_history_push(uint32_t hash)
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

static uint8_t is_repetition(uint32_t hash)
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
    if (search_max_nodes && search_nodes >= search_max_nodes) {
        search_stopped = 1;
        return;
    }
    if (search_time_fn && search_deadline) {
        if ((search_nodes & 1023) == 0) {
            if (search_time_fn() >= search_deadline)
                search_stopped = 1;
        }
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
    int16_t bonus = (int16_t)depth * depth;
    int16_t val = history[side][m.to];
    /* Gravity: prevent overflow, trend toward 0 */
    val += bonus - val * bonus / 16384;
    if (val > 4000) val = 4000;
    if (val < -4000) val = -4000;
    history[side][m.to] = val;
}

/* ========== Quiescence Search ========== */

static int16_t quiescence(board_t *b, int16_t alpha, int16_t beta,
                          uint8_t ply, uint8_t qs_depth)
{
    int16_t stand_pat;
    int16_t score;
    uint8_t in_check;
    uint8_t count, i;
    uint16_t base;
    scored_move_t *moves;
    undo_t undo;

    if (search_stopped) return 0;
    search_nodes++;
    check_time();
    if (search_stopped) return 0;

    if (ply >= MAX_PLY || qs_depth >= QS_MAX_DEPTH) return evaluate(b);

    in_check = is_square_attacked(b, b->king_sq[b->side], b->side ^ 1);

    if (in_check) {
        /* In check: must search all moves (evasions) */
        uint8_t legal_found = 0;
        count = generate_moves(b, raw_moves_buf, GEN_ALL);

        /* Claim pool space */
        base = move_sp;
        if (base + count > MOVE_POOL_SIZE) return evaluate(b);
        moves = &move_pool[base];
        for (i = 0; i < count; i++) {
            moves[i].move = raw_moves_buf[i];
            moves[i].score = 0;
        }
        move_sp = base + count;
        score_moves(b, moves, count, ply, MOVE_NONE);

        /* Keep caller's alpha bound (do NOT reset to -SCORE_INF) */
        for (i = 0; i < count; i++) {
            pick_move(moves, count, i);
            board_make(b, moves[i].move, &undo);
            if (!board_is_legal(b)) {
                board_unmake(b, moves[i].move, &undo);
                continue;
            }
            legal_found = 1;
            score = -quiescence(b, -beta, -alpha, ply + 1, qs_depth + 1);
            board_unmake(b, moves[i].move, &undo);

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
    stand_pat = evaluate(b);
    if (stand_pat >= beta) return beta;
    if (stand_pat > alpha) alpha = stand_pat;

    /* Delta pruning: if even capturing a queen can't raise alpha */
    if (stand_pat + 1100 < alpha) return alpha;

    /* Generate captures only */
    count = generate_moves(b, raw_moves_buf, GEN_CAPTURES);

    /* Claim pool space */
    base = move_sp;
    if (base + count > MOVE_POOL_SIZE) return alpha;
    moves = &move_pool[base];
    for (i = 0; i < count; i++) {
        moves[i].move = raw_moves_buf[i];
        moves[i].score = 0;
    }
    move_sp = base + count;
    score_moves(b, moves, count, ply, MOVE_NONE);

    for (i = 0; i < count; i++) {
        pick_move(moves, count, i);
        board_make(b, moves[i].move, &undo);
        if (!board_is_legal(b)) {
            board_unmake(b, moves[i].move, &undo);
            continue;
        }
        score = -quiescence(b, -beta, -alpha, ply + 1, qs_depth + 1);
        board_unmake(b, moves[i].move, &undo);

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

static int16_t negamax(board_t *b, int8_t depth, int16_t alpha, int16_t beta,
                       uint8_t ply, uint8_t do_null)
{
    int16_t score, best_score;
    uint8_t in_check;
    uint8_t legal_moves;
    tt_move16_t tt_best_packed;
    move_t tt_move;
    int16_t tt_score;
    int8_t tt_depth;
    uint8_t tt_flag;
    uint16_t base;
    scored_move_t *moves;
    uint8_t count, i, stage, cutoff;
    undo_t undo;
    uint8_t best_flag;
    move_t best_move;
    int8_t new_depth;

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
    if (tt_probe(b->hash, b->lock, &tt_score, &tt_best_packed, &tt_depth, &tt_flag)) {
        /* Adjust mate scores */
        if (tt_score > SCORE_MATE - MAX_PLY)
            tt_score -= ply;
        else if (tt_score < -SCORE_MATE + MAX_PLY)
            tt_score += ply;

        if (tt_depth >= depth) {
            if (tt_flag == TT_EXACT) return tt_score;
            if (tt_flag == TT_BETA && tt_score >= beta) return beta;
            if (tt_flag == TT_ALPHA && tt_score <= alpha) return alpha;
        }
        if (tt_best_packed != TT_MOVE_NONE)
            tt_move = tt_unpack_move(tt_best_packed);
    }

    in_check = is_square_attacked(b, b->king_sq[b->side], b->side ^ 1);

    /* Check extension */
    if (in_check) depth++;

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
            uint32_t old_hash = b->hash;
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
            score = -negamax(b, depth - 1 - 2, -beta, -beta + 1, ply + 1, 0);
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

        count = generate_moves(b, raw_moves_buf, mode);
        base = move_sp;
        if (base + count > MOVE_POOL_SIZE) return evaluate(b);
        moves = &move_pool[base];
        for (i = 0; i < count; i++) {
            moves[i].move = raw_moves_buf[i];
            moves[i].score = 0;
        }
        move_sp = base + count;
        score_moves(b, moves, count, ply, tt_move);

        for (i = 0; i < count; i++) {
            move_t m;

            pick_move(moves, count, i);
            m = moves[i].move;

            board_make(b, m, &undo);
            if (!board_is_legal(b)) {
                board_unmake(b, m, &undo);
                continue;
            }
            legal_moves++;

            /* Record position for repetition detection */
            search_history_push(b->hash);

            /* Late move reductions */
            new_depth = depth - 1;
            if (!in_check && legal_moves > 4 && depth >= 3 &&
                !(m.flags & FLAG_CAPTURE) && !(m.flags & FLAG_PROMOTION)) {
                new_depth--;
                /* Re-search at full depth if reduced search improves alpha */
                score = -negamax(b, new_depth, -beta, -alpha, ply + 1, 1);
                if (score > alpha && !search_stopped) {
                    new_depth = depth - 1;
                    score = -negamax(b, new_depth, -beta, -alpha, ply + 1, 1);
                }
            } else {
                score = -negamax(b, new_depth, -beta, -alpha, ply + 1, 1);
            }

            search_history_pop();
            board_unmake(b, m, &undo);

            if (search_stopped) { move_sp = base; return 0; }

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
        int16_t store_score = best_score;
        /* Adjust mate scores for TT storage */
        if (store_score > SCORE_MATE - MAX_PLY)
            store_score += ply;
        else if (store_score < -SCORE_MATE + MAX_PLY)
            store_score -= ply;

        tt_store(b->hash, b->lock, store_score,
                 tt_pack_move(best_move), depth, best_flag);
    }

    return best_score;
}

/* ========== Iterative Deepening ========== */

search_result_t search_go(board_t *b, const search_limits_t *limits)
{
    search_result_t result;
    uint8_t max_depth;
    int8_t d;
    int16_t score;

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

    max_depth = limits->max_depth;
    if (max_depth == 0 && limits->max_time_ms == 0) max_depth = 1;
    if (max_depth == 0) max_depth = MAX_PLY - 1;

    result.best_move = MOVE_NONE;
    result.score = 0;
    result.depth = 0;
    result.nodes = 0;

    for (d = 1; d <= (int8_t)max_depth; d++) {
        search_best_root_move = MOVE_NONE;
        score = negamax(b, d, -SCORE_INF, SCORE_INF, 0, 1);

        if (search_stopped) break;

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
