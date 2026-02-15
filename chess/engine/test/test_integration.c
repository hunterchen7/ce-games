/*
 * test_integration.c — Integration tests exercising the engine public API
 *
 * Tests the engine through engine.h only (as the UI does), verifying:
 *   1. Lifecycle (init, new_game, repeated new games)
 *   2. Legal move generation from specific squares
 *   3. Move validation (engine_is_legal_move)
 *   4. Making moves and status detection
 *   5. Castling via engine_get_move_effects
 *   6. En passant via engine_get_move_effects
 *   7. Promotion flow (deferred, as UI does it)
 *   8. AI think + make move
 *   9. Checkmate, stalemate, and draw detection
 *  10. Position get/set roundtrip
 *  11. Full game simulation through public API
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "../src/engine.h"

static int tests_passed = 0;
static int tests_failed = 0;

#define PASS(name) do { tests_passed++; printf("  PASS: %s\n", name); } while(0)
#define FAIL(name, ...) do { tests_failed++; printf("  FAIL: %s - ", name); printf(__VA_ARGS__); printf("\n"); } while(0)

/* Time function for engine */
static uint32_t test_time_ms(void)
{
    return (uint32_t)(clock() * 1000 / CLOCKS_PER_SEC);
}

/* Set a position from a board array and parameters */
static void set_position(const int8_t board[8][8], int8_t turn,
                         uint8_t castling, uint8_t ep_row, uint8_t ep_col)
{
    engine_position_t pos;
    int r, c;
    for (r = 0; r < 8; r++)
        for (c = 0; c < 8; c++)
            pos.board[r][c] = board[r][c];
    pos.turn = turn;
    pos.castling = castling;
    pos.ep_row = ep_row;
    pos.ep_col = ep_col;
    pos.halfmove_clock = 0;
    pos.fullmove_number = 1;
    engine_set_position(&pos);
}

/* ========== Test: Lifecycle ========== */

static void test_lifecycle(void)
{
    engine_hooks_t hooks;
    engine_position_t pos;
    int i;

    printf("=== Lifecycle Tests ===\n");

    hooks.time_ms = test_time_ms;

    /* Init and new game */
    engine_init(&hooks);
    engine_new_game();

    /* Check starting position */
    engine_get_position(&pos);
    if (pos.turn == 1 && pos.board[7][4] == 6 && pos.board[0][4] == -6)
        PASS("Starting position correct");
    else
        FAIL("Starting position", "turn=%d king=%d/%d", pos.turn, pos.board[7][4], pos.board[0][4]);

    /* Multiple new games (shouldn't crash or leak) */
    for (i = 0; i < 5; i++) {
        engine_init(&hooks);
        engine_new_game();
    }
    engine_get_position(&pos);
    if (pos.turn == 1)
        PASS("Multiple init/new_game cycles");
    else
        FAIL("Multiple init/new_game", "turn=%d after reinit", pos.turn);
}

/* ========== Test: Legal Move Generation ========== */

static void test_legal_moves(void)
{
    engine_move_t moves[64];
    uint8_t count;
    engine_hooks_t hooks;

    printf("\n=== Legal Move Tests ===\n");

    hooks.time_ms = test_time_ms;
    engine_init(&hooks);
    engine_new_game();

    /* White pawn on e2 (row 6, col 4) should have 2 moves: e3, e4 */
    count = engine_get_moves_from(6, 4, moves, 64);
    if (count == 2)
        PASS("Pawn e2 has 2 moves");
    else
        FAIL("Pawn e2 moves", "expected 2, got %d", count);

    /* White knight on b1 (row 7, col 1) should have 2 moves: a3, c3 */
    count = engine_get_moves_from(7, 1, moves, 64);
    if (count == 2)
        PASS("Knight b1 has 2 moves");
    else
        FAIL("Knight b1 moves", "expected 2, got %d", count);

    /* White king on e1 (row 7, col 4) should have 0 moves initially */
    count = engine_get_moves_from(7, 4, moves, 64);
    if (count == 0)
        PASS("King e1 has 0 moves at start");
    else
        FAIL("King e1 moves", "expected 0, got %d", count);

    /* All moves for white at start should be 20 */
    count = engine_get_all_moves(moves, 64);
    if (count == 20)
        PASS("White has 20 moves at start");
    else
        FAIL("White start moves", "expected 20, got %d", count);

    /* Verify engine_is_legal_move */
    {
        engine_move_t m;
        m.from_row = 6; m.from_col = 4; /* e2 */
        m.to_row = 4; m.to_col = 4;     /* e4 */
        m.flags = 0;
        if (engine_is_legal_move(m))
            PASS("e2-e4 is legal");
        else
            FAIL("e2-e4 legal", "should be legal");

        /* Illegal: e2-e5 */
        m.to_row = 3;
        if (!engine_is_legal_move(m))
            PASS("e2-e5 is illegal");
        else
            FAIL("e2-e5 legal", "should be illegal");
    }
}

/* ========== Test: Making Moves and Status ========== */

static void test_make_move(void)
{
    engine_move_t m;
    engine_position_t pos;
    uint8_t status;
    engine_hooks_t hooks;

    printf("\n=== Make Move Tests ===\n");

    hooks.time_ms = test_time_ms;
    engine_init(&hooks);
    engine_new_game();

    /* Play 1. e4 */
    m.from_row = 6; m.from_col = 4;
    m.to_row = 4; m.to_col = 4;
    m.flags = 0;
    status = engine_make_move(m);

    engine_get_position(&pos);
    if (pos.turn == -1 && pos.board[4][4] == 1 && pos.board[6][4] == 0)
        PASS("1. e4 applied correctly");
    else
        FAIL("1. e4", "turn=%d board[4][4]=%d board[6][4]=%d",
             pos.turn, pos.board[4][4], pos.board[6][4]);

    if (status == ENGINE_STATUS_NORMAL)
        PASS("Status after 1. e4 is normal");
    else
        FAIL("Status after e4", "expected NORMAL, got %d", status);

    /* Play 1... e5 */
    m.from_row = 1; m.from_col = 4;
    m.to_row = 3; m.to_col = 4;
    m.flags = 0;
    status = engine_make_move(m);

    engine_get_position(&pos);
    if (pos.turn == 1 && pos.board[3][4] == -1)
        PASS("1... e5 applied correctly");
    else
        FAIL("1... e5", "turn=%d board[3][4]=%d", pos.turn, pos.board[3][4]);
}

/* ========== Test: Castling Effects ========== */

static void test_castling_effects(void)
{
    /* Position where white can castle kingside:
       Row 7: R . . . K . . R (white)
       Only pawns in between cleared */
    int8_t board[8][8];
    engine_move_t moves[64];
    engine_move_effects_t fx;
    uint8_t count, i;
    int found_castle = 0;
    engine_hooks_t hooks;

    printf("\n=== Castling Effects Tests ===\n");

    hooks.time_ms = test_time_ms;
    engine_init(&hooks);

    memset(board, 0, sizeof(board));
    board[7][0] = 4;  /* W_ROOK a1 */
    board[7][4] = 6;  /* W_KING e1 */
    board[7][7] = 4;  /* W_ROOK h1 */
    board[0][4] = -6; /* B_KING e8 */

    set_position(board, 1, ENGINE_CASTLE_WK | ENGINE_CASTLE_WQ,
                 ENGINE_EP_NONE, ENGINE_EP_NONE);

    /* Get king moves */
    count = engine_get_moves_from(7, 4, moves, 64);

    for (i = 0; i < count; i++) {
        if (moves[i].flags & ENGINE_FLAG_CASTLE) {
            found_castle = 1;
            engine_get_move_effects(moves[i], &fx);

            if (fx.has_rook_move) {
                if (moves[i].to_col == 6) {
                    /* Kingside */
                    if (fx.rook_from_row == 7 && fx.rook_from_col == 7 &&
                        fx.rook_to_row == 7 && fx.rook_to_col == 5)
                        PASS("Kingside castle rook effects correct");
                    else
                        FAIL("Kingside castle rook", "from=%d,%d to=%d,%d",
                             fx.rook_from_row, fx.rook_from_col,
                             fx.rook_to_row, fx.rook_to_col);
                } else if (moves[i].to_col == 2) {
                    /* Queenside */
                    if (fx.rook_from_row == 7 && fx.rook_from_col == 0 &&
                        fx.rook_to_row == 7 && fx.rook_to_col == 3)
                        PASS("Queenside castle rook effects correct");
                    else
                        FAIL("Queenside castle rook", "from=%d,%d to=%d,%d",
                             fx.rook_from_row, fx.rook_from_col,
                             fx.rook_to_row, fx.rook_to_col);
                }
            } else {
                FAIL("Castle effects", "has_rook_move is false");
            }
        }
    }

    if (!found_castle)
        FAIL("Castle moves", "no castle moves found (got %d moves)", count);

    /* Make the kingside castle and verify position */
    {
        engine_move_t castle_move;
        engine_position_t pos;
        castle_move.from_row = 7; castle_move.from_col = 4;
        castle_move.to_row = 7; castle_move.to_col = 6;
        castle_move.flags = ENGINE_FLAG_CASTLE;
        engine_make_move(castle_move);
        engine_get_position(&pos);
        if (pos.board[7][6] == 6 && pos.board[7][5] == 4 &&
            pos.board[7][4] == 0 && pos.board[7][7] == 0)
            PASS("Kingside castle position correct");
        else
            FAIL("Kingside castle pos", "king=%d rook=%d old_king=%d old_rook=%d",
                 pos.board[7][6], pos.board[7][5], pos.board[7][4], pos.board[7][7]);
    }
}

/* ========== Test: En Passant Effects ========== */

static void test_ep_effects(void)
{
    /* White pawn on e5, black pawn just played d7-d5 */
    int8_t board[8][8];
    engine_move_t moves[64];
    engine_move_effects_t fx;
    uint8_t count, i;
    int found_ep = 0;
    engine_hooks_t hooks;

    printf("\n=== En Passant Effects Tests ===\n");

    hooks.time_ms = test_time_ms;
    engine_init(&hooks);

    memset(board, 0, sizeof(board));
    board[3][4] = 1;   /* W_PAWN e5 (row 3) */
    board[3][3] = -1;  /* B_PAWN d5 (row 3, just arrived) */
    board[7][4] = 6;   /* W_KING e1 */
    board[0][4] = -6;  /* B_KING e8 */

    /* EP target is d6 = row 2, col 3 */
    set_position(board, 1, 0, 2, 3);

    count = engine_get_moves_from(3, 4, moves, 64);

    for (i = 0; i < count; i++) {
        if (moves[i].flags & ENGINE_FLAG_EN_PASSANT) {
            found_ep = 1;
            engine_get_move_effects(moves[i], &fx);

            if (fx.has_ep_capture &&
                fx.ep_capture_row == 3 && fx.ep_capture_col == 3)
                PASS("EP capture square correct (d5 = row 3, col 3)");
            else
                FAIL("EP capture square", "has_ep=%d row=%d col=%d",
                     fx.has_ep_capture, fx.ep_capture_row, fx.ep_capture_col);

            /* Make the EP capture */
            {
                engine_position_t pos;
                engine_make_move(moves[i]);
                engine_get_position(&pos);
                if (pos.board[2][3] == 1 && pos.board[3][3] == 0 && pos.board[3][4] == 0)
                    PASS("EP capture applied correctly");
                else
                    FAIL("EP capture", "dest=%d cap_sq=%d orig=%d",
                         pos.board[2][3], pos.board[3][3], pos.board[3][4]);
            }
            break;
        }
    }

    if (!found_ep)
        FAIL("EP detection", "no EP move found (got %d moves)", count);
}

/* ========== Test: Promotion Flow ========== */

static void test_promotion(void)
{
    /* White pawn on e7 about to promote */
    int8_t board[8][8];
    engine_move_t moves[64];
    uint8_t count, i;
    int promo_count = 0;
    engine_hooks_t hooks;

    printf("\n=== Promotion Tests ===\n");

    hooks.time_ms = test_time_ms;
    engine_init(&hooks);

    memset(board, 0, sizeof(board));
    board[1][4] = 1;   /* W_PAWN e7 (row 1) */
    board[7][4] = 6;   /* W_KING e1 */
    board[0][0] = -6;  /* B_KING a8 */

    set_position(board, 1, 0, ENGINE_EP_NONE, ENGINE_EP_NONE);

    count = engine_get_moves_from(1, 4, moves, 64);

    /* Should have 4 promotion moves (Q, R, B, N) */
    for (i = 0; i < count; i++) {
        if (moves[i].flags & ENGINE_FLAG_PROMOTION)
            promo_count++;
    }

    if (promo_count == 4)
        PASS("Pawn e7 has 4 promotion moves");
    else
        FAIL("Promotion count", "expected 4, got %d (total moves: %d)", promo_count, count);

    /* Test the deferred promotion flow (as UI does it):
       1. Find any promotion move to e8
       2. Change flags to specific promotion type
       3. Call engine_make_move */
    {
        engine_move_t promo_move;
        engine_position_t pos;
        uint8_t status;

        /* Find queen promotion */
        promo_move.from_row = 1; promo_move.from_col = 4;
        promo_move.to_row = 0; promo_move.to_col = 4;
        promo_move.flags = ENGINE_FLAG_PROMOTION | ENGINE_FLAG_PROMO_Q;

        status = engine_make_move(promo_move);
        engine_get_position(&pos);

        if (pos.board[0][4] == 5) /* W_QUEEN */
            PASS("Queen promotion applied correctly");
        else
            FAIL("Queen promotion", "expected W_QUEEN(5), got %d", pos.board[0][4]);

        (void)status;
    }

    /* Test knight promotion */
    engine_init(&hooks);
    memset(board, 0, sizeof(board));
    board[1][4] = 1;   /* W_PAWN e7 */
    board[7][4] = 6;   /* W_KING e1 */
    board[0][0] = -6;  /* B_KING a8 */
    set_position(board, 1, 0, ENGINE_EP_NONE, ENGINE_EP_NONE);

    {
        engine_move_t promo_move;
        engine_position_t pos;

        promo_move.from_row = 1; promo_move.from_col = 4;
        promo_move.to_row = 0; promo_move.to_col = 4;
        promo_move.flags = ENGINE_FLAG_PROMOTION | ENGINE_FLAG_PROMO_N;

        engine_make_move(promo_move);
        engine_get_position(&pos);

        if (pos.board[0][4] == 2) /* W_KNIGHT */
            PASS("Knight promotion applied correctly");
        else
            FAIL("Knight promotion", "expected W_KNIGHT(2), got %d", pos.board[0][4]);
    }
}

/* ========== Test: AI Think ========== */

static void test_ai_think(void)
{
    engine_move_t m;
    engine_hooks_t hooks;

    printf("\n=== AI Think Tests ===\n");

    hooks.time_ms = test_time_ms;
    engine_init(&hooks);
    engine_new_game();

    /* AI should return a valid move from starting position */
    m = engine_think(2, 5000);
    if (m.from_row != ENGINE_SQ_NONE)
        PASS("AI returns valid move from start");
    else
        FAIL("AI start move", "got ENGINE_SQ_NONE");

    /* Verify the move is legal */
    if (engine_is_legal_move(m))
        PASS("AI move is legal");
    else
        FAIL("AI move legal", "from=%d,%d to=%d,%d flags=%d",
             m.from_row, m.from_col, m.to_row, m.to_col, m.flags);

    /* Make the AI move and verify status */
    {
        uint8_t status = engine_make_move(m);
        if (status == ENGINE_STATUS_NORMAL || status == ENGINE_STATUS_CHECK)
            PASS("Status after AI move is valid");
        else
            FAIL("AI move status", "got %d", status);
    }
}

/* ========== Test: Game End Detection ========== */

static void test_game_end(void)
{
    int8_t board[8][8];
    engine_hooks_t hooks;
    uint8_t status;

    printf("\n=== Game End Detection Tests ===\n");

    hooks.time_ms = test_time_ms;

    /* Checkmate: back rank mate — white rook on a8, black king on h8,
       black pawns on f7, g7, h7 blocking escape */
    engine_init(&hooks);
    memset(board, 0, sizeof(board));
    board[0][7] = -6;  /* B_KING h8 */
    board[1][5] = -1;  /* B_PAWN f7 */
    board[1][6] = -1;  /* B_PAWN g7 */
    board[1][7] = -1;  /* B_PAWN h7 */
    board[0][0] = 4;   /* W_ROOK a8 (delivering check) */
    board[7][4] = 6;   /* W_KING e1 */
    set_position(board, -1, 0, ENGINE_EP_NONE, ENGINE_EP_NONE);

    status = engine_get_status();
    if (status == ENGINE_STATUS_CHECKMATE)
        PASS("Checkmate detection (Ra8#)");
    else
        FAIL("Checkmate", "expected CHECKMATE, got %d", status);

    /* Stalemate: black king on a8, white queen on b6, white king on c8 */
    engine_init(&hooks);
    memset(board, 0, sizeof(board));
    board[0][0] = -6;  /* B_KING a8 */
    board[2][1] = 5;   /* W_QUEEN b6 */
    board[0][2] = 6;   /* W_KING c8 */
    set_position(board, -1, 0, ENGINE_EP_NONE, ENGINE_EP_NONE);

    status = engine_get_status();
    if (status == ENGINE_STATUS_STALEMATE)
        PASS("Stalemate detection");
    else
        FAIL("Stalemate", "expected STALEMATE, got %d", status);

    /* Insufficient material: K vs K */
    engine_init(&hooks);
    memset(board, 0, sizeof(board));
    board[0][0] = -6;  /* B_KING a8 */
    board[7][7] = 6;   /* W_KING h1 */
    set_position(board, 1, 0, ENGINE_EP_NONE, ENGINE_EP_NONE);

    status = engine_get_status();
    if (status == ENGINE_STATUS_DRAW_MAT)
        PASS("Insufficient material (K vs K)");
    else
        FAIL("K vs K", "expected DRAW_MAT, got %d", status);

    /* Check detection */
    engine_init(&hooks);
    memset(board, 0, sizeof(board));
    board[0][4] = -6;  /* B_KING e8 */
    board[4][4] = 4;   /* W_ROOK e4 (checking) */
    board[7][0] = 6;   /* W_KING a1 */
    set_position(board, -1, 0, ENGINE_EP_NONE, ENGINE_EP_NONE);

    status = engine_get_status();
    if (status == ENGINE_STATUS_CHECK)
        PASS("Check detection (Re4+)");
    else
        FAIL("Check detection", "expected CHECK, got %d", status);

    if (engine_in_check())
        PASS("engine_in_check() agrees");
    else
        FAIL("engine_in_check", "returned false when in check");
}

/* ========== Test: Position Get/Set Roundtrip ========== */

static void test_position_roundtrip(void)
{
    engine_position_t pos1, pos2;
    int r, c;
    int match;
    engine_hooks_t hooks;

    printf("\n=== Position Roundtrip Tests ===\n");

    hooks.time_ms = test_time_ms;
    engine_init(&hooks);
    engine_new_game();

    /* Get position, set it back, get again, compare */
    engine_get_position(&pos1);
    engine_set_position(&pos1);
    engine_get_position(&pos2);

    match = 1;
    for (r = 0; r < 8 && match; r++)
        for (c = 0; c < 8 && match; c++)
            if (pos1.board[r][c] != pos2.board[r][c])
                match = 0;

    if (match && pos1.turn == pos2.turn && pos1.castling == pos2.castling)
        PASS("Position roundtrip preserves board");
    else
        FAIL("Position roundtrip", "board mismatch");
}

/* ========== Test: Full Game Through Public API ========== */

static void test_full_game(void)
{
    engine_hooks_t hooks;
    engine_move_t m;
    uint8_t status;
    int half_moves = 0;
    int max_moves = 200;

    printf("\n=== Full Game Simulation ===\n");

    hooks.time_ms = test_time_ms;
    engine_init(&hooks);
    engine_new_game();

    /* Play a game with AI on both sides at depth 1 */
    while (half_moves < max_moves) {
        m = engine_think(1, 2000);
        if (m.from_row == ENGINE_SQ_NONE)
            break;

        /* Get effects before making move (as UI does) */
        {
            engine_move_effects_t fx;
            engine_get_move_effects(m, &fx);
            (void)fx; /* just verify it doesn't crash */
        }

        status = engine_make_move(m);
        half_moves++;

        if (status == ENGINE_STATUS_CHECKMATE ||
            status >= ENGINE_STATUS_STALEMATE)
            break;
    }

    printf("  Game played %d half-moves, final status: ", half_moves);
    switch (status) {
        case ENGINE_STATUS_NORMAL:    printf("normal\n"); break;
        case ENGINE_STATUS_CHECK:     printf("check\n"); break;
        case ENGINE_STATUS_CHECKMATE: printf("checkmate\n"); break;
        case ENGINE_STATUS_STALEMATE: printf("stalemate\n"); break;
        case ENGINE_STATUS_DRAW_50:   printf("50-move\n"); break;
        case ENGINE_STATUS_DRAW_REP:  printf("repetition\n"); break;
        case ENGINE_STATUS_DRAW_MAT:  printf("insufficient\n"); break;
        default:                      printf("unknown(%d)\n", status); break;
    }

    PASS("Full game simulation (no crash)");
}

/* ========== Test: Move Effects for Non-Special Moves ========== */

static void test_normal_move_effects(void)
{
    engine_move_t m;
    engine_move_effects_t fx;
    engine_hooks_t hooks;

    printf("\n=== Normal Move Effects Tests ===\n");

    hooks.time_ms = test_time_ms;
    engine_init(&hooks);
    engine_new_game();

    /* Normal pawn move should have no special effects */
    m.from_row = 6; m.from_col = 4;
    m.to_row = 4; m.to_col = 4;
    m.flags = 0;

    engine_get_move_effects(m, &fx);
    if (!fx.has_rook_move && !fx.has_ep_capture)
        PASS("Normal move has no special effects");
    else
        FAIL("Normal move effects", "rook=%d ep=%d", fx.has_rook_move, fx.has_ep_capture);
}

/* ========== Main ========== */

int main(void)
{
    printf("Chess Engine Integration Tests\n");
    printf("==============================\n\n");

    test_lifecycle();
    test_legal_moves();
    test_make_move();
    test_castling_effects();
    test_ep_effects();
    test_promotion();
    test_ai_think();
    test_game_end();
    test_position_roundtrip();
    test_normal_move_effects();
    test_full_game();

    printf("\n========================================\n");
    printf("Results: %d passed, %d failed (of %d)\n",
           tests_passed, tests_failed, tests_passed + tests_failed);

    return tests_failed ? 1 : 0;
}
