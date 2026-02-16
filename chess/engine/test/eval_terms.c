#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/board.h"
#include "../src/eval.h"
#include "../src/zobrist.h"

/*
 * Pull eval.c into this translation unit so feature extraction shares the exact
 * same helper logic, constants, and tables used by evaluate().
 */
#include "../src/eval.c"

static const char *piece_names[6] = { "pawn", "knight", "bishop", "rook", "queen", "king" };

typedef struct {
    int32_t mg_base;
    int32_t eg_base;
    int32_t phase;
    int32_t side_sign;

    int32_t piece_count[6];
    int32_t table_mg[6];
    int32_t table_eg[6];

    int32_t bishop_pair_mg;
    int32_t bishop_pair_eg;
    int32_t tempo_mg;
    int32_t tempo_eg;
    int32_t doubled_mg;
    int32_t doubled_eg;
    int32_t isolated_mg;
    int32_t isolated_eg;
    int32_t rook_open_mg;
    int32_t rook_open_eg;
    int32_t rook_semiopen_mg;
    int32_t rook_semiopen_eg;
    int32_t shield_mg;
    int32_t shield_eg;

    int32_t connected_mg[6];
    int32_t connected_eg[6];
    int32_t passed_mg_terms[6];
    int32_t passed_eg_terms[6];
    int32_t knight_mob_mg_terms[9];
    int32_t knight_mob_eg_terms[9];
    int32_t bishop_mob_mg_terms[14];
    int32_t bishop_mob_eg_terms[14];
} eval_terms_row_t;

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

static void extract_terms(const board_t *b, eval_terms_row_t *out)
{
    uint8_t i, sq, row, col, type;
    uint8_t w_pawns[8], b_pawns[8];

    memset(out, 0, sizeof(*out));

    /* Material+PST is emitted via table/count features, so keep base at zero. */
    out->mg_base = 0;
    out->eg_base = 0;
    out->phase = b->phase;
    if (out->phase > PHASE_MAX) out->phase = PHASE_MAX;
    if (out->phase < 0) out->phase = 0;
    out->side_sign = (b->side == WHITE) ? 1 : -1;

    for (i = 0; i < b->piece_count[WHITE]; i++) {
        sq = b->piece_list[WHITE][i];
        type = PIECE_TYPE(b->squares[sq]);
        {
            uint8_t idx = EVAL_INDEX(type);
            uint8_t sq64 = SQ_TO_SQ64(sq);
            out->piece_count[idx] += 1;
            out->table_mg[idx] += mg_table[idx][sq64];
            out->table_eg[idx] += eg_table[idx][sq64];
        }
    }

    for (i = 0; i < b->piece_count[BLACK]; i++) {
        sq = b->piece_list[BLACK][i];
        type = PIECE_TYPE(b->squares[sq]);
        {
            uint8_t idx = EVAL_INDEX(type);
            uint8_t sq64 = SQ_TO_SQ64(sq);
            uint8_t pst_sq = PST_FLIP(sq64);
            out->piece_count[idx] -= 1;
            out->table_mg[idx] -= mg_table[idx][pst_sq];
            out->table_eg[idx] -= eg_table[idx][pst_sq];
        }
    }

    if (b->bishop_count[WHITE] >= 2) {
        out->bishop_pair_mg += BISHOP_PAIR_MG;
        out->bishop_pair_eg += BISHOP_PAIR_EG;
    }
    if (b->bishop_count[BLACK] >= 2) {
        out->bishop_pair_mg -= BISHOP_PAIR_MG;
        out->bishop_pair_eg -= BISHOP_PAIR_EG;
    }

    if (b->side == WHITE) {
        out->tempo_mg += TEMPO_MG;
        out->tempo_eg += TEMPO_EG;
    } else {
        out->tempo_mg -= TEMPO_MG;
        out->tempo_eg -= TEMPO_EG;
    }

    build_pawn_files(b, WHITE, w_pawns);
    build_pawn_files(b, BLACK, b_pawns);

    for (i = 0; i < b->piece_count[WHITE]; i++) {
        sq = b->piece_list[WHITE][i];
        type = PIECE_TYPE(b->squares[sq]);
        row = SQ_TO_ROW(sq);
        col = SQ_TO_COL(sq);

        if (type == PIECE_PAWN) {
            uint8_t rel_rank = 7 - row;
            uint8_t ri;

            if (w_pawns[col] & (uint8_t)~(1u << row)) {
                out->doubled_mg -= DOUBLED_MG;
                out->doubled_eg -= DOUBLED_EG;
            }

            {
                uint8_t adj = 0;
                if (col > 0) adj |= w_pawns[col - 1];
                if (col < 7) adj |= w_pawns[col + 1];
                if (!adj) {
                    out->isolated_mg -= ISOLATED_MG;
                    out->isolated_eg -= ISOLATED_EG;
                }
            }

            {
                uint8_t supported = 0;
                uint8_t s1 = sq + 17;
                uint8_t s2 = sq + 15;
                if (SQ_VALID(s1) && b->squares[s1] == MAKE_PIECE(COLOR_WHITE, PIECE_PAWN)) supported = 1;
                if (SQ_VALID(s2) && b->squares[s2] == MAKE_PIECE(COLOR_WHITE, PIECE_PAWN)) supported = 1;
                if (supported && rel_rank >= 2 && rel_rank <= 7) {
                    ri = rel_rank - 2;
                    if (ri < 6) {
                        out->connected_mg[ri] += connected_bonus[ri];
                        out->connected_eg[ri] += connected_bonus[ri];
                    }
                }
            }

            {
                uint8_t passed = 1;
                int8_t f;
                for (f = (int8_t)col - 1; f <= (int8_t)col + 1; f++) {
                    uint8_t mask;
                    uint8_t r;
                    if (f < 0 || f > 7) continue;
                    mask = b_pawns[(uint8_t)f];
                    for (r = 0; r < row; r++) {
                        if (mask & (1u << r)) {
                            passed = 0;
                            break;
                        }
                    }
                    if (!passed) break;
                }
                if (passed && rel_rank >= 2) {
                    ri = rel_rank - 2;
                    if (ri < 6) {
                        out->passed_mg_terms[ri] += passed_mg[ri];
                        out->passed_eg_terms[ri] += passed_eg[ri];
                    }
                }
            }
        } else if (type == PIECE_ROOK) {
            if (!w_pawns[col] && !b_pawns[col]) {
                out->rook_open_mg += ROOK_OPEN_MG;
                out->rook_open_eg += ROOK_OPEN_EG;
            } else if (!w_pawns[col] && b_pawns[col]) {
                out->rook_semiopen_mg += ROOK_SEMIOPEN_MG;
                out->rook_semiopen_eg += ROOK_SEMIOPEN_EG;
            }
        }
    }

    for (i = 0; i < b->piece_count[BLACK]; i++) {
        sq = b->piece_list[BLACK][i];
        type = PIECE_TYPE(b->squares[sq]);
        row = SQ_TO_ROW(sq);
        col = SQ_TO_COL(sq);

        if (type == PIECE_PAWN) {
            uint8_t rel_rank = row;
            uint8_t ri;

            if (b_pawns[col] & (uint8_t)~(1u << row)) {
                out->doubled_mg += DOUBLED_MG;
                out->doubled_eg += DOUBLED_EG;
            }

            {
                uint8_t adj = 0;
                if (col > 0) adj |= b_pawns[col - 1];
                if (col < 7) adj |= b_pawns[col + 1];
                if (!adj) {
                    out->isolated_mg += ISOLATED_MG;
                    out->isolated_eg += ISOLATED_EG;
                }
            }

            {
                uint8_t supported = 0;
                uint8_t s1 = sq - 17;
                uint8_t s2 = sq - 15;
                if (SQ_VALID(s1) && b->squares[s1] == MAKE_PIECE(COLOR_BLACK, PIECE_PAWN)) supported = 1;
                if (SQ_VALID(s2) && b->squares[s2] == MAKE_PIECE(COLOR_BLACK, PIECE_PAWN)) supported = 1;
                if (supported && rel_rank >= 2 && rel_rank <= 7) {
                    ri = rel_rank - 2;
                    if (ri < 6) {
                        out->connected_mg[ri] -= connected_bonus[ri];
                        out->connected_eg[ri] -= connected_bonus[ri];
                    }
                }
            }

            {
                uint8_t passed = 1;
                int8_t f;
                for (f = (int8_t)col - 1; f <= (int8_t)col + 1; f++) {
                    uint8_t mask;
                    uint8_t r;
                    if (f < 0 || f > 7) continue;
                    mask = w_pawns[(uint8_t)f];
                    for (r = row + 1; r < 8; r++) {
                        if (mask & (1u << r)) {
                            passed = 0;
                            break;
                        }
                    }
                    if (!passed) break;
                }
                if (passed && rel_rank >= 2) {
                    ri = rel_rank - 2;
                    if (ri < 6) {
                        out->passed_mg_terms[ri] -= passed_mg[ri];
                        out->passed_eg_terms[ri] -= passed_eg[ri];
                    }
                }
            }
        } else if (type == PIECE_ROOK) {
            if (!b_pawns[col] && !w_pawns[col]) {
                out->rook_open_mg -= ROOK_OPEN_MG;
                out->rook_open_eg -= ROOK_OPEN_EG;
            } else if (!b_pawns[col] && w_pawns[col]) {
                out->rook_semiopen_mg -= ROOK_SEMIOPEN_MG;
                out->rook_semiopen_eg -= ROOK_SEMIOPEN_EG;
            }
        }
    }

    {
        uint8_t enemy_side;

        enemy_side = BLACK;
        for (i = 0; i < b->piece_count[WHITE]; i++) {
            sq = b->piece_list[WHITE][i];
            type = PIECE_TYPE(b->squares[sq]);

            if (type == PIECE_KNIGHT) {
                uint8_t mob = 0, j;
                for (j = 0; j < 8; j++) {
                    uint8_t dest = sq + knight_offsets[j];
                    if (!SQ_VALID(dest)) continue;
                    {
                        uint8_t occ = b->squares[dest];
                        if (occ != PIECE_NONE && IS_WHITE(occ)) continue;
                    }
                    if (pawn_attacks_sq(b, dest, enemy_side)) continue;
                    mob++;
                }
                if (mob > 8) mob = 8;
                out->knight_mob_mg_terms[mob] += knight_mob_mg[mob];
                out->knight_mob_eg_terms[mob] += knight_mob_eg[mob];
            } else if (type == PIECE_BISHOP) {
                uint8_t mob = 0, j;
                for (j = 0; j < 4; j++) {
                    uint8_t dest = sq + bishop_offsets[j];
                    while (SQ_VALID(dest)) {
                        uint8_t occ = b->squares[dest];
                        if (occ != PIECE_NONE && IS_WHITE(occ)) break;
                        if (!pawn_attacks_sq(b, dest, enemy_side)) mob++;
                        if (occ != PIECE_NONE) break;
                        dest += bishop_offsets[j];
                    }
                }
                if (mob > 13) mob = 13;
                out->bishop_mob_mg_terms[mob] += bishop_mob_mg[mob];
                out->bishop_mob_eg_terms[mob] += bishop_mob_eg[mob];
            }
        }

        enemy_side = WHITE;
        for (i = 0; i < b->piece_count[BLACK]; i++) {
            sq = b->piece_list[BLACK][i];
            type = PIECE_TYPE(b->squares[sq]);

            if (type == PIECE_KNIGHT) {
                uint8_t mob = 0, j;
                for (j = 0; j < 8; j++) {
                    uint8_t dest = sq + knight_offsets[j];
                    if (!SQ_VALID(dest)) continue;
                    {
                        uint8_t occ = b->squares[dest];
                        if (occ != PIECE_NONE && IS_BLACK(occ)) continue;
                    }
                    if (pawn_attacks_sq(b, dest, enemy_side)) continue;
                    mob++;
                }
                if (mob > 8) mob = 8;
                out->knight_mob_mg_terms[mob] -= knight_mob_mg[mob];
                out->knight_mob_eg_terms[mob] -= knight_mob_eg[mob];
            } else if (type == PIECE_BISHOP) {
                uint8_t mob = 0, j;
                for (j = 0; j < 4; j++) {
                    uint8_t dest = sq + bishop_offsets[j];
                    while (SQ_VALID(dest)) {
                        uint8_t occ = b->squares[dest];
                        if (occ != PIECE_NONE && IS_BLACK(occ)) break;
                        if (!pawn_attacks_sq(b, dest, enemy_side)) mob++;
                        if (occ != PIECE_NONE) break;
                        dest += bishop_offsets[j];
                    }
                }
                if (mob > 13) mob = 13;
                out->bishop_mob_mg_terms[mob] -= bishop_mob_mg[mob];
                out->bishop_mob_eg_terms[mob] -= bishop_mob_eg[mob];
            }
        }
    }

    {
        uint8_t ksq, krow, kcol;
        uint8_t shield;

        ksq = b->king_sq[WHITE];
        krow = SQ_TO_ROW(ksq);
        kcol = SQ_TO_COL(ksq);
        shield = 0;
        if (krow > 0) {
            int8_t c;
            for (c = (int8_t)kcol - 1; c <= (int8_t)kcol + 1; c++) {
                if (c < 0 || c > 7) continue;
                {
                    uint8_t fsq = RC_TO_SQ(krow - 1, (uint8_t)c);
                    if (b->squares[fsq] == MAKE_PIECE(COLOR_WHITE, PIECE_PAWN)) shield++;
                }
            }
        }
        out->shield_mg += shield * SHIELD_MG;
        out->shield_eg += shield * SHIELD_EG;

        ksq = b->king_sq[BLACK];
        krow = SQ_TO_ROW(ksq);
        kcol = SQ_TO_COL(ksq);
        shield = 0;
        if (krow < 7) {
            int8_t c;
            for (c = (int8_t)kcol - 1; c <= (int8_t)kcol + 1; c++) {
                if (c < 0 || c > 7) continue;
                {
                    uint8_t fsq = RC_TO_SQ(krow + 1, (uint8_t)c);
                    if (b->squares[fsq] == MAKE_PIECE(COLOR_BLACK, PIECE_PAWN)) shield++;
                }
            }
        }
        out->shield_mg -= shield * SHIELD_MG;
        out->shield_eg -= shield * SHIELD_EG;
    }
}

static void print_header(void)
{
    int i;

    printf("mg_base,eg_base,phase,side_sign");

    for (i = 0; i < 6; i++) {
        printf(",count_%s", piece_names[i]);
    }
    for (i = 0; i < 6; i++) {
        printf(",table_%s_mg,table_%s_eg", piece_names[i], piece_names[i]);
    }

    printf(",bishop_pair_mg,bishop_pair_eg");
    printf(",tempo_mg,tempo_eg");
    printf(",doubled_mg,doubled_eg");
    printf(",isolated_mg,isolated_eg");
    printf(",rook_open_mg,rook_open_eg");
    printf(",rook_semiopen_mg,rook_semiopen_eg");
    printf(",shield_mg,shield_eg");

    for (i = 0; i < 6; i++) {
        printf(",connected_mg_r%d,connected_eg_r%d", i + 2, i + 2);
    }
    for (i = 0; i < 6; i++) {
        printf(",passed_mg_r%d,passed_eg_r%d", i + 2, i + 2);
    }

    for (i = 0; i <= 8; i++) {
        printf(",knight_mob_mg_%d,knight_mob_eg_%d", i, i);
    }
    for (i = 0; i <= 13; i++) {
        printf(",bishop_mob_mg_%d,bishop_mob_eg_%d", i, i);
    }

    printf("\n");
}

static void print_row(const eval_terms_row_t *t)
{
    int i;

    printf("%d,%d,%d,%d", (int)t->mg_base, (int)t->eg_base, (int)t->phase, (int)t->side_sign);

    for (i = 0; i < 6; i++) {
        printf(",%d", (int)t->piece_count[i]);
    }
    for (i = 0; i < 6; i++) {
        printf(",%d,%d", (int)t->table_mg[i], (int)t->table_eg[i]);
    }

    printf(",%d,%d", (int)t->bishop_pair_mg, (int)t->bishop_pair_eg);
    printf(",%d,%d", (int)t->tempo_mg, (int)t->tempo_eg);
    printf(",%d,%d", (int)t->doubled_mg, (int)t->doubled_eg);
    printf(",%d,%d", (int)t->isolated_mg, (int)t->isolated_eg);
    printf(",%d,%d", (int)t->rook_open_mg, (int)t->rook_open_eg);
    printf(",%d,%d", (int)t->rook_semiopen_mg, (int)t->rook_semiopen_eg);
    printf(",%d,%d", (int)t->shield_mg, (int)t->shield_eg);

    for (i = 0; i < 6; i++) {
        printf(",%d,%d", (int)t->connected_mg[i], (int)t->connected_eg[i]);
    }
    for (i = 0; i < 6; i++) {
        printf(",%d,%d", (int)t->passed_mg_terms[i], (int)t->passed_eg_terms[i]);
    }

    for (i = 0; i <= 8; i++) {
        printf(",%d,%d", (int)t->knight_mob_mg_terms[i], (int)t->knight_mob_eg_terms[i]);
    }
    for (i = 0; i <= 13; i++) {
        printf(",%d,%d", (int)t->bishop_mob_mg_terms[i], (int)t->bishop_mob_eg_terms[i]);
    }

    printf("\n");
}

static void eval_and_print(const char *fen)
{
    board_t b;
    eval_terms_row_t t;

    board_set_fen(&b, fen);
    extract_terms(&b, &t);
    print_row(&t);
}

int main(int argc, char **argv)
{
    print_header();

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

        if (fen_buf[0] != '\0') eval_and_print(fen_buf);
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
