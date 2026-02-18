/*
 * Chess Engine CE Benchmark
 *
 * Unified profiling harness for the chess engine on TI-84 Plus CE.
 * Uses hardware timers (48 MHz) for cycle-accurate measurements.
 * Output to both screen (graphx) and emulator debug console (dbg_printf).
 *
 * Sections:
 *   1. Memory    — structure sizes
 *   2. Ops       — single-call timing for individual operations
 *   3. Components — iterated benchmarks (movegen, eval, make/unmake)
 *   4. Perft     — node counting at multiple depths
 *   5. Search    — depth-limited search benchmarks
 *
 * Build: cd chess/bench && make
 * Run:   cargo run --release --example debug -- run chess/bench/bin/BENCH.8xp ../libs/[8xv files]
 */

#undef NDEBUG
#include <debug.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <sys/timers.h>
#include <graphx.h>

/* Internal engine headers for component benchmarking */
#include "board.h"
#include "movegen.h"
#include "eval.h"
#include "search.h"
#include "zobrist.h"
#include "tt.h"
#include "engine.h"

/* ========== Time Function (48 MHz hardware timer, overflow-safe) ========== */

/* 32-bit timer at 48 MHz wraps every ~89478 ms (2^32/48000).
   Track overflows so long searches report correct elapsed time. */
static uint32_t bench_time_base;
static uint32_t bench_last_raw;

static uint32_t bench_time_ms(void)
{
    uint32_t raw = timer_GetSafe(1, TIMER_UP) / 48000UL;
    if (raw < bench_last_raw)
        bench_time_base += 89478UL;
    bench_last_raw = raw;
    return bench_time_base + raw;
}

static void bench_time_reset(void)
{
    timer_Set(1, 0);
    bench_time_base = 0;
    bench_last_raw = 0;
}

/* ========== FEN Parser (into board_t directly) ========== */

static void parse_fen_board(const char *fen, board_t *b)
{
    int8_t ui_board[8][8];
    int8_t turn = 1;
    uint8_t castling = 0;
    uint8_t ep_row = 0xFF, ep_col = 0xFF;
    uint8_t halfmove = 0;
    uint16_t fullmove = 1;
    int row = 0, col = 0;
    const char *p = fen;

    memset(ui_board, 0, sizeof(ui_board));

    /* Piece placement */
    while (*p && *p != ' ') {
        if (*p == '/') { row++; col = 0; }
        else if (*p >= '1' && *p <= '8') { col += *p - '0'; }
        else {
            int8_t piece = 0;
            switch (*p) {
                case 'P': piece =  1; break; case 'N': piece =  2; break;
                case 'B': piece =  3; break; case 'R': piece =  4; break;
                case 'Q': piece =  5; break; case 'K': piece =  6; break;
                case 'p': piece = -1; break; case 'n': piece = -2; break;
                case 'b': piece = -3; break; case 'r': piece = -4; break;
                case 'q': piece = -5; break; case 'k': piece = -6; break;
            }
            if (piece != 0 && row < 8 && col < 8)
                ui_board[row][col] = piece;
            col++;
        }
        p++;
    }

    if (*p == ' ') p++;
    if (*p == 'b') turn = -1;
    if (*p) p++;
    if (*p == ' ') p++;

    /* Castling */
    if (*p == '-') { p++; }
    else {
        while (*p && *p != ' ') {
            switch (*p) {
                case 'K': castling |= CASTLE_WK; break;
                case 'Q': castling |= CASTLE_WQ; break;
                case 'k': castling |= CASTLE_BK; break;
                case 'q': castling |= CASTLE_BQ; break;
            }
            p++;
        }
    }
    if (*p == ' ') p++;

    /* En passant */
    if (*p != '-' && *p >= 'a' && *p <= 'h') {
        ep_col = *p - 'a'; p++;
        if (*p >= '1' && *p <= '8') { ep_row = 8 - (*p - '0'); p++; }
    } else if (*p) { p++; }
    if (*p == ' ') p++;

    /* Halfmove clock */
    while (*p >= '0' && *p <= '9') { halfmove = halfmove * 10 + (*p - '0'); p++; }
    if (*p == ' ') p++;

    /* Fullmove number */
    while (*p >= '0' && *p <= '9') { fullmove = fullmove * 10 + (*p - '0'); p++; }
    if (fullmove == 0) fullmove = 1;

    board_set_from_ui(b, ui_board, turn, castling, ep_row, ep_col,
                      halfmove, fullmove);
}

/* ========== Screen/Debug Output ========== */

static int line_y = 2;

static void out(const char *s)
{
    gfx_PrintStringXY(s, 2, line_y);
    line_y += 10;
    dbg_printf("%s\n", s);
    gfx_SwapDraw();
    gfx_Blit(gfx_screen);
}

/* ========== Perft ========== */

static uint32_t perft(board_t *b, uint8_t depth)
{
    move_t moves[MAX_MOVES];
    undo_t u;
    uint32_t nodes = 0;
    uint8_t i, count;

    if (depth == 0) return 1;

    count = generate_moves(b, moves, GEN_ALL);

    for (i = 0; i < count; i++) {
        board_make(b, moves[i], &u);
        if (board_is_legal(b))
            nodes += perft(b, depth - 1);
        board_unmake(b, moves[i], &u);
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
#define NUM_POS   50
#define ITERS     100

/* ========== Main ========== */

int main(void)
{
    board_t b;
    move_t moves[256];
    undo_t undo;
    char buf[50];
    uint32_t cycles, total_cycles, ms, nodes;
    uint8_t nmoves;
    int i, j;
    int16_t eval_result;
    search_limits_t limits;
    search_result_t sr;
    engine_hooks_t hooks;

    gfx_Begin();
    gfx_SetDrawBuffer();
    gfx_ZeroScreen();
    gfx_SetTextFGColor(255);

    /* Enable hardware timer 1: 48 MHz CPU clock for cycle measurements */
    timer_Enable(1, TIMER_CPU, TIMER_NOINT, TIMER_UP);

    out("=== Chess Engine Benchmark ===");

    /* Init engine internals */
    zobrist_init(0x12345678);
    search_init();
    tt_clear();
    hooks.time_ms = bench_time_ms;
    engine_init(&hooks);

#if 0  /* skip sections 1-5 for faster profile run */
    /* ======== 1. Memory Sizes ======== */
    out("-- Memory --");
    sprintf(buf, "board_t: %u B", (unsigned)sizeof(board_t));
    out(buf);
    sprintf(buf, "undo_t:  %u B", (unsigned)sizeof(undo_t));
    out(buf);
    sprintf(buf, "move_t:  %u B", (unsigned)sizeof(move_t));
    out(buf);
    dbg_printf("zobrist tables = %u bytes\n",
               (unsigned)(sizeof(zobrist_piece) + sizeof(zobrist_castle) +
                          sizeof(zobrist_ep_file) + sizeof(zobrist_side) +
                          sizeof(lock_piece) + sizeof(lock_castle) +
                          sizeof(lock_ep_file) + sizeof(lock_side)));
    dbg_printf("perft frame ~%u bytes (moves[%u] + undo + locals)\n",
               (unsigned)(sizeof(move_t) * MAX_MOVES + sizeof(undo_t) + 32),
               (unsigned)MAX_MOVES);

    /* ======== 2. Single-Call Operation Timing ======== */
    out("-- Single Ops (startpos) --");
    parse_fen_board(fens[0], &b);

    /* generate_moves */
    timer_Set(1, 0);
    nmoves = generate_moves(&b, moves, GEN_ALL);
    cycles = timer_GetSafe(1, TIMER_UP);
    sprintf(buf, "movegen: %u moves %lu cy", nmoves, (unsigned long)cycles);
    out(buf);

    /* is_square_attacked */
    timer_Set(1, 0);
    (void)is_square_attacked(&b, SQ_E1, BLACK);
    cycles = timer_GetSafe(1, TIMER_UP);
    sprintf(buf, "attacked(e1): %lu cy", (unsigned long)cycles);
    out(buf);

    /* make+unmake averaged over all startpos moves */
    {
        uint32_t total = 0;
        nmoves = generate_moves(&b, moves, GEN_ALL);
        for (i = 0; i < nmoves; i++) {
            timer_Set(1, 0);
            board_make(&b, moves[i], &undo);
            board_unmake(&b, moves[i], &undo);
            total += timer_GetSafe(1, TIMER_UP);
        }
        sprintf(buf, "mk/unmk: %lu avg cy", (unsigned long)(total / nmoves));
        out(buf);
    }

    /* evaluate */
    timer_Set(1, 0);
    eval_result = evaluate(&b);
    cycles = timer_GetSafe(1, TIMER_UP);
    sprintf(buf, "eval: %d  %lu cy", eval_result, (unsigned long)cycles);
    out(buf);

    /* ======== 3. Iterated Component Benchmarks ======== */
    out("-- Movegen x100 --");
    total_cycles = 0;
    for (i = 0; i < NUM_POS; i++) {
        parse_fen_board(fens[i], &b);
        timer_Set(1, 0);
        for (j = 0; j < ITERS; j++)
            nmoves = generate_moves(&b, moves, 0);
        cycles = timer_GetSafe(1, TIMER_UP);
        total_cycles += cycles;
        sprintf(buf, "P%d: %lu cy/call", i, (unsigned long)(cycles / ITERS));
        out(buf);
    }
    sprintf(buf, "Avg: %lu cy/call",
            (unsigned long)(total_cycles / (NUM_POS * ITERS)));
    out(buf);

    out("-- Eval x100 --");
    total_cycles = 0;
    for (i = 0; i < NUM_POS; i++) {
        parse_fen_board(fens[i], &b);
        timer_Set(1, 0);
        for (j = 0; j < ITERS; j++)
            eval_result = evaluate(&b);
        cycles = timer_GetSafe(1, TIMER_UP);
        total_cycles += cycles;
        sprintf(buf, "P%d: %lu cy/call", i, (unsigned long)(cycles / ITERS));
        out(buf);
    }
    sprintf(buf, "Avg: %lu cy/call",
            (unsigned long)(total_cycles / (NUM_POS * ITERS)));
    out(buf);
    (void)eval_result;

    out("-- Make/Unmake x100 --");
    total_cycles = 0;
    for (i = 0; i < NUM_POS; i++) {
        parse_fen_board(fens[i], &b);
        nmoves = generate_moves(&b, moves, 0);
        if (nmoves == 0) continue;
        timer_Set(1, 0);
        for (j = 0; j < ITERS; j++) {
            board_make(&b, moves[0], &undo);
            board_unmake(&b, moves[0], &undo);
        }
        cycles = timer_GetSafe(1, TIMER_UP);
        total_cycles += cycles;
        sprintf(buf, "P%d: %lu cy/pair", i, (unsigned long)(cycles / ITERS));
        out(buf);
    }
    sprintf(buf, "Avg: %lu cy/pair",
            (unsigned long)(total_cycles / (NUM_POS * ITERS)));
    out(buf);

    /* ======== 4. Perft ======== */
    out("-- Perft (startpos) --");
    for (j = 1; j <= 5; j++) {
        parse_fen_board(fens[0], &b);
        timer_Set(1, 0);
        nodes = perft(&b, (uint8_t)j);
        cycles = timer_GetSafe(1, TIMER_UP);
        ms = cycles / 48000UL;
        sprintf(buf, "d%d: %lu n  %lu ms", j,
                (unsigned long)nodes, (unsigned long)ms);
        out(buf);
        dbg_printf("  perft(%d) = %lu nodes  %lu cycles",
                   j, (unsigned long)nodes, (unsigned long)cycles);
        if (ms > 0)
            dbg_printf("  %lu knps", (unsigned long)(nodes / ms));
        dbg_printf("\n");
    }

    /* ======== 5. Search Benchmarks (startpos, d1-d5) ======== */
    out("-- Search (startpos) --");
    for (j = 1; j <= 5; j++) {
        parse_fen_board(fens[0], &b);
        search_history_clear();
        tt_clear();
        limits.max_depth = (uint8_t)j;
        limits.max_time_ms = 0;
        limits.max_nodes = 0;
        limits.time_fn = NULL;
        timer_Set(1, 0);
        sr = search_go(&b, &limits);
        cycles = timer_GetSafe(1, TIMER_UP);
        ms = cycles / 48000UL;
        sprintf(buf, "d%d: n=%lu %lu ms",
                j, (unsigned long)sr.nodes, (unsigned long)ms);
        out(buf);
        dbg_printf("  search(%d) = %lu nodes  %lu cycles  %lu ms\n",
                   j, (unsigned long)sr.nodes,
                   (unsigned long)cycles, (unsigned long)ms);
    }

    /* Search all 50 positions at d4 */
    out("-- Search d4 (50 pos) --");
    total_cycles = 0;
    for (i = 0; i < NUM_POS; i++) {
        parse_fen_board(fens[i], &b);
        search_history_clear();
        tt_clear();
        limits.max_depth = 4;
        limits.max_time_ms = 0;
        limits.max_nodes = 0;
        limits.time_fn = NULL;
        timer_Set(1, 0);
        sr = search_go(&b, &limits);
        cycles = timer_GetSafe(1, TIMER_UP);
        total_cycles += cycles;
        sprintf(buf, "P%d: %lu cy n=%lu",
                i, (unsigned long)cycles, (unsigned long)sr.nodes);
        out(buf);
    }
    sprintf(buf, "Avg: %lu cy/search",
            (unsigned long)(total_cycles / NUM_POS));
    out(buf);
#endif  /* skip sections 1-5 */

    /* ======== 6. Profiled Search (50 positions x 1000n) ======== */
    {
        const search_profile_t *prof;
        const eval_profile_t *eprof;
        uint64_t total_search_cy = 0;
        uint32_t total_nodes = 0;

        out("-- Profile 1000n (50 pos) --");
        search_profile_reset();
        eval_profile_reset();
        for (i = 0; i < NUM_POS; i++) {
            parse_fen_board(fens[i], &b);
            search_history_clear();
            tt_clear();
            limits.max_depth = 0;
            limits.max_time_ms = 0;
            limits.max_nodes = 1000;
            limits.time_fn = NULL;
            timer_Set(1, 0);
            sr = search_go(&b, &limits);
            cycles = timer_GetSafe(1, TIMER_UP);
            total_search_cy += cycles;
            total_nodes += sr.nodes;
            sprintf(buf, "P%d: n=%lu %lums", i,
                    (unsigned long)sr.nodes, (unsigned long)(cycles / 48000UL));
            out(buf);
        }
        prof = search_profile_get();

        dbg_printf("\n=== SEARCH PROFILE (50 pos x 1000 nodes) ===\n");
        dbg_printf("nodes:       %lu\n", (unsigned long)total_nodes);
        dbg_printf("total_cy:    %llu\n", (unsigned long long)total_search_cy);
        dbg_printf("cy/node:     %llu\n", total_nodes ? (unsigned long long)(total_search_cy / total_nodes) : 0ULL);
        dbg_printf("\n");
        dbg_printf("%-14s %10s %8s %8s %5s\n", "Category", "Cycles", "Cnt", "Cy/call", "Pct");
        dbg_printf("%-14s %10s %8s %8s %5s\n", "--------------", "----------", "--------", "--------", "-----");

#define PROF_ROW(label, cy_field, cnt_field) do { \
    uint32_t _cy = prof->cy_field; \
    uint32_t _cn = prof->cnt_field; \
    uint32_t _pc = _cn ? (_cy / _cn) : 0; \
    uint32_t _pct = total_search_cy ? (uint32_t)(((uint64_t)_cy * 100) / total_search_cy) : 0; \
    dbg_printf("%-14s %10lu %8lu %8lu %4lu%%\n", \
               label, (unsigned long)_cy, (unsigned long)_cn, (unsigned long)_pc, (unsigned long)_pct); \
    sprintf(buf, "%s: %lu%% (%lu cy)", label, (unsigned long)_pct, (unsigned long)_cy); \
    out(buf); \
} while(0)

        PROF_ROW("eval",       eval_cy,       eval_cnt);
        PROF_ROW("movegen",    movegen_cy,     movegen_cnt);
        PROF_ROW("make/unmake",make_unmake_cy, make_cnt);
        PROF_ROW("is_legal",   is_legal_cy,    legal_cnt);
        {
            /* legal_info: called once per non-leaf node; use total_nodes for cnt */
            uint32_t _cy = prof->legal_info_cy;
            uint32_t _pc = total_nodes ? (_cy / total_nodes) : 0;
            uint32_t _pct = total_search_cy ? (uint32_t)(((uint64_t)_cy * 100) / total_search_cy) : 0;
            dbg_printf("%-14s %10lu %8lu %8lu %4lu%%\n",
                       "legal_info", (unsigned long)_cy, (unsigned long)total_nodes,
                       (unsigned long)_pc, (unsigned long)_pct);
            sprintf(buf, "legal_info: %lu%% (%lu cy)", (unsigned long)_pct, (unsigned long)_cy);
            out(buf);
        }
        PROF_ROW("moveorder",  moveorder_cy,   movegen_cnt);
        PROF_ROW("tt",         tt_cy,          tt_cnt);
#undef PROF_ROW

        {
            uint64_t accounted = (uint64_t)prof->eval_cy + prof->movegen_cy +
                prof->make_unmake_cy + prof->is_legal_cy +
                prof->legal_info_cy + prof->moveorder_cy + prof->tt_cy;
            int64_t overhead = (int64_t)total_search_cy - (int64_t)accounted;
            int32_t pct = total_search_cy ? (int32_t)((overhead * 100) / (int64_t)total_search_cy) : 0;
            dbg_printf("%-14s %10lld %8s %8s %4ld%%\n",
                       "overhead", (long long)overhead, "-", "-", (long)pct);
            sprintf(buf, "overhead: %ld%% (%lld cy)", (long)pct, (long long)overhead);
            out(buf);
            sprintf(buf, "total: %llu cy, %lu nodes",
                    (unsigned long long)total_search_cy, (unsigned long)total_nodes);
            out(buf);
        }

        /* ---- Eval Sub-Profile ---- */
        eprof = eval_profile_get();
        dbg_printf("\n=== EVAL SUB-PROFILE ===\n");
        dbg_printf("eval calls:  %lu\n", (unsigned long)eprof->eval_count);
        dbg_printf("total eval:  %lu cy\n", (unsigned long)prof->eval_cy);
        if (eprof->eval_count > 0) {
            dbg_printf("cy/eval:     %lu\n",
                       (unsigned long)(prof->eval_cy / eprof->eval_count));
        }
        dbg_printf("\n");
        dbg_printf("%-14s %10s %8s %5s\n", "Section", "Cycles", "Cy/call", "Pct");
        dbg_printf("%-14s %10s %8s %5s\n", "--------------", "----------", "--------", "-----");

#define EPROF_ROW(label, field) do { \
    uint32_t _cy = eprof->field; \
    uint32_t _pc = eprof->eval_count ? (_cy / eprof->eval_count) : 0; \
    uint32_t _pct = prof->eval_cy ? (uint32_t)(((uint64_t)_cy * 100) / prof->eval_cy) : 0; \
    dbg_printf("%-14s %10lu %8lu %4lu%%\n", \
               label, (unsigned long)_cy, (unsigned long)_pc, (unsigned long)_pct); \
    sprintf(buf, "  %s: %lu%% %lu cy/c", label, (unsigned long)_pct, (unsigned long)_pc); \
    out(buf); \
} while(0)

        out("-- Eval Breakdown --");
        EPROF_ROW("build_pawns", build_cy);
        EPROF_ROW("pieces",      pieces_cy);
        EPROF_ROW("mobility",    mobility_cy);
        EPROF_ROW("shield",      shield_cy);
        {
            uint32_t eacc = eprof->build_cy + eprof->pieces_cy +
                            eprof->mobility_cy + eprof->shield_cy;
            uint32_t eov = prof->eval_cy > eacc ? prof->eval_cy - eacc : 0;
            uint32_t epct = prof->eval_cy ? (uint32_t)(((uint64_t)eov * 100) / prof->eval_cy) : 0;
            dbg_printf("%-14s %10lu %8s %4lu%%\n",
                       "other", (unsigned long)eov, "-", (unsigned long)epct);
            sprintf(buf, "  other: %lu%%", (unsigned long)epct);
            out(buf);
        }
#undef EPROF_ROW
    }
    /* ======== 7. Timed Search (50 positions x 5s, 10s) ======== */
    {
        static const uint32_t time_limits[] = { 5000, 10000 };
        uint32_t total_nodes;
        int t;
        for (t = 0; t < 2; t++) {
            sprintf(buf, "-- Search %lus (50 pos) --",
                    (unsigned long)(time_limits[t] / 1000));
            out(buf);
            total_nodes = 0;
            for (i = 0; i < NUM_POS; i++) {
                parse_fen_board(fens[i], &b);
                search_history_clear();
                tt_clear();
                limits.max_depth = 15;
                limits.max_time_ms = time_limits[t];
                limits.max_nodes = 0;
                limits.time_fn = bench_time_ms;
                bench_time_reset();
                sr = search_go(&b, &limits);
                ms = timer_GetSafe(1, TIMER_UP) / 48000UL;
                total_nodes += sr.nodes;
                sprintf(buf, "P%d: n=%lu d=%u %lums",
                        i, (unsigned long)sr.nodes,
                        (unsigned)sr.depth, (unsigned long)ms);
                out(buf);
                dbg_printf("P%d: nodes=%lu depth=%u ms=%lu\n",
                           i, (unsigned long)sr.nodes,
                           (unsigned)sr.depth, (unsigned long)ms);
            }
            sprintf(buf, "Total: %lu nodes", (unsigned long)total_nodes);
            out(buf);
            dbg_printf("Total: %lu nodes\n", (unsigned long)total_nodes);
        }
    }

    /* ======== 8. Node-Time Benchmark (50 positions x 5 node limits) ======== */
    {
        static const uint32_t node_limits[] = { 2000, 4000, 6000, 8000, 10000 };
        #define NUM_NLIMITS 5
        uint32_t times[NUM_NLIMITS];
        uint32_t totals[NUM_NLIMITS];
        int n;

        search_profile_set_active(0);  /* disable profiling timer reads */
        out("-- Node-Time Bench --");
        dbg_printf("\n=== NODE-TIME BENCHMARK (50 pos) ===\n");
        dbg_printf("| Pos | 2000n ms | 4000n ms | 6000n ms | 8000n ms | 10000n ms |\n");
        dbg_printf("|-----|----------|----------|----------|----------|-----------|\n");

        for (n = 0; n < NUM_NLIMITS; n++)
            totals[n] = 0;

        for (i = 0; i < NUM_POS; i++) {
            for (n = 0; n < NUM_NLIMITS; n++) {
                parse_fen_board(fens[i], &b);
                search_history_clear();
                tt_clear();
                limits.max_depth = 15;
                limits.max_time_ms = 0;
                limits.max_nodes = node_limits[n];
                limits.time_fn = NULL;
                timer_Set(1, 0);
                sr = search_go(&b, &limits);
                times[n] = timer_GetSafe(1, TIMER_UP) / 48000UL;
                totals[n] += times[n];
            }
            dbg_printf("| P%-2d | %8lu | %8lu | %8lu | %8lu | %9lu |\n",
                       i,
                       (unsigned long)times[0], (unsigned long)times[1],
                       (unsigned long)times[2], (unsigned long)times[3],
                       (unsigned long)times[4]);
            sprintf(buf, "P%d: %lu %lu %lu %lu %lu",
                    i,
                    (unsigned long)times[0], (unsigned long)times[1],
                    (unsigned long)times[2], (unsigned long)times[3],
                    (unsigned long)times[4]);
            out(buf);
        }

        dbg_printf("| Avg | %8lu | %8lu | %8lu | %8lu | %9lu |\n",
                   (unsigned long)(totals[0] / NUM_POS),
                   (unsigned long)(totals[1] / NUM_POS),
                   (unsigned long)(totals[2] / NUM_POS),
                   (unsigned long)(totals[3] / NUM_POS),
                   (unsigned long)(totals[4] / NUM_POS));
        sprintf(buf, "Avg: %lu %lu %lu %lu %lu",
                (unsigned long)(totals[0] / NUM_POS),
                (unsigned long)(totals[1] / NUM_POS),
                (unsigned long)(totals[2] / NUM_POS),
                (unsigned long)(totals[3] / NUM_POS),
                (unsigned long)(totals[4] / NUM_POS));
        out(buf);
        #undef NUM_NLIMITS
        search_profile_set_active(1);  /* re-enable profiling */
    }

    out("=== Done ===");

    timer_Disable(1);

    /* Signal termination to emulator */
    *(volatile uint8_t *)0xFB0000 = 0;

    for (;;) ;

    return 0;
}
