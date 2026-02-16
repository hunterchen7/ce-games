#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/board.h"
#include "../src/eval.h"
#include "../src/zobrist.h"

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
        if (*p == '/') {
            r++;
            c = 0;
        } else if (*p >= '1' && *p <= '8') {
            c += *p - '0';
        } else {
            uint8_t piece = PIECE_NONE;
            uint8_t color = (*p >= 'a') ? COLOR_BLACK : COLOR_WHITE;
            char ch = (*p >= 'a') ? (char)(*p - 32) : *p;

            switch (ch) {
                case 'P': piece = MAKE_PIECE(color, PIECE_PAWN); break;
                case 'N': piece = MAKE_PIECE(color, PIECE_KNIGHT); break;
                case 'B': piece = MAKE_PIECE(color, PIECE_BISHOP); break;
                case 'R': piece = MAKE_PIECE(color, PIECE_ROOK); break;
                case 'Q': piece = MAKE_PIECE(color, PIECE_QUEEN); break;
                case 'K': piece = MAKE_PIECE(color, PIECE_KING); break;
            }

            if (piece != PIECE_NONE && r < 8 && c < 8) {
                uint8_t sq = RC_TO_SQ(r, c);
                uint8_t s = IS_BLACK(piece) ? BLACK : WHITE;
                uint8_t idx = b->piece_count[s];

                b->squares[sq] = piece;
                b->piece_list[s][idx] = sq;
                b->piece_index[sq] = idx;
                b->piece_count[s] = idx + 1;

                if (PIECE_TYPE(piece) == PIECE_KING) {
                    b->king_sq[s] = sq;
                } else if (PIECE_TYPE(piece) == PIECE_BISHOP) {
                    b->bishop_count[s]++;
                }
            }

            c++;
        }
        p++;
    }

    if (*p == ' ') p++;
    side = (*p == 'b') ? BLACK : WHITE;
    b->side = side;
    if (*p) p++;

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
        uint8_t file = (uint8_t)(*p - 'a');
        p++;
        if (*p >= '1' && *p <= '8') {
            uint8_t rank = (uint8_t)(*p - '1');
            ep_sq = RC_TO_SQ(7 - rank, file);
            p++;
        }
    } else if (*p) {
        p++;
    }
    b->ep_square = ep_sq;

    if (*p == ' ') p++;
    if (*p) halfmove = (uint8_t)strtol(p, (char **)&p, 10);
    b->halfmove = halfmove;

    if (*p == ' ') p++;
    if (*p) fullmove = (uint16_t)strtol(p, (char **)&p, 10);
    b->fullmove = fullmove;

    {
        uint32_t h = 0;
        uint16_t l = 0;
        int sq;
        for (sq = 0; sq < 128; sq++) {
            uint8_t piece;
            uint8_t pidx;
            uint8_t sq64;
            if (!SQ_VALID(sq)) continue;
            piece = b->squares[sq];
            if (piece == PIECE_NONE) continue;
            pidx = zobrist_piece_index(piece);
            sq64 = SQ_TO_SQ64((uint8_t)sq);
            h ^= zobrist_piece[pidx][sq64];
            l ^= lock_piece[pidx][sq64];
        }
        h ^= zobrist_castle[b->castling];
        l ^= lock_castle[b->castling];
        if (b->ep_square != SQ_NONE) {
            h ^= zobrist_ep_file[SQ_TO_COL(b->ep_square)];
            l ^= lock_ep_file[SQ_TO_COL(b->ep_square)];
        }
        if (b->side == BLACK) {
            h ^= zobrist_side;
            l ^= lock_side;
        }
        b->hash = h;
        b->lock = l;
    }

    b->mg[WHITE] = 0;
    b->mg[BLACK] = 0;
    b->eg[WHITE] = 0;
    b->eg[BLACK] = 0;
    b->phase = 0;

    {
        int sq;
        for (sq = 0; sq < 128; sq++) {
            uint8_t piece;
            uint8_t type;
            uint8_t s;
            uint8_t idx;
            uint8_t sq64;
            uint8_t pst_sq;

            if (!SQ_VALID(sq)) continue;
            piece = b->squares[sq];
            if (piece == PIECE_NONE) continue;

            type = PIECE_TYPE(piece);
            s = IS_BLACK(piece) ? BLACK : WHITE;
            idx = EVAL_INDEX(type);
            sq64 = SQ_TO_SQ64((uint8_t)sq);
            pst_sq = (s == WHITE) ? sq64 : PST_FLIP(sq64);

            b->mg[s] += mg_table[idx][pst_sq];
            b->eg[s] += eg_table[idx][pst_sq];
            b->phase += phase_weight[idx];
        }
    }
}

static void trim_line(char *s)
{
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r')) {
        s[n - 1] = '\0';
        n--;
    }
}

static void eval_and_print(const char *fen)
{
    board_t b;
    board_set_fen(&b, fen);
    printf("%d\n", (int)evaluate(&b));
}

int main(int argc, char **argv)
{
    if (argc > 1) {
        char fen_buf[512];
        size_t len = 0;
        int i;

        fen_buf[0] = '\0';
        for (i = 1; i < argc; i++) {
            size_t alen = strlen(argv[i]);
            if (len + alen + 2 >= sizeof(fen_buf)) break;
            if (i > 1) {
                fen_buf[len++] = ' ';
                fen_buf[len] = '\0';
            }
            memcpy(fen_buf + len, argv[i], alen);
            len += alen;
            fen_buf[len] = '\0';
        }
        eval_and_print(fen_buf);
        return 0;
    }

    {
        char line[512];
        while (fgets(line, sizeof(line), stdin)) {
            trim_line(line);
            if (line[0] == '\0') continue;
            eval_and_print(line);
        }
    }

    return 0;
}
