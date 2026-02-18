/* Diagnostic: dump root move candidates for a specific position */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include "../src/board.h"
#include "../src/movegen.h"
#include "../src/eval.h"
#include "../src/search.h"
#include "../src/zobrist.h"
#include "../src/tt.h"

static clock_t search_start;

static uint32_t native_time_ms(void)
{
    return (uint32_t)((clock() - search_start) * 1000 / CLOCKS_PER_SEC);
}

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
    out[4] = '\0';
}

/* Minimal FEN parser */
static void parse_fen(const char *fen, board_t *b)
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
                case 'P': piece=1; break; case 'N': piece=2; break;
                case 'B': piece=3; break; case 'R': piece=4; break;
                case 'Q': piece=5; break; case 'K': piece=6; break;
                case 'p': piece=-1; break; case 'n': piece=-2; break;
                case 'b': piece=-3; break; case 'r': piece=-4; break;
                case 'q': piece=-5; break; case 'k': piece=-6; break;
            }
            if (piece && row < 8 && col < 8) ui_board[row][col] = piece;
            col++;
        }
        p++;
    }
    if (*p == ' ') p++;
    if (*p == 'b') turn = -1;
    if (*p) p++;
    if (*p == ' ') p++;
    if (*p == '-') p++;
    else while (*p && *p != ' ') {
        switch (*p) {
            case 'K': castling |= CASTLE_WK; break;
            case 'Q': castling |= CASTLE_WQ; break;
            case 'k': castling |= CASTLE_BK; break;
            case 'q': castling |= CASTLE_BQ; break;
        }
        p++;
    }
    if (*p == ' ') p++;
    if (*p != '-' && *p >= 'a' && *p <= 'h') {
        ep_col = *p - 'a'; p++;
        if (*p >= '1' && *p <= '8') { ep_row = 8 - (*p - '0'); p++; }
    } else if (*p) p++;
    if (*p == ' ') p++;
    while (*p >= '0' && *p <= '9') { halfmove = halfmove * 10 + (*p - '0'); p++; }
    if (*p == ' ') p++;
    while (*p >= '0' && *p <= '9') { fullmove = fullmove * 10 + (*p - '0'); p++; }
    if (!fullmove) fullmove = 1;
    board_set_from_ui(b, ui_board, turn, castling, ep_row, ep_col, halfmove, fullmove);
}

int main(void)
{
    const char *fen = "r3k2r/pp2bppp/2n1pn2/3q4/3P2b1/2N1BN2/PP2BPPP/R2Q1RK1 b kq - 3 10";
    board_t board;
    search_limits_t limits;
    search_result_t result;
    move_t cand_moves[16];
    int16_t cand_scores[16];
    uint8_t cand_count, i;
    char mbuf[6];
    int16_t best;
    uint32_t time_limits[] = {5000, 10000, 15000, 30000};
    int t;

    zobrist_init(0x12345678);

    parse_fen(fen, &board);
    printf("FEN: %s\n\n", fen);

    for (t = 0; t < 4; t++) {
        tt_clear();
        search_init();

        memset(&limits, 0, sizeof(limits));
        limits.max_time_ms = time_limits[t];
        limits.time_fn = native_time_ms;
        limits.move_variance = 15;

        search_start = clock();
        result = search_go(&board, &limits);
        uint32_t elapsed = native_time_ms();

        search_get_root_candidates(cand_moves, cand_scores, &cand_count);

        move_str(result.best_move, mbuf);
        printf("=== %us search: best=%s score=%d depth=%u nodes=%u elapsed=%ums candidates=%u ===\n",
               time_limits[t] / 1000, mbuf, result.score, result.depth,
               result.nodes, elapsed, cand_count);

        best = -30000;
        for (i = 0; i < cand_count; i++)
            if (cand_scores[i] > best) best = cand_scores[i];

        for (i = 0; i < cand_count; i++) {
            move_str(cand_moves[i], mbuf);
            printf("  %s  score=%d  delta=%d%s\n",
                   mbuf, cand_scores[i], best - cand_scores[i],
                   (best - cand_scores[i] <= 15) ? "  <=15cp" : "");
        }
        printf("\n");
    }

    return 0;
}
