#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "../src/board.h"
#include "../src/movegen.h"
#include "../src/zobrist.h"

/* ========== FEN Parser ========== */

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

    /* Piece placement */
    while (*p && *p != ' ') {
        if (*p == '/') {
            r++;
            c = 0;
        } else if (*p >= '1' && *p <= '8') {
            c += *p - '0';
        } else {
            uint8_t piece = PIECE_NONE;
            uint8_t color = (*p >= 'a') ? COLOR_BLACK : COLOR_WHITE;
            char ch = (*p >= 'a') ? *p - 32 : *p;  /* to uppercase */

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
                b->squares[sq] = piece;
                if (PIECE_TYPE(piece) == PIECE_KING)
                    b->king_sq[s] = sq;
                b->piece_list[s][b->piece_count[s]] = sq;
                b->piece_count[s]++;
            }
            c++;
        }
        p++;
    }

    /* Side to move */
    if (*p == ' ') p++;
    side = (*p == 'b') ? BLACK : WHITE;
    b->side = side;
    p++;

    /* Castling */
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

    /* En passant */
    if (*p == ' ') p++;
    if (*p != '-' && *p) {
        uint8_t file = *p - 'a'; p++;
        uint8_t rank = *p - '1'; p++;
        /* FEN rank 1=bottom (row 7), rank 8=top (row 0) */
        ep_sq = RC_TO_SQ(7 - rank, file);
    } else if (*p) {
        p++;  /* skip '-' */
    }
    b->ep_square = ep_sq;

    /* Halfmove clock */
    if (*p == ' ') p++;
    if (*p) {
        halfmove = (uint8_t)strtol(p, (char **)&p, 10);
    }
    b->halfmove = halfmove;

    /* Fullmove number */
    if (*p == ' ') p++;
    if (*p) {
        fullmove = (uint16_t)strtol(p, (char **)&p, 10);
    }
    b->fullmove = fullmove;

    /* Compute hash from scratch */
    /* We need to call board_compute_hash, but it's static in board.c.
       Instead, we use the Zobrist keys directly. */
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
        if (b->side == BLACK) {
            h ^= zobrist_side;
            l ^= lock_side;
        }
        b->hash = h;
        b->lock = l;
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

        /* Legality check: was the move legal? */
        if (!board_is_legal(b)) {
            board_unmake(b, moves[i], &undo);
            continue;
        }

        nodes += perft(b, depth - 1);
        board_unmake(b, moves[i], &undo);
    }

    return nodes;
}

/* ========== Divide (perft with per-move breakdown) ========== */

static uint64_t divide(board_t *b, int depth)
{
    move_t moves[MAX_MOVES];
    undo_t undo;
    uint8_t nmoves, i;
    uint64_t total = 0;

    nmoves = generate_moves(b, moves, GEN_ALL);

    for (i = 0; i < nmoves; i++) {
        board_make(b, moves[i], &undo);
        if (!board_is_legal(b)) {
            board_unmake(b, moves[i], &undo);
            continue;
        }

        uint64_t sub = perft(b, depth - 1);
        total += sub;

        /* Print move in algebraic notation */
        char from_file = 'a' + SQ_TO_COL(moves[i].from);
        char from_rank = '1' + (7 - SQ_TO_ROW(moves[i].from));
        char to_file   = 'a' + SQ_TO_COL(moves[i].to);
        char to_rank   = '1' + (7 - SQ_TO_ROW(moves[i].to));

        printf("%c%c%c%c", from_file, from_rank, to_file, to_rank);
        if (moves[i].flags & FLAG_PROMOTION) {
            switch (moves[i].flags & FLAG_PROMO_MASK) {
                case FLAG_PROMO_Q: printf("q"); break;
                case FLAG_PROMO_R: printf("r"); break;
                case FLAG_PROMO_B: printf("b"); break;
                case FLAG_PROMO_N: printf("n"); break;
            }
        }
        printf(": %llu\n", (unsigned long long)sub);

        board_unmake(b, moves[i], &undo);
    }

    return total;
}

/* ========== Test Positions ========== */

typedef struct {
    const char *name;
    const char *fen;
    int depth;
    uint64_t expected;
} perft_test_t;

/* --- Standard CPW perft positions --- */
static const perft_test_t standard_tests[] = {
    {
        "Starting position",
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
        5, 4865609ULL
    },
    {
        "Kiwipete",
        "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
        4, 4085603ULL
    },
    {
        "Position 3",
        "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
        5, 674624ULL
    },
    {
        "Position 4",
        "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1",
        5, 15833292ULL
    },
    {
        "Position 5",
        "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8",
        4, 2103487ULL
    },
    {
        "Position 6",
        "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10",
        4, 3894594ULL
    },
    /* Stockfish perft.sh position 7: tricky promotion + queenside castling + checks */
    {
        "Stockfish #7",
        "r7/4p3/5p1q/3P4/4pQ2/4pP2/6pp/R3K1kr w Q - 1 3",
        5, 11609488ULL
    },
};

/* --- Edge-case positions (Peter Ellis Jones collection, verified against Stockfish) ---
   These target specific tricky scenarios at shallow depths. */
static const perft_test_t edge_tests[] = {
    /* Castling + bishop: limited legal moves under check */
    { "Edge: castling blocked by attack",
      "r6r/1b2k1bq/8/8/7B/8/8/R3K2R b KQ - 3 2",
      1, 8ULL },
    /* EP capture on d3 as only legal move */
    { "Edge: en passant saves king",
      "8/8/8/2k5/2pP4/8/B7/4K3 b - d3 0 3",
      1, 8ULL },
    /* White a3 pawn push: verify 19 legal moves */
    { "Edge: knight on a6",
      "r1bqkbnr/pppppppp/n7/8/8/P7/1PPPPPPP/RNBQKBNR w KQkq - 2 2",
      1, 19ULL },
    /* Queen giving check, very few legal responses */
    { "Edge: queen check, 5 responses",
      "r3k2r/p1pp1pb1/bn2Qnp1/2qPN3/1p2P3/2N5/PPPBBPPP/R3K2R b KQkq - 3 2",
      1, 5ULL },
    /* Similar check scenario, more responses */
    { "Edge: queen check, 44 responses",
      "2kr3r/p1ppqpb1/bn2Qnp1/3PN3/1p2P3/2N5/PPPBBPPP/R3K2R b KQ - 3 2",
      1, 44ULL },
    /* White to move with pawn on 7th, promotion variations */
    { "Edge: promotion + queen on d2",
      "rnb2k1r/pp1Pbppp/2p5/q7/2B5/8/PPPQNnPP/RNB1K2R w KQ - 3 9",
      1, 39ULL },
    /* Sparse endgame: pawn vs rook */
    { "Edge: pawn vs empty",
      "2r5/3pk3/8/2P5/8/2K5/8/8 w - - 5 4",
      1, 9ULL },
    /* Position 5 at depth 3: deeper check of same position */
    { "Edge: position 5 d3",
      "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8",
      3, 62379ULL },
    /* Position 6 at depth 3 */
    { "Edge: position 6 d3",
      "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10",
      3, 89890ULL },
    /* EP discovered check: pawn can capture EP to escape pin */
    { "Edge: EP + discovered check (1)",
      "3k4/3p4/8/K1P4r/8/8/8/8 b - - 0 1",
      6, 1134888ULL },
    /* Bishop pin on pawn, EP square present */
    { "Edge: EP + bishop pin",
      "8/8/4k3/8/2p5/8/B2P2K1/8 w - - 0 1",
      6, 1015133ULL },
    /* EP capture into discovered check */
    { "Edge: EP + discovered check (2)",
      "8/8/1k6/2b5/2pP4/8/5K2/8 b - d3 0 1",
      6, 1440467ULL },
    /* Kingside castling only, minimal pieces */
    { "Edge: kingside castle only",
      "5k2/8/8/8/8/8/8/4K2R w K - 0 1",
      6, 661072ULL },
    /* Queenside castling only, minimal pieces */
    { "Edge: queenside castle only",
      "3k4/8/8/8/8/8/8/R3K3 w Q - 0 1",
      6, 803711ULL },
    /* Both sides can castle, bishops + queen */
    { "Edge: mutual castling + sliding pieces",
      "r3k2r/1b4bq/8/8/8/8/7B/R3K2R w KQkq - 0 1",
      4, 1274206ULL },
    /* Both sides castling with queens giving check */
    { "Edge: mutual castling + queen checks",
      "r3k2r/8/3Q4/8/8/5q2/8/R3K2R b KQkq - 0 1",
      4, 1720476ULL },
    /* Pawn on 7th, promotion to all pieces + king in corner */
    { "Edge: promotion vs king",
      "2K2r2/4P3/8/8/8/8/8/3k4 w - - 0 1",
      6, 3821001ULL },
    /* Black underpromotion / stalemate traps */
    { "Edge: promotion + stalemate trap",
      "8/8/1P2K3/8/2n5/1q6/8/5k2 b - - 0 1",
      5, 1004658ULL },
    /* White pawn on 7th, king nearby */
    { "Edge: king + pawn promotion (1)",
      "4k3/1P6/8/8/8/8/K7/8 w - - 0 1",
      6, 217342ULL },
    /* Pawn on a-file about to promote, kings adjacent */
    { "Edge: king + pawn promotion (2)",
      "8/P1k5/K7/8/8/8/8/8 w - - 0 1",
      6, 92683ULL },
    /* Pawn promotion stalemate/checkmate edge */
    { "Edge: promotion stalemate edge",
      "K1k5/8/P7/8/8/8/8/8 w - - 0 1",
      6, 2217ULL },
    /* Pawn on c-file, promotion at depth 7 */
    { "Edge: deep promotion",
      "8/k1P5/8/1K6/8/8/8/8 w - - 0 1",
      7, 567584ULL },
    /* Pure piece endgame: queen + knight vs king */
    { "Edge: queen + knight vs king",
      "8/8/2k5/5q2/5n2/8/5K2/8 b - - 0 1",
      4, 23527ULL },
};

#define NUM_STANDARD (sizeof(standard_tests) / sizeof(standard_tests[0]))
#define NUM_EDGE     (sizeof(edge_tests) / sizeof(edge_tests[0]))

/* ========== Test Runner ========== */

static int run_suite(const char *suite_name, const perft_test_t *tests,
                     unsigned count, board_t *board, int verbose,
                     int *passed, int *failed)
{
    unsigned i;

    printf("=== %s (%u positions) ===\n\n", suite_name, count);

    for (i = 0; i < count; i++) {
        clock_t start, end;
        uint64_t result;
        double elapsed;

        printf("[%u/%u] %s (depth %d)...\n", i + 1, count,
               tests[i].name, tests[i].depth);

        board_set_fen(board, tests[i].fen);

        start = clock();

        if (verbose) {
            printf("  Divide:\n");
            result = divide(board, tests[i].depth);
        } else {
            result = perft(board, tests[i].depth);
        }

        end = clock();
        elapsed = (double)(end - start) / CLOCKS_PER_SEC;

        if (result == tests[i].expected) {
            printf("  PASS: %llu nodes (%.3fs)\n", (unsigned long long)result, elapsed);
            (*passed)++;
        } else {
            printf("  FAIL: got %llu, expected %llu (%.3fs)\n",
                   (unsigned long long)result,
                   (unsigned long long)tests[i].expected,
                   elapsed);
            (*failed)++;
        }
    }

    printf("\n");
    return 0;
}

/* ========== Main ========== */

int main(int argc, char **argv)
{
    board_t board;
    int passed = 0, failed = 0;
    int verbose = 0;
    int skip_edge = 0;
    unsigned total;

    /* Parse args */
    for (int a = 1; a < argc; a++) {
        if (strcmp(argv[a], "-v") == 0 || strcmp(argv[a], "--verbose") == 0)
            verbose = 1;
        else if (strcmp(argv[a], "--standard") == 0)
            skip_edge = 1;
        else if (strcmp(argv[a], "--divide") == 0 && a + 2 < argc) {
            const char *fen = argv[a + 1];
            int depth = atoi(argv[a + 2]);
            zobrist_init(0x12345678u);
            board_set_fen(&board, fen);
            printf("Divide depth %d:\n", depth);
            uint64_t total_nodes = divide(&board, depth);
            printf("\nTotal: %llu\n", (unsigned long long)total_nodes);
            return 0;
        }
    }

    zobrist_init(0x12345678u);

    run_suite("Standard CPW Perft", standard_tests, NUM_STANDARD, &board, verbose,
              &passed, &failed);

    if (!skip_edge) {
        run_suite("Edge Cases (Stockfish/PEJ)", edge_tests, NUM_EDGE, &board, verbose,
                  &passed, &failed);
    }

    total = skip_edge ? NUM_STANDARD : (unsigned)(NUM_STANDARD + NUM_EDGE);
    printf("Results: %d passed, %d failed (of %u)\n", passed, failed, total);
    return failed ? 1 : 0;
}
