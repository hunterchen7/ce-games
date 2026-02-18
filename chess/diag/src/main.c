/*
 * Chess Engine Diagnostic — Root Move Analysis
 *
 * Sets up a specific FEN position, searches for 30s,
 * then dumps all root move candidates and their scores.
 */

#undef NDEBUG
#include <debug.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <sys/timers.h>
#include <graphx.h>

#include "board.h"
#include "movegen.h"
#include "eval.h"
#include "search.h"
#include "zobrist.h"
#include "tt.h"

/* ========== Timer (48 MHz) ========== */

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

/* ========== FEN Parser ========== */

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

    if (*p != '-' && *p >= 'a' && *p <= 'h') {
        ep_col = *p - 'a'; p++;
        if (*p >= '1' && *p <= '8') { ep_row = 8 - (*p - '0'); p++; }
    } else if (*p) { p++; }
    if (*p == ' ') p++;

    while (*p >= '0' && *p <= '9') { halfmove = halfmove * 10 + (*p - '0'); p++; }
    if (*p == ' ') p++;

    while (*p >= '0' && *p <= '9') { fullmove = fullmove * 10 + (*p - '0'); p++; }
    if (fullmove == 0) fullmove = 1;

    board_set_from_ui(b, ui_board, turn, castling, ep_row, ep_col,
                      halfmove, fullmove);
}

/* ========== Move to String ========== */

static const char *sq_name(uint8_t sq)
{
    static char buf[3];
    buf[0] = 'a' + SQ_TO_COL(sq);
    buf[1] = '8' - SQ_TO_ROW(sq);
    buf[2] = '\0';
    return buf;
}

static void move_str(move_t m, char *out)
{
    const char *f = sq_name(m.from);
    out[0] = f[0]; out[1] = f[1];
    const char *t = sq_name(m.to);
    out[2] = t[0]; out[3] = t[1];
    if (m.flags & FLAG_PROMOTION) {
        uint8_t promo = m.flags & FLAG_PROMO_MASK;
        if (promo == FLAG_PROMO_Q) out[4] = 'q';
        else if (promo == FLAG_PROMO_R) out[4] = 'r';
        else if (promo == FLAG_PROMO_B) out[4] = 'b';
        else out[4] = 'n';
        out[5] = '\0';
    } else {
        out[4] = '\0';
    }
}

/* ========== Screen Output ========== */

static int line_y = 2;

static void out(const char *s)
{
    gfx_PrintStringXY(s, 2, line_y);
    line_y += 10;
    dbg_printf("%s\n", s);
    gfx_SwapDraw();
    gfx_Blit(gfx_screen);
}

/* ========== Main ========== */

int main(void)
{
    static const char *fen =
        "r3k2r/pp2bppp/2n1pn2/3q4/3P2b1/2N1BN2/PP2BPPP/R2Q1RK1 b kq - 3 10";

    board_t board;
    search_result_t result;
    search_limits_t limits;
    move_t cand_moves[16];
    int16_t cand_scores[16];
    uint8_t cand_count;
    char buf[80], mbuf[6];
    uint8_t i;
    int16_t best_score;
    uint32_t elapsed;

    gfx_Begin();
    gfx_SetDrawBuffer();
    gfx_ZeroScreen();

    timer_Enable(1, TIMER_32K, TIMER_NOINT, TIMER_UP);

    out("=== ROOT MOVE DIAGNOSTIC ===");
    out("FEN: ...3q4/3P2b1/2N1BN2...");
    out("Black to move, 10K nodes");
    out("move_variance=15 to record");
    out("");

    /* Init engine internals */
    zobrist_init(0x12345678);
    tt_clear();
    search_init();

    /* Parse position */
    parse_fen_board(fen, &board);

    /* Search with move_variance=15 to trigger root candidate recording */
    memset(&limits, 0, sizeof(limits));
    limits.max_nodes = 10000;
    limits.move_variance = 15;  /* enables root candidate recording */

    dbg_printf("\n=== ROOT MOVE DIAGNOSTIC ===\n");
    dbg_printf("FEN: %s\n", fen);
    dbg_printf("Searching 10000 nodes with move_variance=15...\n\n");

    bench_time_reset();
    result = search_go(&board, &limits);
    elapsed = bench_time_ms();

    /* Get root candidates */
    search_get_root_candidates(cand_moves, cand_scores, &cand_count);

    /* Print search result */
    move_str(result.best_move, mbuf);
    sprintf(buf, "Best: %s  score=%d  d=%u  n=%lu  %lums",
            mbuf, result.score, result.depth, result.nodes, elapsed);
    out(buf);
    dbg_printf("%s\n", buf);

    /* Find best score among candidates */
    best_score = -30000;
    for (i = 0; i < cand_count; i++)
        if (cand_scores[i] > best_score) best_score = cand_scores[i];

    sprintf(buf, "Root candidates: %u  best_cp=%d", cand_count, best_score);
    out(buf);
    dbg_printf("%s\n\n", buf);

    /* Print ALL root moves with scores */
    dbg_printf("| # | Move  | Score | Delta | Within 15cp? |\n");
    dbg_printf("|---|-------|-------|-------|--------------|\n");

    for (i = 0; i < cand_count; i++) {
        int16_t delta = best_score - cand_scores[i];
        const char *mark = (delta <= 15) ? "YES" : "no";
        move_str(cand_moves[i], mbuf);
        dbg_printf("| %2u | %5s | %5d | %5d | %12s |\n",
                   i, mbuf, cand_scores[i], delta, mark);

        /* Also show on screen (first 15 that fit) */
        if (i < 15) {
            sprintf(buf, "%2u: %s %5d %s%s",
                    i, mbuf, cand_scores[i],
                    (delta <= 15) ? "<=" : "  ",
                    (delta <= 15) ? "15cp" : "");
            out(buf);
        }
    }

    /* Summary: moves within 15cp */
    {
        uint8_t within = 0;
        dbg_printf("\n--- Moves within 15cp of best ---\n");
        for (i = 0; i < cand_count; i++) {
            if (best_score - cand_scores[i] <= 15) {
                move_str(cand_moves[i], mbuf);
                dbg_printf("  %s  score=%d\n", mbuf, cand_scores[i]);
                within++;
            }
        }
        sprintf(buf, "%u moves within 15cp", within);
        out(buf);
        dbg_printf("Total: %s\n", buf);
    }

    out("");
    out("Done. Press any key.");
    dbg_printf("\n=== DONE ===\n");

    /* Spin until program exits */
    for (;;) ;

    gfx_End();
    return 0;
}
