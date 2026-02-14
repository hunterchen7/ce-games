/*
 * Chess Engine CE Benchmark
 *
 * Headless profiling harness for the chess engine on TI-84 Plus CE.
 * Uses hardware timers (48 MHz) for cycle-accurate measurements
 * and dbg_printf() for output to the emulator debug console.
 *
 * Build: make
 * Run:   cargo run --release --example debug -- run chessbench/bin/CHBENCH.8xp ../libs/*.8xv
 */

#undef NDEBUG /* ensure dbg_printf is available */
#include <debug.h>
#include <sys/timers.h>
#include <stdint.h>
#include <string.h>

#include "board.h"
#include "movegen.h"
#include "zobrist.h"

/* ========== Minimal FEN Parser ========== */

static void board_set_fen(board_t *b, const char *fen)
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
        if (*p == '/') {
            row++;
            col = 0;
        } else if (*p >= '1' && *p <= '8') {
            col += *p - '0';
        } else {
            int8_t piece = 0;
            switch (*p) {
                case 'P': piece =  1; break;
                case 'N': piece =  2; break;
                case 'B': piece =  3; break;
                case 'R': piece =  4; break;
                case 'Q': piece =  5; break;
                case 'K': piece =  6; break;
                case 'p': piece = -1; break;
                case 'n': piece = -2; break;
                case 'b': piece = -3; break;
                case 'r': piece = -4; break;
                case 'q': piece = -5; break;
                case 'k': piece = -6; break;
            }
            if (piece != 0 && row < 8 && col < 8)
                ui_board[row][col] = piece;
            col++;
        }
        p++;
    }

    if (*p == ' ') p++;

    /* Side to move */
    if (*p == 'b') turn = -1;
    if (*p) p++;
    if (*p == ' ') p++;

    /* Castling */
    if (*p == '-') {
        p++;
    } else {
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
        ep_col = *p - 'a';
        p++;
        if (*p >= '1' && *p <= '8') {
            ep_row = 8 - (*p - '0');
            p++;
        }
    } else if (*p) {
        p++; /* skip '-' or any other character */
    }
    if (*p == ' ') p++;

    /* Halfmove clock */
    halfmove = 0;
    while (*p >= '0' && *p <= '9') {
        halfmove = halfmove * 10 + (*p - '0');
        p++;
    }
    if (*p == ' ') p++;

    /* Fullmove number */
    fullmove = 0;
    while (*p >= '0' && *p <= '9') {
        fullmove = fullmove * 10 + (*p - '0');
        p++;
    }
    if (fullmove == 0) fullmove = 1;

    board_set_from_ui(b, ui_board, turn, castling, ep_row, ep_col,
                      halfmove, fullmove);
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
        if (board_is_legal(b)) {
            nodes += perft(b, depth - 1);
        }
        board_unmake(b, moves[i], &u);
    }

    return nodes;
}

/* ========== Benchmark Positions ========== */

typedef struct {
    const char *name;
    const char *fen;
    uint8_t max_depth;
} bench_pos_t;

static const bench_pos_t positions[] = {
    {
        "startpos",
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
        5
    },
};

#define NUM_POSITIONS (sizeof(positions) / sizeof(positions[0]))

/* ========== Main ========== */

int main(void)
{
    board_t board;
    uint8_t p, d;
    uint32_t nodes, cycles, ms;

    dbg_printf("=== Chess Engine CE Benchmark ===\n\n");

    /* Structure sizes */
    dbg_printf("--- Memory ---\n");
    dbg_printf("sizeof(board_t)  = %u bytes\n", (unsigned int)sizeof(board_t));
    dbg_printf("sizeof(undo_t)   = %u bytes\n", (unsigned int)sizeof(undo_t));
    dbg_printf("sizeof(move_t)   = %u bytes\n", (unsigned int)sizeof(move_t));
    dbg_printf("zobrist tables   = %u bytes\n",
               (unsigned int)(sizeof(zobrist_piece) + sizeof(zobrist_castle) +
                              sizeof(zobrist_ep_file) + sizeof(zobrist_side) +
                              sizeof(lock_piece) + sizeof(lock_castle) +
                              sizeof(lock_ep_file) + sizeof(lock_side)));
    dbg_printf("perft frame      ~%u bytes (moves[%u] + undo + locals)\n",
               (unsigned int)(sizeof(move_t) * MAX_MOVES + sizeof(undo_t) + 32),
               (unsigned int)MAX_MOVES);
    dbg_printf("\n");

    /* Initialize engine */
    board_init(&board);

    /* Enable timer 1: CPU clock (48 MHz), no interrupt, count up */
    timer_Enable(1, TIMER_CPU, TIMER_NOINT, TIMER_UP);

    /* Perft benchmarks */
    for (p = 0; p < NUM_POSITIONS; p++) {
        dbg_printf("--- %s ---\n", positions[p].name);

        for (d = 1; d <= positions[p].max_depth; d++) {
            board_set_fen(&board, positions[p].fen);

            timer_Set(1, 0);
            nodes = perft(&board, d);
            cycles = timer_GetSafe(1, TIMER_UP);
            ms = cycles / 48000UL;

            dbg_printf("  depth %u: %lu nodes  %lu cycles  (%lu ms",
                       (unsigned int)d,
                       (unsigned long)nodes,
                       (unsigned long)cycles,
                       (unsigned long)ms);
            if (ms > 0) {
                dbg_printf(", %lu knps", (unsigned long)(nodes / ms));
            }
            dbg_printf(")\n");
        }
        dbg_printf("\n");
    }

    /* Per-operation timing */
    dbg_printf("--- Per-Operation (startpos) ---\n");

    /* Single generate_moves call */
    {
        move_t moves[MAX_MOVES];
        uint8_t count;

        board_startpos(&board);

        timer_Set(1, 0);
        count = generate_moves(&board, moves, GEN_ALL);
        cycles = timer_GetSafe(1, TIMER_UP);

        dbg_printf("generate_moves: %u moves  %lu cycles\n",
                   (unsigned int)count, (unsigned long)cycles);
    }

    /* Make + unmake averaged over all initial moves */
    {
        move_t moves[MAX_MOVES];
        undo_t u;
        uint8_t count, i;
        uint32_t total = 0;

        board_startpos(&board);
        count = generate_moves(&board, moves, GEN_ALL);

        for (i = 0; i < count; i++) {
            timer_Set(1, 0);
            board_make(&board, moves[i], &u);
            board_unmake(&board, moves[i], &u);
            total += timer_GetSafe(1, TIMER_UP);
        }

        dbg_printf("make+unmake: %u moves  %lu total cycles  %lu avg/move\n",
                   (unsigned int)count, (unsigned long)total,
                   (unsigned long)(total / count));
    }

    /* Single is_square_attacked call */
    {
        board_startpos(&board);

        timer_Set(1, 0);
        (void)is_square_attacked(&board, SQ_E1, BLACK);
        cycles = timer_GetSafe(1, TIMER_UP);

        dbg_printf("is_square_attacked(e1,BLACK): %lu cycles\n",
                   (unsigned long)cycles);
    }

    dbg_printf("\n=== Benchmark Complete ===\n");

    timer_Disable(1);

    /* Signal termination to emulator */
    *(volatile uint8_t *)0xFB0000 = 0;

    return 0;
}
