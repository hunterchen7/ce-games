/* Diagnostic: check if Rxa2 is found after Qxa2 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "../src/board.h"
#include "../src/movegen.h"
#include "../src/eval.h"
#include "../src/search.h"
#include "../src/zobrist.h"
#include "../src/tt.h"

static const char *sq_name(uint8_t sq)
{
    static char buf[3];
    buf[0] = 'a' + SQ_TO_COL(sq);
    buf[1] = '8' - SQ_TO_ROW(sq);
    buf[2] = '\0';
    return buf;
}

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
    /* Position AFTER Black played Qd5xa2 */
    const char *fen_after = "r3k2r/pp2bppp/2n1pn2/8/3P2b1/2N1BN2/qP2BPPP/R2Q1RK1 w - - 0 11";
    board_t board;
    move_t moves[256];
    uint8_t count, i;
    int eval_score;

    zobrist_init(0x12345678);
    tt_clear();
    search_init();

    parse_fen(fen_after, &board);

    /* 1. Static eval */
    eval_score = evaluate(&board);
    printf("Static eval (White to move): %d\n\n", eval_score);

    /* 2. What's on a1 and a2? */
    printf("Square a1 (0x70): 0x%02X  (type=%d color=%s)\n",
           board.squares[0x70], PIECE_TYPE(board.squares[0x70]),
           board.squares[0x70] == PIECE_NONE ? "none" :
           IS_WHITE(board.squares[0x70]) ? "white" : "black");
    printf("Square a2 (0x60): 0x%02X  (type=%d color=%s)\n",
           board.squares[0x60], PIECE_TYPE(board.squares[0x60]),
           board.squares[0x60] == PIECE_NONE ? "none" :
           IS_WHITE(board.squares[0x60]) ? "white" : "black");
    printf("Side to move: %s\n\n", board.side == WHITE ? "WHITE" : "BLACK");

    /* 3. Generate ALL moves */
    count = generate_moves(&board, moves, 0 /* GEN_ALL */);
    printf("All moves (%u):\n", count);
    for (i = 0; i < count; i++) {
        const char *f = sq_name(moves[i].from);
        char from[3] = {f[0], f[1], 0};
        const char *t = sq_name(moves[i].to);
        printf("  %s%s%s", from, t, (moves[i].flags & FLAG_CAPTURE) ? "x" : "");
        if (moves[i].from == 0x70 && moves[i].to == 0x60)
            printf("  *** Ra1xa2 FOUND ***");
        printf("\n");
    }

    /* 4. Generate CAPTURES only */
    count = generate_moves(&board, moves, 2 /* GEN_CAPTURES */);
    printf("\nCapture moves (%u):\n", count);
    uint8_t found_rxa2 = 0;
    for (i = 0; i < count; i++) {
        const char *f = sq_name(moves[i].from);
        char from[3] = {f[0], f[1], 0};
        const char *t = sq_name(moves[i].to);
        printf("  %s%s (flags=0x%02X)\n", from, t, moves[i].flags);
        if (moves[i].from == 0x70 && moves[i].to == 0x60) found_rxa2 = 1;
    }
    printf("Rxa2 in captures: %s\n", found_rxa2 ? "YES" : "NO");

    /* 5. Depth-1 search from White's side */
    {
        search_limits_t limits;
        search_result_t result;
        memset(&limits, 0, sizeof(limits));
        limits.max_depth = 5;
        limits.move_variance = 100; /* record everything */
        tt_clear();
        search_init();
        result = search_go(&board, &limits);
        const char *f = sq_name(result.best_move.from);
        char from[3] = {f[0], f[1], 0};
        const char *t = sq_name(result.best_move.to);
        printf("\nDepth-5 search: best=%s%s score=%d nodes=%u\n",
               from, t, result.score, result.nodes);

        move_t cand_moves[16];
        int16_t cand_scores[16];
        uint8_t cand_count;
        search_get_root_candidates(cand_moves, cand_scores, &cand_count);
        printf("Root candidates (%u):\n", cand_count);
        for (i = 0; i < cand_count; i++) {
            const char *cf = sq_name(cand_moves[i].from);
            char cfrom[3] = {cf[0], cf[1], 0};
            const char *ct = sq_name(cand_moves[i].to);
            printf("  %s%s  score=%d%s\n", cfrom, ct, cand_scores[i],
                   (cand_moves[i].from == 0x70 && cand_moves[i].to == 0x60) ? " *** Rxa2" : "");
        }
    }

    return 0;
}
