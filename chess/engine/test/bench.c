/*
 * bench.c — Desktop benchmark for chess engine
 *
 * Measures cycle counts (via clock_gettime) and wall time for:
 *   1. Movegen x1000 per position
 *   2. Eval x1000 per position
 *   3. Make/Unmake x1000 per position
 *   4. is_square_attacked x1000
 *   5. Perft depths 1-5
 *   6. Search depths 1-5
 *
 * Build: make bench  (from chess/engine/)
 * Run:   ./build/bench
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "../src/board.h"
#include "../src/movegen.h"
#include "../src/eval.h"
#include "../src/search.h"
#include "../src/zobrist.h"
#include "../src/tt.h"
#include "../src/engine.h"

/* ========== High-Resolution Timer ========== */

#ifdef __APPLE__
#include <mach/mach_time.h>
static mach_timebase_info_data_t tb_info;
static uint64_t timer_ns(void)
{
    return mach_absolute_time() * tb_info.numer / tb_info.denom;
}
static void timer_init(void)
{
    mach_timebase_info(&tb_info);
}
#else
static uint64_t timer_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}
static void timer_init(void) {}
#endif

static uint32_t bench_time_ms(void)
{
    return (uint32_t)(timer_ns() / 1000000ULL);
}

/* ========== FEN Parser (copied from perft.c) ========== */

static void board_set_fen(board_t *b, const char *fen)
{
    int r = 0, c = 0;
    uint8_t castling = 0;
    uint8_t ep_sq = SQ_NONE;
    uint8_t halfmove = 0;
    uint16_t fullmove = 1;
    uint8_t side;
    const char *p = fen;

    board_init(b);

    while (*p && *p != ' ') {
        if (*p == '/') { r++; c = 0; }
        else if (*p >= '1' && *p <= '8') { c += *p - '0'; }
        else {
            uint8_t piece = PIECE_NONE;
            uint8_t color = (*p >= 'a') ? COLOR_BLACK : COLOR_WHITE;
            char ch = (*p >= 'a') ? *p - 32 : *p;
            switch (ch) {
                case 'P': piece = MAKE_PIECE(color, PIECE_PAWN);   break;
                case 'N': piece = MAKE_PIECE(color, PIECE_KNIGHT); break;
                case 'B': piece = MAKE_PIECE(color, PIECE_BISHOP); break;
                case 'R': piece = MAKE_PIECE(color, PIECE_ROOK);   break;
                case 'Q': piece = MAKE_PIECE(color, PIECE_QUEEN);  break;
                case 'K': piece = MAKE_PIECE(color, PIECE_KING);   break;
            }
            if (piece != PIECE_NONE) {
                uint8_t sq = RC_TO_SQ(r, c);
                uint8_t s = IS_BLACK(piece) ? BLACK : WHITE;
                uint8_t idx = b->piece_count[s];
                b->squares[sq] = piece;
                if (PIECE_TYPE(piece) == PIECE_KING) b->king_sq[s] = sq;
                b->piece_list[s][idx] = sq;
                b->piece_count[s] = idx + 1;
            }
            c++;
        }
        p++;
    }

    if (*p == ' ') p++;
    side = (*p == 'b') ? BLACK : WHITE;
    b->side = side;
    p++;

    if (*p == ' ') p++;
    while (*p && *p != ' ') {
        switch (*p) {
            case 'K': castling |= CASTLE_WK; break;
            case 'Q': castling |= CASTLE_WQ; break;
            case 'k': castling |= CASTLE_BK; break;
            case 'q': castling |= CASTLE_BQ; break;
        }
        p++;
    }
    b->castling = castling;

    if (*p == ' ') p++;
    if (*p != '-' && *p) {
        uint8_t file = *p - 'a'; p++;
        uint8_t rank = *p - '1'; p++;
        ep_sq = RC_TO_SQ(7 - rank, file);
    } else if (*p) { p++; }
    b->ep_square = ep_sq;

    if (*p == ' ') p++;
    if (*p) halfmove = (uint8_t)strtol(p, (char **)&p, 10);
    b->halfmove = halfmove;

    if (*p == ' ') p++;
    if (*p) fullmove = (uint16_t)strtol(p, (char **)&p, 10);
    b->fullmove = fullmove;

    /* Compute Zobrist hash */
    {
        uint32_t h = 0;
        uint16_t l = 0;
        int sq;
        for (sq = 0; sq < 128; sq++) {
            if (!SQ_VALID(sq)) continue;
            uint8_t piece = b->squares[sq];
            if (piece == PIECE_NONE) continue;
            uint8_t pidx = zobrist_piece_index(piece);
            uint8_t sq64 = SQ_TO_SQ64(sq);
            h ^= zobrist_piece[pidx][sq64];
            l ^= lock_piece[pidx][sq64];
        }
        h ^= zobrist_castle[b->castling];
        l ^= lock_castle[b->castling];
        if (b->ep_square != SQ_NONE) {
            h ^= zobrist_ep_file[SQ_TO_COL(b->ep_square)];
            l ^= lock_ep_file[SQ_TO_COL(b->ep_square)];
        }
        if (b->side == BLACK) { h ^= zobrist_side; l ^= lock_side; }
        b->hash = h;
        b->lock = l;
    }

    /* Compute incremental eval from scratch */
    {
        int sq;
        b->mg[WHITE] = 0; b->mg[BLACK] = 0;
        b->eg[WHITE] = 0; b->eg[BLACK] = 0;
        b->phase = 0;
        for (sq = 0; sq < 128; sq++) {
            uint8_t piece, type, s, idx, sq64, pst_sq;
            if (!SQ_VALID(sq)) continue;
            piece = b->squares[sq];
            if (piece == PIECE_NONE) continue;
            type = PIECE_TYPE(piece);
            s = IS_BLACK(piece) ? BLACK : WHITE;
            idx = EVAL_INDEX(type);
            sq64 = SQ_TO_SQ64(sq);
            pst_sq = (s == WHITE) ? sq64 : PST_FLIP(sq64);
            b->mg[s] += mg_table[idx][pst_sq];
            b->eg[s] += eg_table[idx][pst_sq];
            b->phase += phase_weight[idx];
        }
    }
}

/* ========== Perft ========== */

static uint64_t perft(board_t *b, int depth)
{
    move_t moves[MAX_MOVES];
    undo_t undo;
    uint8_t nmoves, i;
    uint64_t nodes = 0;

    if (depth == 0) return 1;

    nmoves = generate_moves(b, moves, GEN_ALL);
    for (i = 0; i < nmoves; i++) {
        board_make(b, moves[i], &undo);
        if (board_is_legal(b))
            nodes += perft(b, depth - 1);
        board_unmake(b, moves[i], &undo);
    }
    return nodes;
}

/* ========== Benchmark Positions ========== */

/*
 * 50 benchmark positions from well-known chess engine test suites:
 *   - Chessprogramming Wiki Perft Results (positions 0-5)
 *   - TalkChess / Martin Sedlak edge cases (positions 6-18)
 *   - Peterellisjones perft collection (positions 19-24)
 *   - Stockfish benchmark.cpp (positions 25-37)
 *   - Additional TalkChess movegen test positions (positions 38-49)
 */
static const char *fens[] = {
    /* 0: Starting position */
    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
    /* 1: Kiwipete (Peter McKenzie) */
    "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
    /* 2: Sparse endgame */
    "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
    /* 3: Promotion-heavy */
    "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1",
    /* 4: Pawn on d7 promotes */
    "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8",
    /* 5: Steven Edwards symmetrical */
    "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10",
    /* 6: Illegal EP — bishop pins pawn (W) */
    "8/5bk1/8/2Pp4/8/1K6/8/8 w - d6 0 1",
    /* 7: Illegal EP — bishop pins pawn (B) */
    "8/8/1k6/8/2pP4/8/5BK1/8 b - d3 0 1",
    /* 8: EP gives discovered check */
    "8/8/1k6/2b5/2pP4/8/5K2/8 b - d3 0 1",
    /* 9: Short castling gives check */
    "5k2/8/8/8/8/8/8/4K2R w K - 0 1",
    /* 10: Long castling gives check */
    "3k4/8/8/8/8/8/8/R3K3 w Q - 0 1",
    /* 11: Castling rights lost by rook capture */
    "r3k2r/1b4bq/8/8/8/8/7B/R3K2R w KQkq - 0 1",
    /* 12: Castling prevented by attack */
    "r3k2r/8/3Q4/8/8/5q2/8/R3K2R b KQkq - 0 1",
    /* 13: Promote out of check */
    "2K2r2/4P3/8/8/8/8/8/3k4 w - - 0 1",
    /* 14: Discovered check */
    "8/8/1P2K3/8/2n5/1q6/8/5k2 b - - 0 1",
    /* 15: Promote to give check */
    "4k3/1P6/8/8/8/8/K7/8 w - - 0 1",
    /* 16: Under-promote to avoid stalemate */
    "8/P1k5/K7/8/8/8/8/8 w - - 0 1",
    /* 17: Self stalemate */
    "K1k5/8/P7/8/8/8/8/8 w - - 0 1",
    /* 18: Stalemate vs checkmate */
    "8/k1P5/8/1K6/8/8/8/8 w - - 0 1",
    /* 19: Rook vs bishop endgame */
    "r6r/1b2k1bq/8/8/7B/8/8/R3K2R b KQ - 3 2",
    /* 20: EP discovered check (bishop a2) */
    "8/8/8/2k5/2pP4/8/B7/4K3 b - d3 0 3",
    /* 21: Kiwipete variant — Qe6+ */
    "r3k2r/p1pp1pb1/bn2Qnp1/2qPN3/1p2P3/2N5/PPPBBPPP/R3K2R b KQkq - 3 2",
    /* 22: Simple rook vs pawn */
    "2r5/3pk3/8/2P5/8/2K5/8/8 w - - 5 4",
    /* 23: Illegal EP — king exposed to rook */
    "3k4/3p4/8/K1P4r/8/8/8/8 b - - 0 1",
    /* 24: Bishop pin prevents EP */
    "8/8/4k3/8/2p5/8/B2P2K1/8 w - - 0 1",
    /* 25: Stockfish — tactical middlegame */
    "4rrk1/pp1n3p/3q2pQ/2p1pb2/2PP4/2P3N1/P2B2PP/4RRK1 b - - 7 19",
    /* 26: Stockfish — open game */
    "r3r1k1/2p2ppp/p1p1bn2/8/1q2P3/2NPQN2/PPP3PP/R4RK1 b - - 2 15",
    /* 27: Stockfish — Sicilian-type */
    "r1bbk1nr/pp3p1p/2n5/1N4p1/2Np1B2/8/PPP2PPP/2KR1B1R w kq - 0 13",
    /* 28: Stockfish — attacking position */
    "r1bq1rk1/ppp1nppp/4n3/3p3Q/3P4/1BP1B3/PP1N2PP/R4RK1 w - - 1 16",
    /* 29: Stockfish — active rook */
    "4r1k1/r1q2ppp/ppp2n2/4P3/5Rb1/1N1BQ3/PPP3PP/R5K1 w - - 1 17",
    /* 30: Stockfish — closed pawns */
    "3b4/5kp1/1p1p1p1p/pP1PpP1P/P1P1P3/3KN3/8/8 w - - 0 1",
    /* 31: Stockfish — pawn endgame */
    "8/1p3pp1/7p/5P1P/2k3P1/8/2K2P2/8 w - - 0 1",
    /* 32: Stockfish — rook + pawn endgame */
    "8/pp2r1k1/2p1p3/3pP2p/1P1P1P1P/P5KR/8/8 w - - 0 1",
    /* 33: Stockfish — bishop + pawn endgame */
    "8/3p4/p1bk3p/Pp6/1Kp1PpPp/2P2P1P/2P5/5B2 b - - 0 1",
    /* 34: Stockfish — rook endgame passed pawn */
    "5k2/7R/4P2p/5K2/p1r2P1p/8/8/8 b - - 0 1",
    /* 35: Stockfish — minor piece endgame */
    "6k1/6p1/P6p/r1N5/5p2/7P/1b3PP1/4R1K1 w - - 0 1",
    /* 36: Stockfish — queen middlegame */
    "1r3k2/4q3/2Pp3b/3Bp3/2Q2p2/1p1P2P1/1P2KP2/3N4 w - - 0 1",
    /* 37: Stockfish — opposite colored bishops */
    "6k1/4pp1p/3p2p1/P1pPb3/R7/1r2P1PP/3B1P2/6K1 w - - 0 1",
    /* 38: Promotion bug catcher */
    "n1n5/PPPk4/8/8/8/8/4Kppp/5N1N b - - 0 1",
    /* 39: Mirrored position 4 */
    "r2q1rk1/pP1p2pp/Q4n2/bbp1p3/Np6/1B3NBn/pPPP1PPP/R3K2R b KQ - 0 1",
    /* 40: EP after double push (real game) */
    "rnbqkb1r/ppppp1pp/7n/4Pp2/8/8/PPPP1PPP/RNBQKBNR w KQkq f6 0 3",
    /* 41: Complex castling + EP + extra rook */
    "r3k2r/8/8/8/3pPp2/8/8/R3K1RR b KQkq e3 0 1",
    /* 42: Real game endgame with EP */
    "8/7p/p5pb/4k3/P1pPn3/8/P5PP/1rB2RK1 b - d3 0 28",
    /* 43: Deep endgame — Q+R+B */
    "8/3K4/2p5/p2b2r1/5k2/8/8/1q6 b - - 1 67",
    /* 44: Castling with rook threat */
    "1k6/1b6/8/8/7R/8/8/4K2R b K - 0 1",
    /* 45: Castling + pawn structure + pins */
    "r3k2r/p6p/8/B7/1pp1p3/3b4/P6P/R3K2R w KQkq - 0 1",
    /* 46: Pure pawn race */
    "8/p7/8/1P6/K1k3p1/6P1/7P/8 w - - 0 1",
    /* 47: K+P endgame — distant pawns */
    "8/5p2/8/2k3P1/p3K3/8/1P6/8 b - - 0 1",
    /* 48: Realistic middlegame — both castle */
    "r3k2r/pb3p2/5npp/n2p4/1p1PPB2/6P1/P2N1PBP/R3K2R w KQkq - 0 1",
    /* 49: Double check position */
    "8/8/2k5/5q2/5n2/8/5K2/8 b - - 0 1",
};
#define NUM_POS 50
#define ITERS   1000

/* ========== Main ========== */

int main(void)
{
    board_t b;
    move_t moves[MAX_MOVES];
    undo_t undo;
    uint64_t t0, t1, total_ns, nodes;
    uint8_t nmoves;
    int i, j, d;
    int eval_result;
    search_limits_t limits;
    search_result_t sr;
    engine_hooks_t hooks;

    timer_init();

    printf("=== Chess Engine Desktop Benchmark ===\n\n");

    /* Init engine internals */
    zobrist_init(0x12345678);
    search_init();
    tt_clear();
    hooks.time_ms = bench_time_ms;
    engine_init(&hooks);

    /* ======== 1. Memory Sizes ======== */
    printf("-- Memory --\n");
    printf("  board_t: %zu bytes\n", sizeof(board_t));
    printf("  undo_t:  %zu bytes\n", sizeof(undo_t));
    printf("  move_t:  %zu bytes\n", sizeof(move_t));
    printf("\n");

    /* ======== 2. Single-Call Ops (startpos) ======== */
    printf("-- Single Ops (startpos) --\n");
    board_set_fen(&b, fens[0]);

    t0 = timer_ns();
    nmoves = generate_moves(&b, moves, GEN_ALL);
    t1 = timer_ns();
    printf("  movegen: %u moves, %llu ns\n", nmoves, (unsigned long long)(t1 - t0));

    t0 = timer_ns();
    (void)is_square_attacked(&b, SQ_E1, BLACK);
    t1 = timer_ns();
    printf("  attacked(e1): %llu ns\n", (unsigned long long)(t1 - t0));

    {
        uint64_t total = 0;
        nmoves = generate_moves(&b, moves, GEN_ALL);
        for (i = 0; i < nmoves; i++) {
            t0 = timer_ns();
            board_make(&b, moves[i], &undo);
            board_unmake(&b, moves[i], &undo);
            total += timer_ns() - t0;
        }
        printf("  make/unmake avg: %llu ns\n", (unsigned long long)(total / nmoves));
    }

    t0 = timer_ns();
    eval_result = evaluate(&b);
    t1 = timer_ns();
    printf("  eval: %d, %llu ns\n", eval_result, (unsigned long long)(t1 - t0));
    printf("\n");

    /* ======== 3. Iterated Component Benchmarks ======== */
    printf("-- Movegen x%d (%d positions) --\n", ITERS, NUM_POS);
    total_ns = 0;
    for (i = 0; i < NUM_POS; i++) {
        board_set_fen(&b, fens[i]);
        t0 = timer_ns();
        for (j = 0; j < ITERS; j++)
            nmoves = generate_moves(&b, moves, GEN_ALL);
        t1 = timer_ns();
        total_ns += (t1 - t0);
        printf("  P%d: %llu ns/call (%u moves)\n", i,
               (unsigned long long)((t1 - t0) / ITERS), nmoves);
    }
    printf("  Avg: %llu ns/call\n\n",
           (unsigned long long)(total_ns / (NUM_POS * ITERS)));

    printf("-- is_square_attacked x%d --\n", ITERS);
    total_ns = 0;
    for (i = 0; i < NUM_POS; i++) {
        board_set_fen(&b, fens[i]);
        t0 = timer_ns();
        for (j = 0; j < ITERS; j++)
            (void)is_square_attacked(&b, b.king_sq[b.side], b.side ^ 1);
        t1 = timer_ns();
        total_ns += (t1 - t0);
        printf("  P%d: %llu ns/call\n", i,
               (unsigned long long)((t1 - t0) / ITERS));
    }
    printf("  Avg: %llu ns/call\n\n",
           (unsigned long long)(total_ns / (NUM_POS * ITERS)));

    printf("-- Eval x%d --\n", ITERS);
    total_ns = 0;
    for (i = 0; i < NUM_POS; i++) {
        board_set_fen(&b, fens[i]);
        t0 = timer_ns();
        for (j = 0; j < ITERS; j++)
            eval_result = evaluate(&b);
        t1 = timer_ns();
        total_ns += (t1 - t0);
        printf("  P%d: %llu ns/call\n", i,
               (unsigned long long)((t1 - t0) / ITERS));
    }
    printf("  Avg: %llu ns/call\n\n",
           (unsigned long long)(total_ns / (NUM_POS * ITERS)));
    (void)eval_result;

    printf("-- Make/Unmake x%d --\n", ITERS);
    total_ns = 0;
    for (i = 0; i < NUM_POS; i++) {
        board_set_fen(&b, fens[i]);
        nmoves = generate_moves(&b, moves, GEN_ALL);
        if (nmoves == 0) continue;
        t0 = timer_ns();
        for (j = 0; j < ITERS; j++) {
            board_make(&b, moves[0], &undo);
            board_unmake(&b, moves[0], &undo);
        }
        t1 = timer_ns();
        total_ns += (t1 - t0);
        printf("  P%d: %llu ns/pair\n", i,
               (unsigned long long)((t1 - t0) / ITERS));
    }
    printf("  Avg: %llu ns/pair\n\n",
           (unsigned long long)(total_ns / (NUM_POS * ITERS)));

    /* ======== 4. Perft ======== */
    printf("-- Perft (startpos) --\n");
    for (d = 1; d <= 5; d++) {
        board_set_fen(&b, fens[0]);
        t0 = timer_ns();
        nodes = perft(&b, d);
        t1 = timer_ns();
        uint64_t elapsed_ns = t1 - t0;
        double elapsed_ms = elapsed_ns / 1e6;
        double knps = (elapsed_ms > 0) ? nodes / elapsed_ms : 0;
        printf("  depth %d: %llu nodes, %.1f ms (%.0f knps)\n",
               d, (unsigned long long)nodes, elapsed_ms, knps);
    }
    printf("\n");

    /* ======== 5. Search ======== */
    printf("-- Search (startpos, depths 1-5) --\n");
    for (d = 1; d <= 5; d++) {
        board_set_fen(&b, fens[0]);
        search_history_clear();
        tt_clear();
        memset(&limits, 0, sizeof(limits));
        limits.max_depth = d;
        limits.time_fn = NULL;

        t0 = timer_ns();
        sr = search_go(&b, &limits);
        t1 = timer_ns();
        uint64_t elapsed_ns = t1 - t0;
        double elapsed_ms = elapsed_ns / 1e6;
        printf("  depth %d: score=%d, nodes=%lu, %.1f ms\n",
               d, sr.score, (unsigned long)sr.nodes, elapsed_ms);
    }
    printf("\n");

    printf("-- Search (all %d positions, depths 1-5) --\n", NUM_POS);
    for (d = 1; d <= 5; d++) {
        uint64_t total_elapsed = 0;
        uint32_t total_nodes = 0;
        printf("  depth %d:", d);
        for (i = 0; i < NUM_POS; i++) {
            board_set_fen(&b, fens[i]);
            search_history_clear();
            tt_clear();
            memset(&limits, 0, sizeof(limits));
            limits.max_depth = d;
            limits.time_fn = NULL;

            t0 = timer_ns();
            sr = search_go(&b, &limits);
            t1 = timer_ns();
            total_elapsed += (t1 - t0);
            total_nodes += sr.nodes;
        }
        printf(" %u nodes, %.1f ms total\n",
               total_nodes, total_elapsed / 1e6);
    }

    printf("\n=== Done ===\n");
    return 0;
}
