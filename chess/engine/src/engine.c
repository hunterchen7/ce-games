#include "engine.h"
#include "board.h"
#include "movegen.h"
#include "search.h"
#include "eval.h"
#include "zobrist.h"
#include "book.h"

/* ========== Internal State ========== */

board_t engine_board;
static engine_hooks_t engine_hooks;
static uint8_t last_was_book;

/* ========== Translation Helpers ========== */

static engine_move_t internal_to_engine_move(move_t m)
{
    engine_move_t em;
    em.from_row = SQ_TO_ROW(m.from);
    em.from_col = SQ_TO_COL(m.from);
    em.to_row = SQ_TO_ROW(m.to);
    em.to_col = SQ_TO_COL(m.to);
    em.flags = m.flags;
    return em;
}

static move_t engine_to_internal_move(engine_move_t em)
{
    move_t m;
    m.from = RC_TO_SQ(em.from_row, em.from_col);
    m.to = RC_TO_SQ(em.to_row, em.to_col);
    m.flags = em.flags;
    return m;
}

/* Check legality: generate moves, make each, verify king safety */
static uint8_t is_legal_internal(board_t *b, move_t m)
{
    undo_t undo;
    uint8_t legal;
    board_make(b, m, &undo);
    legal = board_is_legal(b);
    board_unmake(b, m, &undo);
    return legal;
}

/* ========== Game Status Detection ========== */

/* Check for insufficient material (K vs K, KN vs K, KB vs K, KB vs KB same color) */
static uint8_t is_insufficient_material(const board_t *b)
{
    uint8_t wc = b->piece_count[WHITE];
    uint8_t bc = b->piece_count[BLACK];
    uint8_t i, type;

    /* K vs K */
    if (wc == 1 && bc == 1) return 1;

    /* KN vs K or KB vs K */
    if (wc == 1 && bc == 2) {
        for (i = 0; i < bc; i++) {
            type = PIECE_TYPE(b->squares[b->piece_list[BLACK][i]]);
            if (type == PIECE_KNIGHT || type == PIECE_BISHOP) return 1;
        }
    }
    if (wc == 2 && bc == 1) {
        for (i = 0; i < wc; i++) {
            type = PIECE_TYPE(b->squares[b->piece_list[WHITE][i]]);
            if (type == PIECE_KNIGHT || type == PIECE_BISHOP) return 1;
        }
    }

    return 0;
}

static uint8_t compute_status(board_t *b)
{
    uint8_t in_check;
    uint8_t has_legal;
    move_t moves[MAX_MOVES];
    uint8_t count, i;
    undo_t undo;

    /* 50-move rule */
    if (b->halfmove >= 100)
        return ENGINE_STATUS_DRAW_50;

    /* Insufficient material */
    if (is_insufficient_material(b))
        return ENGINE_STATUS_DRAW_MAT;

    in_check = is_square_attacked(b, b->king_sq[b->side], b->side ^ 1);

    /* Check for any legal move */
    count = generate_moves(b, moves, GEN_ALL);
    has_legal = 0;
    for (i = 0; i < count; i++) {
        board_make(b, moves[i], &undo);
        if (board_is_legal(b)) {
            has_legal = 1;
            board_unmake(b, moves[i], &undo);
            break;
        }
        board_unmake(b, moves[i], &undo);
    }

    if (!has_legal) {
        if (in_check)
            return ENGINE_STATUS_CHECKMATE;
        else
            return ENGINE_STATUS_STALEMATE;
    }

    if (in_check)
        return ENGINE_STATUS_CHECK;

    return ENGINE_STATUS_NORMAL;
}

/* ========== Lifecycle ========== */

void engine_init(const engine_hooks_t *hooks)
{
    if (hooks)
        engine_hooks = *hooks;
    else
        engine_hooks.time_ms = 0;

    search_init();
    board_init(&engine_board);
    book_init();
}

void engine_new_game(void)
{
    search_init();
    board_startpos(&engine_board);
    search_history_push(engine_board.hash);
}

/* ========== Position ========== */

void engine_set_position(const engine_position_t *pos)
{
    board_set_from_ui(&engine_board,
                      pos->board, pos->turn,
                      pos->castling,
                      pos->ep_row, pos->ep_col,
                      pos->halfmove_clock,
                      pos->fullmove_number);
    search_history_clear();
    search_history_push(engine_board.hash);
}

void engine_get_position(engine_position_t *out)
{
    int r, c;
    for (r = 0; r < 8; r++) {
        for (c = 0; c < 8; c++) {
            out->board[r][c] = engine_to_ui_piece(
                engine_board.squares[RC_TO_SQ(r, c)]);
        }
    }
    out->turn = (engine_board.side == WHITE) ? 1 : -1;
    out->castling = engine_board.castling;
    if (engine_board.ep_square != SQ_NONE) {
        out->ep_row = SQ_TO_ROW(engine_board.ep_square);
        out->ep_col = SQ_TO_COL(engine_board.ep_square);
    } else {
        out->ep_row = ENGINE_EP_NONE;
        out->ep_col = ENGINE_EP_NONE;
    }
    out->halfmove_clock = engine_board.halfmove;
    out->fullmove_number = engine_board.fullmove;
}

/* ========== Legal Moves ========== */

uint8_t engine_get_moves_from(uint8_t row, uint8_t col,
                              engine_move_t *out, uint8_t max)
{
    uint8_t sq = RC_TO_SQ(row, col);
    move_t moves[MAX_MOVES];
    uint8_t count, result, i;
    undo_t undo;

    count = generate_moves_from(&engine_board, sq, moves);
    result = 0;

    for (i = 0; i < count && result < max; i++) {
        board_make(&engine_board, moves[i], &undo);
        if (board_is_legal(&engine_board)) {
            out[result++] = internal_to_engine_move(moves[i]);
        }
        board_unmake(&engine_board, moves[i], &undo);
    }

    return result;
}

uint8_t engine_get_all_moves(engine_move_t *out, uint8_t max)
{
    move_t moves[MAX_MOVES];
    uint8_t count, result, i;
    undo_t undo;

    count = generate_moves(&engine_board, moves, GEN_ALL);
    result = 0;

    for (i = 0; i < count && result < max; i++) {
        board_make(&engine_board, moves[i], &undo);
        if (board_is_legal(&engine_board)) {
            out[result++] = internal_to_engine_move(moves[i]);
        }
        board_unmake(&engine_board, moves[i], &undo);
    }

    return result;
}

uint8_t engine_is_legal_move(engine_move_t em)
{
    move_t target = engine_to_internal_move(em);
    move_t moves[MAX_MOVES];
    uint8_t count, i;

    count = generate_moves_from(&engine_board, target.from, moves);

    for (i = 0; i < count; i++) {
        if (moves[i].to == target.to &&
            (moves[i].flags & (FLAG_PROMOTION | FLAG_PROMO_MASK)) ==
            (target.flags & (FLAG_PROMOTION | FLAG_PROMO_MASK))) {
            return is_legal_internal(&engine_board, moves[i]);
        }
    }
    return 0;
}

/* ========== Move Side Effects ========== */

void engine_get_move_effects(engine_move_t em, engine_move_effects_t *fx)
{
    fx->has_rook_move = 0;
    fx->has_ep_capture = 0;

    if (em.flags & ENGINE_FLAG_CASTLE) {
        fx->has_rook_move = 1;
        fx->rook_from_row = em.from_row;
        fx->rook_to_row = em.from_row;
        if (em.to_col > em.from_col) {
            /* Kingside: rook h-file to f-file */
            fx->rook_from_col = 7;
            fx->rook_to_col = 5;
        } else {
            /* Queenside: rook a-file to d-file */
            fx->rook_from_col = 0;
            fx->rook_to_col = 3;
        }
    }

    if (em.flags & ENGINE_FLAG_EN_PASSANT) {
        fx->has_ep_capture = 1;
        fx->ep_capture_row = em.from_row;
        fx->ep_capture_col = em.to_col;
    }
}

/* ========== Making Moves ========== */

uint8_t engine_make_move(engine_move_t em)
{
    move_t target = engine_to_internal_move(em);
    move_t moves[MAX_MOVES];
    uint8_t count, i;
    undo_t undo;

    /* Find the matching generated move (to get correct flags) */
    count = generate_moves_from(&engine_board, target.from, moves);

    for (i = 0; i < count; i++) {
        if (moves[i].to != target.to) continue;
        /* Match promotion type if applicable */
        if ((moves[i].flags & FLAG_PROMOTION) &&
            (moves[i].flags & FLAG_PROMO_MASK) !=
            (target.flags & FLAG_PROMO_MASK))
            continue;

        /* Verify legality */
        board_make(&engine_board, moves[i], &undo);
        if (!board_is_legal(&engine_board)) {
            board_unmake(&engine_board, moves[i], &undo);
            continue;
        }

        /* Move is legal and applied. Update history. */
        if (PIECE_TYPE(undo.moved_piece) == PIECE_PAWN ||
            (undo.flags & FLAG_CAPTURE)) {
            search_history_set_irreversible();
        }
        search_history_push(engine_board.hash);

        return compute_status(&engine_board);
    }

    /* No legal move found — return normal (shouldn't happen with valid input) */
    return ENGINE_STATUS_NORMAL;
}

/* ========== AI ========== */

static uint32_t engine_max_nodes = 0;
static uint8_t  engine_use_book  = 1;
static uint8_t  engine_book_max_ply = 0; /* 0 = unlimited */
static int      engine_eval_noise = 0;
static int      engine_move_variance = 0;

void engine_set_max_nodes(uint32_t n)
{
    engine_max_nodes = n;
}

void engine_set_use_book(uint8_t enabled)
{
    engine_use_book = enabled;
}

void engine_set_book_max_ply(uint8_t ply)
{
    engine_book_max_ply = ply;
}

void engine_set_eval_noise(int noise)
{
    engine_eval_noise = noise;
}

void engine_set_move_variance(int cp)
{
    engine_move_variance = cp;
}

engine_move_t engine_think(uint8_t max_depth, uint32_t max_time_ms)
{
    search_limits_t limits;
    search_result_t result;
    engine_move_t em;
    move_t book_move;

    /* Try the opening book first — instant response */
    if (engine_use_book
        && (!engine_book_max_ply || engine_board.fullmove <= engine_book_max_ply)
        && book_probe(&engine_board, &book_move)) {
        last_was_book = 1;
        return internal_to_engine_move(book_move);
    }
    last_was_book = 0;

    limits.max_depth = max_depth;
    limits.max_time_ms = max_time_ms;
    limits.max_nodes = engine_max_nodes;
    limits.time_fn = engine_hooks.time_ms;
    limits.eval_noise = engine_eval_noise;
    limits.move_variance = engine_move_variance;

    result = search_go(&engine_board, &limits);

    if (result.best_move.from == SQ_NONE) {
        em.from_row = ENGINE_SQ_NONE;
        em.from_col = 0;
        em.to_row = 0;
        em.to_col = 0;
        em.flags = 0;
        return em;
    }

    return internal_to_engine_move(result.best_move);
}

engine_bench_result_t engine_bench(uint8_t max_depth, uint32_t max_time_ms)
{
    search_limits_t limits;
    search_result_t result;
    engine_bench_result_t br;

    limits.max_depth = max_depth;
    limits.max_time_ms = max_time_ms;
    limits.max_nodes = 0; /* no node limit for benchmarking */
    limits.time_fn = engine_hooks.time_ms;
    limits.eval_noise = 0;
    limits.move_variance = 0;

    result = search_go(&engine_board, &limits);
    br.nodes = result.nodes;
    br.depth = result.depth;
    return br;
}

/* ========== Query ========== */

uint8_t engine_get_status(void)
{
    return compute_status(&engine_board);
}

uint8_t engine_in_check(void)
{
    return is_square_attacked(&engine_board,
                              engine_board.king_sq[engine_board.side],
                              engine_board.side ^ 1);
}

/* ========== Book Diagnostics ========== */

void engine_get_book_info(engine_book_info_t *out)
{
    book_get_info(&out->ready, &out->num_segments, &out->total_entries);
}

uint8_t engine_last_move_was_book(void)
{
    return last_was_book;
}

/* ========== Cleanup ========== */

void engine_cleanup(void)
{
    book_close();
}
