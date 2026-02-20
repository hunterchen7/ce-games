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

    /* ===== Tactical middlegames (positions 50-69) ===== */
    /* WAC (Win At Chess), Nolot, Bratko-Kopec, ERET */

    /* 50: WAC.001 — Qg6, knight sacrifice + queen attack */
    "2rr3k/pp3pp1/1nnqbN1p/3pN3/2pP4/2P3Q1/PPB4P/R4RK1 w - - 0 1",
    /* 51: WAC.003 — Rg3, tactical middlegame */
    "5rk1/1ppb3p/p1pb4/6q1/3P1p1r/2P1R2P/PP1BQ1P1/5RKN w - - 0 1",
    /* 52: WAC.004 — Qxh7+, Greek gift sacrifice */
    "r1bq2rk/pp3pbp/2p1p1pQ/7P/3P4/2PB1N2/PP3PPR/2KR4 w - - 0 1",
    /* 53: WAC.008 — Rf7, rook penetration */
    "r4q1k/p2bR1rp/2p2Q1N/5p2/5p2/2P5/PP3PPP/R5K1 w - - 0 1",
    /* 54: WAC.010 — Rxh7, rook sac to expose king */
    "2br2k1/2q3rn/p2NppQ1/2p1P3/Pp5R/4P3/1P3PPP/3R2K1 w - - 0 1",
    /* 55: WAC.014 — Qxh7+, attack with bishop pair */
    "r2rb1k1/pp1q1p1p/2n1p1p1/2bp4/5P2/PP1BPR1Q/1BPN2PP/R5K1 w - - 0 1",
    /* 56: WAC.021 — Nxf7, knight fork */
    "r1bqk2r/ppp1nppp/4p3/n5N1/2BPp3/P1P5/2P2PPP/R1BQK2R w KQkq - 0 1",
    /* 57: WAC.022 — g4, pawn thrust trapping queen */
    "r3nrk1/2p2p1p/p1p1b1p1/2NpPq2/3R4/P1N1Q3/1PP2PPP/4R1K1 w - - 0 1",
    /* 58: WAC.029 — Nxd6, knight fork winning material */
    "1r3r2/4q1kp/b1pp2p1/5p2/pPn1N3/6P1/P3PPBP/2QRR1K1 w - - 0 1",
    /* 59: Nolot 1 (Kasparov-Karpov 1990) — Nxh6!! */
    "r3qb1k/1b4p1/p2pr2p/3n4/Pnp1N1N1/6RP/1B3PP1/1B1QR1K1 w - - 0 1",
    /* 60: Nolot 2 (Bronstein-Ljubojevic 1973) — Rxc5!! */
    "r4rk1/pp1n1p1p/1nqP2p1/2b1P1B1/4NQ2/1B3P2/PP2K2P/2R5 w - - 0 1",
    /* 61: Nolot 4 (Keres-Kotov 1950) — Nxe6!! */
    "r1b1kb1r/1p1n1ppp/p2ppn2/6BB/2qNP3/2N5/PPP2PPP/R2Q1RK1 w kq - 0 1",
    /* 62: Nolot 5 (Spassky-Petrosian 1969) — e5!! */
    "r2qrb1k/1p1b2p1/p2ppn1p/8/3NP3/1BN5/PPP3QP/1K3RR1 w - - 0 1",
    /* 63: Nolot 9 — Ng5!! piece sac for attack */
    "r4r1k/4bppb/2n1p2p/p1n1P3/1p1p1BNP/3P1NP1/qP2QPB1/2RR2K1 w - - 0 1",
    /* 64: Nolot 10 (Van der Wiel-Ribli 1980) — Rxf7!! */
    "r1b2rk1/1p1nbppp/pq1p4/3B4/P2NP3/2N1p3/1PP3PP/R2Q1R1K w - - 0 15",
    /* 65: BK.03 — closed KID, f5 pawn break */
    "2q1rr1k/3bbnnp/p2p1pp1/2pPp3/PpP1P1P1/1P2BNNP/2BQ1PRK/7R b - - 0 1",
    /* 66: BK.05 — central knight outpost Nd5 */
    "r1b2rk1/2q1b1pp/p2ppn2/1p6/3QP3/1BN1B3/PPP3PP/R4RK1 w - - 0 1",
    /* 67: BK.09 — opposite-side castling, f5 attack */
    "2kr1bnr/pbpq4/2n1pp2/3p3p/3P1P1B/2N2N1Q/PPP3PP/2KR1B1R w - - 0 1",
    /* 68: ERET 1 — tactical relief, piece tension */
    "r1bqk1r1/1p1p1n2/p1n2pN1/2p1b2Q/2P1Pp2/1PN5/PB4PP/R4RK1 w q - 0 1",
    /* 69: ERET 3 — open line attack, queen + rook battery */
    "r1b1r1k1/1pqn1pbp/p2pp1p1/P7/1n1NPP1Q/2NBBR2/1PP3PP/R6K w - - 0 1",

    /* ===== Positional middlegames (positions 70-89) ===== */
    /* SBD (Silent but Deadly), LCT II, Bratko-Kopec, Kaufman, STS */

    /* 70: SBD.039 — QGD structure */
    "r1b2rk1/1pqn1pp1/p2bpn1p/8/3P4/2NB1N2/PPQB1PPP/3R1RK1 w - - 0 1",
    /* 71: LCTII.POS.08 — KID pawn chain, bishop maneuver */
    "r2qrnk1/pp3ppb/3b1n1p/1Pp1p3/2P1P2N/P5P1/1B1NQPBP/R4RK1 w - - 0 1",
    /* 72: SBD.078 — Sicilian middlegame */
    "r1r3k1/1bq2pbp/pp1pp1p1/2n5/P3PP2/R2B4/1PPBQ1PP/3N1R1K w - - 0 1",
    /* 73: SBD.083 — Catalan structure, e4 break */
    "r2q1rk1/pb2bppp/npp1pn2/3pN3/2PP4/1PB3P1/P2NPPBP/R2Q1RK1 w - - 0 1",
    /* 74: SBD.014 — piece pressure, Nd4 */
    "3q1rk1/3rbppp/ppbppn2/1N6/2P1P3/BP6/P1B1QPPP/R3R1K1 w - - 0 1",
    /* 75: SBD.106 — central bind */
    "r3r1k1/1pqn1pbp/p2p2p1/2nP2B1/P1P1P3/2NB3P/5PP1/R2QR1K1 w - - 0 1",
    /* 76: SBD.008 — piece redeployment */
    "2r1r1k1/pbpp1npp/1p1b3q/3P4/4RN1P/1P4P1/PB1Q1PB1/2R3K1 w - - 0 1",
    /* 77: SBD.111 — maneuvering */
    "r4rk1/1bqp1ppp/pp2pn2/4b3/P1P1P3/2N2BP1/1PQB1P1P/2R2RK1 w - - 0 1",
    /* 78: SBD.079 — French structure */
    "r1rn2k1/pp1qppbp/6p1/3pP3/3P4/1P3N1P/PB1Q1PP1/R3R1K1 w - - 0 1",
    /* 79: SBD.095 — IQP position */
    "r2r2k1/p1pnqpp1/4p2p/3b4/3P4/3BPN2/PP3PPP/2RQR1K1 b - - 0 1",
    /* 80: SBD.017 — piece coordination */
    "3r2k1/p1q1npp1/3r1n1p/2p1p3/4P2B/P1P2Q1P/B4PP1/1R2R1K1 w - - 0 1",
    /* 81: SBD.005 — Sicilian middlegame, prophylactic */
    "2brr1k1/ppq2ppp/2pb1n2/8/3NP3/2P2P2/P1Q2BPP/1R1R1BK1 w - - 0 1",
    /* 82: SF bench — minor piece battle */
    "3q2k1/pb3p1p/4pbp1/2r5/PpN2N2/1P2P2P/5PP1/Q2R2K1 b - - 4 26",
    /* 83: LCTII.POS.05 — pawn structure play */
    "2r2rk1/1p1bq3/p3p2p/3pPpp1/1P1Q4/P7/2P2PPP/2R1RBK1 b - - 0 1",
    /* 84: BK.11 — KID maneuvering, f4 prep */
    "2r1nrk1/p2q1ppp/bp1p4/n1pPp3/P1P1P3/2PBB1N1/4QPPP/R4RK1 w - - 0 1",
    /* 85: BK.17 — KID structure, h5 expansion */
    "r2q1rk1/1ppnbppp/p2p1nb1/3Pp3/2P1P1P1/2N2N1P/PPB1QP2/R1B2RK1 b - - 0 1",
    /* 86: KAU.23 — French structure */
    "rn1q1rk1/1b2bppp/1pn1p3/p2pP3/3P4/P2BBN1P/1P1N1PP1/R2Q1RK1 b - - 0 1",
    /* 87: STS 3.003 — knight outpost */
    "1r1q1rk1/1b1n1p1p/p2b1np1/3pN3/3P1P2/P1N5/3BB1PP/1R1Q1RK1 b - - 0 1",
    /* 88: SBD.015 — prophylactic Bh6 */
    "3r1rk1/p1q4p/1pP1ppp1/2n1b1B1/2P5/6P1/P1Q2PBP/1R3RK1 w - - 0 1",
    /* 89: SBD.042 — knight redeployment */
    "r1b2rk1/pp4pp/1q1Nppn1/2n4B/1P3P2/2B2RP1/P6P/R2Q3K b - - 0 1",

    /* ===== Complex endgames (positions 90-99) ===== */
    /* EET (Eigenmann Endgame Test), PET (Peter's Endgame Test) */

    /* 90: EET 075 — R+B vs R+B, minority attack */
    "6k1/p6p/1p1p2p1/2bP4/P1P5/2B3P1/4r2P/1R5K w - - 0 1",
    /* 91: EET 098 — 2R+B vs 2R+B, central pawns */
    "3r2k1/p1R2ppp/1p6/P1b1PP2/3p4/3R2B1/5PKP/1r6 w - - 0 1",
    /* 92: EET 017 — Q+R+B vs Q+2B, positional */
    "6k1/1p2p1bp/p5p1/4pb2/1q6/4Q3/1P2BPPP/2R3K1 w - - 0 1",
    /* 93: PET 044 — 2R+N vs 2R+N, locked center */
    "1r6/Rp2rp2/1Pp2kp1/N1Pp3p/3Pp1nP/4P1P1/R4P2/6K1 w - - 0 1",
    /* 94: PET 028 — R vs R, outside passed pawn */
    "8/4kp2/4p1p1/2p1r3/PpP5/3R4/1P1K1PP1/8 w - - 0 1",
    /* 95: EET 083 — R+B+N vs R+B+N, tactical */
    "8/1B4k1/5pn1/6N1/1P3rb1/P1K4p/3R4/8 w - - 0 1",
    /* 96: EET 062 — R vs R, passed pawn race */
    "2r3k1/6pp/3pp1P1/1pP5/1P6/P4R2/5K2/8 w - - 0 1",
    /* 97: EET 051 — R vs B, pawn structure */
    "8/5p2/3pp2p/p5p1/4Pk2/2p2P1P/P1Kb2P1/1R6 w - - 0 1",
    /* 98: EET 074 — R+B vs R+pawns, advanced pawns */
    "5k2/1p6/1P1p4/1K1p2p1/PB1P2P1/3pR2p/1P2p1pr/8 w - - 0 1",
    /* 99: PET 038 — RB vs RB endgame */
    "4k3/2p1b3/4p1p1/1pp1P3/5PP1/1PBK4/r1P2R2/8 b - - 0 1",
};
#define NUM_POS   100
#define ITERS     100

/* ========== Main ========== */

/* Large locals moved to static to reduce main()'s stack frame.
   TI-OS only provides ~4KB of stack; board_t + moves[] alone were 1.2KB. */
static board_t b;
static move_t moves[256];
static undo_t undo;
static char buf[50];

int main(void)
{
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

    /* ======== 6. Per-Position Profiled Search (50 positions x 1000n) ======== */
    {
        const search_profile_t *prof;
        const eval_profile_t *eprof;
        uint64_t total_search_cy = 0;
        uint32_t total_nodes = 0;

        /* Accumulators for aggregate totals */
        uint64_t agg_eval = 0, agg_movegen = 0, agg_make = 0, agg_legal = 0;
        uint64_t agg_linfo = 0, agg_morder = 0, agg_tt = 0;
        uint64_t agg_null = 0, agg_pool = 0;
        uint64_t agg_build = 0, agg_pieces = 0, agg_mob = 0, agg_shield = 0;
        uint32_t agg_eval_cnt = 0;

        out("-- Profile 1000n (50 pos) --");

        /* Per-position table header */
        dbg_printf("\n=== PER-POSITION PROFILE (1000n each) ===\n");
        dbg_printf("| Pos | Nodes | cy/node | eval%% | mgen%% | make%% | linfo%% | mord%% | legal%% | tt%% | null%% | pool%% | ovhd%% |\n");
        dbg_printf("|-----|-------|---------|-------|-------|-------|--------|-------|--------|-----|-------|-------|-------|\n");

        for (i = 0; i < NUM_POS; i++) {
            uint64_t pos_cy;
            uint32_t pos_nodes;
            uint64_t accounted;
            int32_t ovhd_pct;

            parse_fen_board(fens[i], &b);
            search_history_clear();
            tt_clear();
            limits.max_depth = 0;
            limits.max_time_ms = 0;
            limits.max_nodes = 1000;
            limits.time_fn = NULL;

            /* Reset per-position */
            search_profile_reset();
            eval_profile_reset();

            timer_Set(1, 0);
            sr = search_go(&b, &limits);
            cycles = timer_GetSafe(1, TIMER_UP);

            pos_cy = cycles;
            pos_nodes = sr.nodes;
            total_search_cy += pos_cy;
            total_nodes += pos_nodes;

            prof = search_profile_get();
            eprof = eval_profile_get();

            /* Accumulate for aggregate */
            agg_eval += prof->eval_cy;
            agg_movegen += prof->movegen_cy;
            agg_make += prof->make_unmake_cy;
            agg_legal += prof->is_legal_cy;
            agg_linfo += prof->legal_info_cy;
            agg_morder += prof->moveorder_cy;
            agg_tt += prof->tt_cy;
            agg_null += prof->null_move_cy;
            agg_pool += prof->pool_copy_cy;
            agg_build += eprof->build_cy;
            agg_pieces += eprof->pieces_cy;
            agg_mob += eprof->mobility_cy;
            agg_shield += eprof->shield_cy;
            agg_eval_cnt += eprof->eval_count;

            /* Compute percentages */
            accounted = (uint64_t)prof->eval_cy + prof->movegen_cy +
                prof->make_unmake_cy + prof->is_legal_cy +
                prof->legal_info_cy + prof->moveorder_cy + prof->tt_cy +
                prof->null_move_cy + prof->pool_copy_cy;
            ovhd_pct = pos_cy ? (int32_t)((((int64_t)pos_cy - (int64_t)accounted) * 100) / (int64_t)pos_cy) : 0;

#define PCT(field) (pos_cy ? (uint32_t)(((uint64_t)prof->field * 100) / pos_cy) : 0)
            dbg_printf("| P%-2d | %5lu | %7lu | %4lu%% | %4lu%% | %4lu%% |  %4lu%% | %4lu%% |  %4lu%% | %2lu%% | %4lu%% | %4lu%% | %4ld%% |\n",
                       i,
                       (unsigned long)pos_nodes,
                       pos_nodes ? (unsigned long)(pos_cy / pos_nodes) : 0UL,
                       (unsigned long)PCT(eval_cy),
                       (unsigned long)PCT(movegen_cy),
                       (unsigned long)PCT(make_unmake_cy),
                       (unsigned long)PCT(legal_info_cy),
                       (unsigned long)PCT(moveorder_cy),
                       (unsigned long)PCT(is_legal_cy),
                       (unsigned long)PCT(tt_cy),
                       (unsigned long)PCT(null_move_cy),
                       (unsigned long)PCT(pool_copy_cy),
                       (long)ovhd_pct);
#undef PCT

            sprintf(buf, "P%d: n=%lu %lums cy/n=%lu", i,
                    (unsigned long)pos_nodes, (unsigned long)(pos_cy / 48000UL),
                    pos_nodes ? (unsigned long)(pos_cy / pos_nodes) : 0UL);
            out(buf);
        }

        /* Aggregate summary */
        dbg_printf("\n=== AGGREGATE PROFILE (50 pos x 1000 nodes) ===\n");
        dbg_printf("nodes:       %lu\n", (unsigned long)total_nodes);
        dbg_printf("total_cy:    %llu\n", (unsigned long long)total_search_cy);
        dbg_printf("cy/node:     %llu\n", total_nodes ? (unsigned long long)(total_search_cy / total_nodes) : 0ULL);
        dbg_printf("\n");
        dbg_printf("%-14s %12s %5s\n", "Category", "Cycles", "Pct");
        dbg_printf("%-14s %12s %5s\n", "--------------", "------------", "-----");

#define AGG_ROW(label, val) do { \
    uint32_t _pct = total_search_cy ? (uint32_t)(((val) * 100) / total_search_cy) : 0; \
    dbg_printf("%-14s %12llu %4lu%%\n", label, (unsigned long long)(val), (unsigned long)_pct); \
} while(0)
        AGG_ROW("eval",        agg_eval);
        AGG_ROW("movegen",     agg_movegen);
        AGG_ROW("make/unmake", agg_make);
        AGG_ROW("is_legal",    agg_legal);
        AGG_ROW("legal_info",  agg_linfo);
        AGG_ROW("moveorder",   agg_morder);
        AGG_ROW("tt",          agg_tt);
        AGG_ROW("null_move",   agg_null);
        AGG_ROW("pool_copy",   agg_pool);
        {
            uint64_t accounted = agg_eval + agg_movegen + agg_make +
                                 agg_legal + agg_linfo + agg_morder + agg_tt +
                                 agg_null + agg_pool;
            int64_t overhead = (int64_t)total_search_cy - (int64_t)accounted;
            AGG_ROW("overhead", (uint64_t)(overhead > 0 ? overhead : 0));
        }
#undef AGG_ROW

        /* Eval sub-profile aggregate */
        dbg_printf("\n=== EVAL SUB-PROFILE (aggregate) ===\n");
        dbg_printf("eval calls:  %lu\n", (unsigned long)agg_eval_cnt);
        if (agg_eval_cnt > 0) {
            dbg_printf("cy/eval:     %llu\n", (unsigned long long)(agg_eval / agg_eval_cnt));
            dbg_printf("mob cy/eval: %llu\n", (unsigned long long)(agg_mob / agg_eval_cnt));
            dbg_printf("bld cy/eval: %llu\n", (unsigned long long)(agg_build / agg_eval_cnt));
            dbg_printf("pcs cy/eval: %llu\n", (unsigned long long)(agg_pieces / agg_eval_cnt));
            dbg_printf("shd cy/eval: %llu\n", (unsigned long long)(agg_shield / agg_eval_cnt));
        }
    }
#if 0  /* skip sections 7-8 for profile run (too slow at 100 pos) */
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
#endif  /* skip sections 7-8 */

    out("=== Done ===");

    timer_Disable(1);

    /* Signal termination to emulator */
    *(volatile uint8_t *)0xFB0000 = 0;

    for (;;) ;

    return 0;
}
