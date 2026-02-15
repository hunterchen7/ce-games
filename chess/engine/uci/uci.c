#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "../src/engine.h"

/* ========== Time Function ========== */

static uint32_t uci_time_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

/* Track side to move for wtime/btime allocation */
static int8_t current_side = 1; /* 1=white, -1=black */

/* ========== FEN Parsing ========== */

static void parse_fen(const char *fen)
{
    engine_position_t pos;
    int r = 0, c = 0;
    const char *p = fen;

    memset(&pos, 0, sizeof(pos));
    pos.ep_row = ENGINE_EP_NONE;
    pos.ep_col = ENGINE_EP_NONE;

    /* Piece placement */
    while (*p && *p != ' ') {
        if (*p == '/') {
            r++;
            c = 0;
        } else if (*p >= '1' && *p <= '8') {
            c += *p - '0';
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
            if (r < 8 && c < 8)
                pos.board[r][c] = piece;
            c++;
        }
        p++;
    }

    /* Side to move */
    if (*p) p++; /* skip space */
    pos.turn = (*p == 'b') ? -1 : 1;
    current_side = pos.turn;
    if (*p) p++;

    /* Castling */
    if (*p) p++; /* skip space */
    pos.castling = 0;
    while (*p && *p != ' ') {
        switch (*p) {
            case 'K': pos.castling |= ENGINE_CASTLE_WK; break;
            case 'Q': pos.castling |= ENGINE_CASTLE_WQ; break;
            case 'k': pos.castling |= ENGINE_CASTLE_BK; break;
            case 'q': pos.castling |= ENGINE_CASTLE_BQ; break;
        }
        p++;
    }

    /* En passant */
    if (*p) p++; /* skip space */
    if (*p && *p != '-') {
        pos.ep_col = (uint8_t)(*p - 'a');
        p++;
        if (*p) {
            pos.ep_row = (uint8_t)(8 - (*p - '0'));
            p++;
        }
    } else if (*p) {
        p++; /* skip '-' */
    }

    /* Halfmove clock */
    if (*p) p++; /* skip space */
    pos.halfmove_clock = 0;
    while (*p && *p != ' ') {
        pos.halfmove_clock = pos.halfmove_clock * 10 + (uint8_t)(*p - '0');
        p++;
    }

    /* Fullmove number */
    if (*p) p++; /* skip space */
    pos.fullmove_number = 0;
    while (*p && *p >= '0' && *p <= '9') {
        pos.fullmove_number = pos.fullmove_number * 10 + (uint16_t)(*p - '0');
        p++;
    }
    if (pos.fullmove_number == 0) pos.fullmove_number = 1;

    engine_set_position(&pos);
}

/* ========== Move Parsing (UCI algebraic) ========== */

static engine_move_t parse_uci_move(const char *s)
{
    engine_move_t em;
    em.from_col = (uint8_t)(s[0] - 'a');
    em.from_row = (uint8_t)(8 - (s[1] - '0'));
    em.to_col = (uint8_t)(s[2] - 'a');
    em.to_row = (uint8_t)(8 - (s[3] - '0'));
    em.flags = 0;

    if (s[4] && s[4] != ' ' && s[4] != '\n') {
        em.flags |= ENGINE_FLAG_PROMOTION;
        switch (s[4]) {
            case 'r': em.flags |= ENGINE_FLAG_PROMO_R; break;
            case 'b': em.flags |= ENGINE_FLAG_PROMO_B; break;
            case 'n': em.flags |= ENGINE_FLAG_PROMO_N; break;
            default:  em.flags |= ENGINE_FLAG_PROMO_Q; break;
        }
    }

    return em;
}

static void move_to_uci(engine_move_t em, char *out)
{
    out[0] = (char)('a' + em.from_col);
    out[1] = (char)('0' + (8 - em.from_row));
    out[2] = (char)('a' + em.to_col);
    out[3] = (char)('0' + (8 - em.to_row));
    out[4] = '\0';

    if (em.flags & ENGINE_FLAG_PROMOTION) {
        switch (em.flags & ENGINE_FLAG_PROMO_MASK) {
            case ENGINE_FLAG_PROMO_R: out[4] = 'r'; break;
            case ENGINE_FLAG_PROMO_B: out[4] = 'b'; break;
            case ENGINE_FLAG_PROMO_N: out[4] = 'n'; break;
            default:                  out[4] = 'q'; break;
        }
        out[5] = '\0';
    }
}

/* ========== UCI Protocol ========== */

static int move_count = 0;

static void handle_position(char *line)
{
    char *p = line;
    char *moves;

    if (strncmp(p, "startpos", 8) == 0) {
        engine_new_game();
        current_side = 1; /* white */
        move_count = 0;
        p += 8;
    } else if (strncmp(p, "fen ", 4) == 0) {
        p += 4;
        /* Find where "moves" starts (if present) */
        moves = strstr(p, " moves ");
        if (moves) {
            *moves = '\0'; /* terminate FEN string */
            parse_fen(p);
            p = moves + 1;
            *moves = ' ';
            move_count = 0;
        } else {
            parse_fen(p);
            return;
        }
    }

    /* Apply moves if present */
    moves = strstr(p, "moves ");
    if (moves) {
        char *tok;
        moves += 6;
        tok = strtok(moves, " \n");
        while (tok) {
            engine_move_t em = parse_uci_move(tok);
            engine_make_move(em);
            current_side = -current_side;
            move_count++;
            tok = strtok(NULL, " \n");
        }
    }
}

static void handle_go(char *line)
{
    uint8_t depth = 0;
    uint32_t movetime = 0;
    char *p;
    char move_str[6];
    engine_move_t em;

    p = strstr(line, "depth ");
    if (p) depth = (uint8_t)atoi(p + 6);

    p = strstr(line, "movetime ");
    if (p) movetime = (uint32_t)atoi(p + 9);

    /* If wtime/btime are specified, use simple time allocation */
    if (!movetime && !depth) {
        p = strstr(line, current_side == 1 ? "wtime " : "btime ");
        if (p) {
            uint32_t time_left = (uint32_t)atoi(p + 6);
            movetime = time_left / 30;
            if (movetime < 100) movetime = 100;
        }
    }

    if (depth == 0 && movetime == 0)
        depth = 6;

    em = engine_think(depth, movetime);

    if (em.from_row != ENGINE_SQ_NONE) {
        move_to_uci(em, move_str);
        printf("bestmove %s\n", move_str);
    } else {
        printf("bestmove 0000\n");
    }
    fflush(stdout);
}

/* ========== Main Loop ========== */

int main(void)
{
    char line[4096];
    engine_hooks_t hooks;

    setbuf(stdin, NULL);
    setbuf(stdout, NULL);

    hooks.time_ms = uci_time_ms;
    engine_init(&hooks);

    /* engine_set_max_nodes(2000); — disabled for full-strength test */

    while (fgets(line, sizeof(line), stdin)) {
        line[strcspn(line, "\n")] = '\0';

        if (strcmp(line, "uci") == 0) {
            printf("id name TI84Chess\n");
            printf("id author hunterchen\n");
            printf("uciok\n");
            fflush(stdout);
        } else if (strcmp(line, "isready") == 0) {
            printf("readyok\n");
            fflush(stdout);
        } else if (strcmp(line, "ucinewgame") == 0) {
            engine_new_game();
        } else if (strncmp(line, "position ", 9) == 0) {
            handle_position(line + 9);
        } else if (strncmp(line, "go", 2) == 0) {
            handle_go(line + 2);
        } else if (strcmp(line, "quit") == 0) {
            break;
        }
    }

    return 0;
}
