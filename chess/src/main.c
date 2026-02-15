#include <graphx.h>
#include <keypadc.h>
#include <sys/util.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include "engine.h"

/* ========== Screen & Layout ========== */

#define SCREEN_W 320
#define SCREEN_H 240

#define SQ_SIZE  26
#define BOARD_PX (SQ_SIZE * 8)       /* 208 */
#define BOARD_X  16
#define BOARD_Y  ((SCREEN_H - BOARD_PX) / 2)  /* 16 */
#define SIDEBAR_X (BOARD_X + BOARD_PX + 6)     /* 230 */
#define SIDEBAR_W (SCREEN_W - SIDEBAR_X - 2)   /* 88 */

/* ========== Framerate ========== */

#define TARGET_FPS 60
#define FRAME_TIME (CLOCKS_PER_SEC / TARGET_FPS)

/* ========== Palette Indices ========== */

#define PAL_BG          0
#define PAL_LIGHT_SQ    1
#define PAL_DARK_SQ     2
#define PAL_SEL_LIGHT   3
#define PAL_SEL_DARK    4
#define PAL_LAST_LIGHT  5
#define PAL_LAST_DARK   6
#define PAL_WHITE_PC    7
#define PAL_BLACK_PC    8
#define PAL_PIECE_OL    9
#define PAL_CURSOR      10
#define PAL_TEXT         11
#define PAL_MENU_HL     12
#define PAL_MENU_BG     13
#define PAL_PROMO_BG    14
#define PAL_SIDEBAR     15
#define PAL_LEGAL       16

/* ========== Piece Constants ========== */

#define EMPTY    0
#define W_PAWN   1
#define W_KNIGHT 2
#define W_BISHOP 3
#define W_ROOK   4
#define W_QUEEN  5
#define W_KING   6
#define B_PAWN   (-1)
#define B_KNIGHT (-2)
#define B_BISHOP (-3)
#define B_ROOK   (-4)
#define B_QUEEN  (-5)
#define B_KING   (-6)

#define PIECE_TYPE(p)  ((p) > 0 ? (p) : -(p))
#define PIECE_IS_WHITE(p) ((p) > 0)

#define WHITE_TURN  1
#define BLACK_TURN (-1)

/* ========== Game States ========== */

typedef enum {
    STATE_MENU,
    STATE_DIFFICULTY,
    STATE_PLAYING,
    STATE_PROMOTION,
    STATE_GAMEOVER
} game_state_t;

#define MODE_HUMAN    0
#define MODE_COMPUTER 1

/* ========== Global State ========== */

static game_state_t state;
static int running;

/* board — board[row][col], row 0 = top of screen (black back rank) */
static int8_t board[8][8];

/* cursor & selection */
static int cur_r, cur_c;
static int sel_r, sel_c;       /* -1 = nothing selected */
static int current_turn;       /* WHITE_TURN or BLACK_TURN */

/* game mode */
static int game_mode;
static int ai_difficulty;      /* 1-10 */

/* last move highlight */
static int last_from_r, last_from_c;
static int last_to_r, last_to_c;
static int has_last_move;

/* promotion */
static int promo_r, promo_c;
static int promo_cursor;       /* 0=Queen, 1=Rook, 2=Bishop, 3=Knight */
static int promo_from_r, promo_from_c; /* origin square for undo */
static int8_t promo_captured;          /* piece that was on dest square */
static int promo_saved_has_last;       /* saved last-move state for cancel */
static int promo_saved_from_r, promo_saved_from_c;
static int promo_saved_to_r, promo_saved_to_c;

/* animation */
#define ANIM_MS_PER_SQ  50
#define ANIM_MAX_MS    200
static int anim_active;
static int8_t anim_piece;
static int anim_from_r, anim_from_c;
static int anim_to_r, anim_to_c;
static int8_t anim_captured;   /* piece captured at destination */
static clock_t anim_start;
static clock_t anim_duration;  /* precomputed ticks for this move */

/* game over */
static int winner; /* WHITE_TURN, BLACK_TURN, or 0 for draw */
static uint8_t game_over_reason; /* ENGINE_STATUS_* value */

/* menu state */
static int menu_cursor;
static int diff_cursor;

/* keyboard */
static uint8_t cur_g1, cur_g6, cur_g7;
static uint8_t prev_g1, prev_g6, prev_g7;

/* legal move targets (engine integration) */
static engine_move_t legal_targets[64];
static uint8_t legal_target_count;

/* pending move being animated */
static engine_move_t pending_move;
static engine_move_effects_t pending_effects;

/* AI state: 0=idle, 1=draw thinking screen, 2=compute */
static int ai_thinking;

/* ========== Engine Time Function ========== */

static uint32_t ce_time_ms(void)
{
    clock_t t = clock();
    /* Split to avoid overflow: clock() * 1000 overflows uint32 after ~22 min */
    return (uint32_t)(t / CLOCKS_PER_SEC) * 1000u +
           (uint32_t)(t % CLOCKS_PER_SEC) * 1000u / CLOCKS_PER_SEC;
}

/* ========== Palette Setup ========== */

static void setup_palette(void)
{
    gfx_palette[PAL_BG]        = gfx_RGBTo1555(34, 34, 34);
    gfx_palette[PAL_LIGHT_SQ]  = gfx_RGBTo1555(240, 217, 181);
    gfx_palette[PAL_DARK_SQ]   = gfx_RGBTo1555(181, 136, 99);
    gfx_palette[PAL_SEL_LIGHT] = gfx_RGBTo1555(246, 246, 130);
    gfx_palette[PAL_SEL_DARK]  = gfx_RGBTo1555(186, 202, 68);
    gfx_palette[PAL_LAST_LIGHT]= gfx_RGBTo1555(205, 210, 106);
    gfx_palette[PAL_LAST_DARK] = gfx_RGBTo1555(170, 162, 58);
    gfx_palette[PAL_WHITE_PC]  = gfx_RGBTo1555(255, 255, 255);
    gfx_palette[PAL_BLACK_PC]  = gfx_RGBTo1555(30, 30, 30);
    gfx_palette[PAL_PIECE_OL]  = gfx_RGBTo1555(80, 80, 80);
    gfx_palette[PAL_CURSOR]    = gfx_RGBTo1555(50, 120, 255);
    gfx_palette[PAL_TEXT]      = gfx_RGBTo1555(220, 220, 220);
    gfx_palette[PAL_MENU_HL]   = gfx_RGBTo1555(70, 130, 230);
    gfx_palette[PAL_MENU_BG]   = gfx_RGBTo1555(20, 20, 30);
    gfx_palette[PAL_PROMO_BG]  = gfx_RGBTo1555(50, 50, 60);
    gfx_palette[PAL_SIDEBAR]   = gfx_RGBTo1555(44, 44, 44);
    gfx_palette[PAL_LEGAL]     = gfx_RGBTo1555(100, 180, 100);
}

/* ========== Board Init ========== */

static void init_board(void)
{
    int r, c;

    for (r = 0; r < 8; r++)
        for (c = 0; c < 8; c++)
            board[r][c] = EMPTY;

    /* black pieces (top, row 0) */
    board[0][0] = B_ROOK;   board[0][1] = B_KNIGHT;
    board[0][2] = B_BISHOP; board[0][3] = B_QUEEN;
    board[0][4] = B_KING;   board[0][5] = B_BISHOP;
    board[0][6] = B_KNIGHT; board[0][7] = B_ROOK;
    for (c = 0; c < 8; c++) board[1][c] = B_PAWN;

    /* white pieces (bottom, row 7) */
    board[7][0] = W_ROOK;   board[7][1] = W_KNIGHT;
    board[7][2] = W_BISHOP; board[7][3] = W_QUEEN;
    board[7][4] = W_KING;   board[7][5] = W_BISHOP;
    board[7][6] = W_KNIGHT; board[7][7] = W_ROOK;
    for (c = 0; c < 8; c++) board[6][c] = W_PAWN;
}

/* ========== Piece Drawing ========== */

/* All coordinates relative to square top-left (sx, sy).
   Square center: cx = sx + 13. Pieces drawn in an ~18x20 area. */

static void draw_pawn(int sx, int sy)
{
    int cx = sx + 13;
    /* head */
    gfx_FillCircle_NoClip(cx, sy + 8, 4);
    /* body */
    gfx_FillRectangle_NoClip(cx - 3, sy + 12, 6, 4);
    /* flare */
    gfx_FillRectangle_NoClip(cx - 5, sy + 16, 10, 3);
    /* base */
    gfx_FillRectangle_NoClip(cx - 7, sy + 19, 14, 4);
}

static void draw_rook(int sx, int sy)
{
    int cx = sx + 13;
    /* merlons */
    gfx_FillRectangle_NoClip(cx - 7, sy + 3, 3, 4);
    gfx_FillRectangle_NoClip(cx - 1, sy + 3, 3, 4);
    gfx_FillRectangle_NoClip(cx + 5, sy + 3, 3, 4);
    /* top bar */
    gfx_FillRectangle_NoClip(cx - 7, sy + 7, 15, 2);
    /* body */
    gfx_FillRectangle_NoClip(cx - 5, sy + 9, 11, 8);
    /* bottom bar */
    gfx_FillRectangle_NoClip(cx - 7, sy + 17, 15, 2);
    /* base */
    gfx_FillRectangle_NoClip(cx - 8, sy + 19, 17, 4);
}

static void draw_knight(int sx, int sy)
{
    int cx = sx + 13;
    /* ear */
    gfx_FillRectangle_NoClip(cx - 1, sy + 2, 4, 3);
    /* head */
    gfx_FillRectangle_NoClip(cx - 5, sy + 4, 10, 5);
    /* snout */
    gfx_FillRectangle_NoClip(cx - 7, sy + 6, 4, 3);
    /* neck */
    gfx_FillRectangle_NoClip(cx - 3, sy + 9, 7, 4);
    /* body */
    gfx_FillRectangle_NoClip(cx - 5, sy + 13, 11, 4);
    /* base bar */
    gfx_FillRectangle_NoClip(cx - 7, sy + 17, 15, 2);
    /* base */
    gfx_FillRectangle_NoClip(cx - 8, sy + 19, 17, 4);
}

static void draw_bishop(int sx, int sy)
{
    int cx = sx + 13;
    /* top ball */
    gfx_FillCircle_NoClip(cx, sy + 4, 2);
    /* hat — triangle approximated with narrowing rects */
    gfx_FillRectangle_NoClip(cx - 2, sy + 6, 5, 2);
    gfx_FillRectangle_NoClip(cx - 3, sy + 8, 7, 2);
    gfx_FillRectangle_NoClip(cx - 4, sy + 10, 9, 2);
    gfx_FillRectangle_NoClip(cx - 5, sy + 12, 11, 2);
    /* collar */
    gfx_FillRectangle_NoClip(cx - 6, sy + 14, 13, 2);
    /* stem */
    gfx_FillRectangle_NoClip(cx - 4, sy + 16, 9, 3);
    /* base */
    gfx_FillRectangle_NoClip(cx - 7, sy + 19, 15, 4);
}

static void draw_queen(int sx, int sy)
{
    int cx = sx + 13;
    /* crown points */
    gfx_FillCircle_NoClip(cx - 5, sy + 4, 2);
    gfx_FillCircle_NoClip(cx, sy + 3, 2);
    gfx_FillCircle_NoClip(cx + 5, sy + 4, 2);
    /* upper body — widening */
    gfx_FillRectangle_NoClip(cx - 3, sy + 6, 7, 2);
    gfx_FillRectangle_NoClip(cx - 4, sy + 8, 9, 2);
    gfx_FillRectangle_NoClip(cx - 5, sy + 10, 11, 2);
    /* body */
    gfx_FillRectangle_NoClip(cx - 5, sy + 12, 11, 5);
    /* collar */
    gfx_FillRectangle_NoClip(cx - 6, sy + 17, 13, 2);
    /* base */
    gfx_FillRectangle_NoClip(cx - 7, sy + 19, 15, 4);
}

static void draw_king(int sx, int sy)
{
    int cx = sx + 13;
    /* cross */
    gfx_FillRectangle_NoClip(cx - 1, sy + 2, 3, 7);
    gfx_FillRectangle_NoClip(cx - 4, sy + 4, 9, 3);
    /* head */
    gfx_FillRectangle_NoClip(cx - 5, sy + 9, 11, 3);
    /* body */
    gfx_FillRectangle_NoClip(cx - 4, sy + 12, 9, 5);
    /* collar */
    gfx_FillRectangle_NoClip(cx - 6, sy + 17, 13, 2);
    /* base */
    gfx_FillRectangle_NoClip(cx - 7, sy + 19, 15, 4);
}

static void draw_piece(int8_t piece, int sx, int sy)
{
    int type;
    uint8_t fill_color;

    if (piece == EMPTY) return;

    type = PIECE_TYPE(piece);
    fill_color = PIECE_IS_WHITE(piece) ? PAL_WHITE_PC : PAL_BLACK_PC;

    /* draw outline (slightly offset in 4 directions) */
    gfx_SetColor(PAL_PIECE_OL);
    switch (type)
    {
        case 1: draw_pawn(sx - 1, sy); draw_pawn(sx + 1, sy);
                draw_pawn(sx, sy - 1); draw_pawn(sx, sy + 1); break;
        case 2: draw_knight(sx - 1, sy); draw_knight(sx + 1, sy);
                draw_knight(sx, sy - 1); draw_knight(sx, sy + 1); break;
        case 3: draw_bishop(sx - 1, sy); draw_bishop(sx + 1, sy);
                draw_bishop(sx, sy - 1); draw_bishop(sx, sy + 1); break;
        case 4: draw_rook(sx - 1, sy); draw_rook(sx + 1, sy);
                draw_rook(sx, sy - 1); draw_rook(sx, sy + 1); break;
        case 5: draw_queen(sx - 1, sy); draw_queen(sx + 1, sy);
                draw_queen(sx, sy - 1); draw_queen(sx, sy + 1); break;
        case 6: draw_king(sx - 1, sy); draw_king(sx + 1, sy);
                draw_king(sx, sy - 1); draw_king(sx, sy + 1); break;
    }

    /* draw filled piece */
    gfx_SetColor(fill_color);
    switch (type)
    {
        case 1: draw_pawn(sx, sy); break;
        case 2: draw_knight(sx, sy); break;
        case 3: draw_bishop(sx, sy); break;
        case 4: draw_rook(sx, sy); break;
        case 5: draw_queen(sx, sy); break;
        case 6: draw_king(sx, sy); break;
    }
}

/* ========== Board Drawing ========== */

/* Check if a square is a legal move target.
   Returns 0 = not a target, 1 = quiet move, 2 = capture */
static uint8_t is_legal_target(int r, int c)
{
    uint8_t i;
    for (i = 0; i < legal_target_count; i++)
    {
        if (legal_targets[i].to_row == r && legal_targets[i].to_col == c)
        {
            if ((legal_targets[i].flags & ENGINE_FLAG_CAPTURE) ||
                (legal_targets[i].flags & ENGINE_FLAG_EN_PASSANT))
                return 2;
            return 1;
        }
    }
    return 0;
}

static uint8_t square_bg_color(int r, int c)
{
    int is_light = (r + c) % 2 == 0;

    /* selected piece square */
    if (sel_r >= 0 && r == sel_r && c == sel_c)
        return is_light ? PAL_SEL_LIGHT : PAL_SEL_DARK;

    /* last move squares */
    if (has_last_move)
    {
        if ((r == last_from_r && c == last_from_c) ||
            (r == last_to_r && c == last_to_c))
            return is_light ? PAL_LAST_LIGHT : PAL_LAST_DARK;
    }

    return is_light ? PAL_LIGHT_SQ : PAL_DARK_SQ;
}

static void draw_board(void)
{
    int r, c, sx, sy;

    /* draw squares and pieces */
    for (r = 0; r < 8; r++)
    {
        for (c = 0; c < 8; c++)
        {
            sx = BOARD_X + c * SQ_SIZE;
            sy = BOARD_Y + r * SQ_SIZE;

            /* square background */
            gfx_SetColor(square_bg_color(r, c));
            gfx_FillRectangle_NoClip(sx, sy, SQ_SIZE, SQ_SIZE);

            /* legal move indicators */
            if (sel_r >= 0)
            {
                uint8_t lt = is_legal_target(r, c);
                if (lt)
                {
                    gfx_SetColor(PAL_LEGAL);
                    if (lt == 1 && board[r][c] == EMPTY)
                    {
                        /* quiet move: small dot in center */
                        gfx_FillCircle_NoClip(sx + SQ_SIZE / 2, sy + SQ_SIZE / 2, 4);
                    }
                    else
                    {
                        /* capture (including EP): corner markers */
                        gfx_FillRectangle_NoClip(sx, sy, 4, 4);
                        gfx_FillRectangle_NoClip(sx + SQ_SIZE - 4, sy, 4, 4);
                        gfx_FillRectangle_NoClip(sx, sy + SQ_SIZE - 4, 4, 4);
                        gfx_FillRectangle_NoClip(sx + SQ_SIZE - 4, sy + SQ_SIZE - 4, 4, 4);
                    }
                }
            }

            /* piece */
            if (board[r][c] != EMPTY)
                draw_piece(board[r][c], sx, sy);
        }
    }

    /* cursor — 2px border */
    {
        int cx = BOARD_X + cur_c * SQ_SIZE;
        int cy = BOARD_Y + cur_r * SQ_SIZE;
        gfx_SetColor(PAL_CURSOR);
        /* top */
        gfx_FillRectangle_NoClip(cx, cy, SQ_SIZE, 2);
        /* bottom */
        gfx_FillRectangle_NoClip(cx, cy + SQ_SIZE - 2, SQ_SIZE, 2);
        /* left */
        gfx_FillRectangle_NoClip(cx, cy, 2, SQ_SIZE);
        /* right */
        gfx_FillRectangle_NoClip(cx + SQ_SIZE - 2, cy, 2, SQ_SIZE);
    }

    /* file labels (a-h) */
    gfx_SetTextScale(1, 1);
    gfx_SetTextFGColor(PAL_TEXT);
    for (c = 0; c < 8; c++)
    {
        gfx_SetTextXY(BOARD_X + c * SQ_SIZE + 10, BOARD_Y + BOARD_PX + 2);
        gfx_PrintChar('a' + c);
    }

    /* rank labels (8-1) */
    for (r = 0; r < 8; r++)
    {
        gfx_SetTextXY(BOARD_X - 10, BOARD_Y + r * SQ_SIZE + 9);
        gfx_PrintChar('8' - r);
    }
}

static void draw_sidebar(void)
{
    gfx_SetColor(PAL_SIDEBAR);
    gfx_FillRectangle_NoClip(SIDEBAR_X, 0, SIDEBAR_W, SCREEN_H);

    gfx_SetTextScale(1, 1);

    /* game mode */
    gfx_SetTextFGColor(PAL_TEXT);
    gfx_PrintStringXY(game_mode == MODE_HUMAN ? "vs Human" : "vs CPU", SIDEBAR_X + 4, 6);

    if (game_mode == MODE_COMPUTER)
    {
        gfx_SetTextXY(SIDEBAR_X + 4, 18);
        gfx_PrintString("Lv ");
        gfx_PrintInt(ai_difficulty, 1);
    }

    /* separator */
    gfx_SetColor(PAL_PIECE_OL);
    gfx_HorizLine_NoClip(SIDEBAR_X + 2, 32, SIDEBAR_W - 4);

    /* turn indicator */
    gfx_SetTextScale(1, 1);
    gfx_SetTextFGColor(current_turn == WHITE_TURN ? PAL_WHITE_PC : PAL_TEXT);
    gfx_PrintStringXY(current_turn == WHITE_TURN ? "White" : "Black", SIDEBAR_X + 4, 38);

    /* show check or thinking status */
    if (ai_thinking)
    {
        gfx_SetTextFGColor(PAL_MENU_HL);
        gfx_PrintStringXY("Thinking", SIDEBAR_X + 4, 50);
    }
    else if (engine_in_check())
    {
        gfx_SetTextFGColor(PAL_CURSOR);
        gfx_PrintStringXY("CHECK!", SIDEBAR_X + 4, 50);
    }
    else
    {
        gfx_PrintStringXY("to move", SIDEBAR_X + 4, 50);
    }

    /* selection indicator */
    if (sel_r >= 0)
    {
        gfx_SetColor(PAL_PIECE_OL);
        gfx_HorizLine_NoClip(SIDEBAR_X + 2, 65, SIDEBAR_W - 4);
        gfx_SetTextFGColor(PAL_CURSOR);
        gfx_SetTextXY(SIDEBAR_X + 4, 72);
        gfx_PrintString("Sel: ");
        gfx_PrintChar('a' + sel_c);
        gfx_PrintChar('8' - sel_r);
    }

    /* separator */
    gfx_SetColor(PAL_PIECE_OL);
    gfx_HorizLine_NoClip(SIDEBAR_X + 2, 90, SIDEBAR_W - 4);

    /* controls */
    gfx_SetTextFGColor(PAL_TEXT);
    gfx_PrintStringXY("D-pad", SIDEBAR_X + 2, 96);
    gfx_PrintStringXY(" Move", SIDEBAR_X + 2, 108);
    gfx_PrintStringXY("Enter", SIDEBAR_X + 2, 124);
    gfx_PrintStringXY(" Select", SIDEBAR_X + 2, 136);
    gfx_PrintStringXY("Clear", SIDEBAR_X + 2, 152);
    gfx_PrintStringXY(" Deselect", SIDEBAR_X + 2, 164);
    gfx_PrintStringXY("Mode", SIDEBAR_X + 2, 180);
    gfx_PrintStringXY(" Resign", SIDEBAR_X + 2, 192);
}

/* render board + sidebar to back buffer (no swap) */
static void render_playing(void)
{
    gfx_FillScreen(PAL_BG);
    draw_board();
    draw_sidebar();
}

static void draw_playing(void)
{
    render_playing();
    gfx_SwapDraw();
}

/* ========== Promotion Popup ========== */

static const int8_t promo_pieces_white[4] = { W_QUEEN, W_ROOK, W_BISHOP, W_KNIGHT };
static const int8_t promo_pieces_black[4] = { B_QUEEN, B_ROOK, B_BISHOP, B_KNIGHT };
static const char *promo_labels[4] = { "Q", "R", "B", "N" };

static void draw_promotion(void)
{
    int i, bx, by, px;
    int popup_w = 140;
    int popup_h = 60;
    int popup_x = (SCREEN_W - popup_w) / 2;
    int popup_y = (SCREEN_H - popup_h) / 2;
    const int8_t *pieces;

    /* draw board underneath (no swap — we'll swap after the popup) */
    render_playing();

    /* dim overlay — fill with dark bg */
    gfx_SetColor(PAL_PROMO_BG);
    gfx_FillRectangle_NoClip(popup_x, popup_y, popup_w, popup_h);

    /* border */
    gfx_SetColor(PAL_TEXT);
    gfx_Rectangle_NoClip(popup_x, popup_y, popup_w, popup_h);

    /* title */
    gfx_SetTextScale(1, 1);
    gfx_SetTextFGColor(PAL_TEXT);
    gfx_PrintStringXY("Promote to:", popup_x + 30, popup_y + 4);

    pieces = (current_turn == WHITE_TURN) ? promo_pieces_white : promo_pieces_black;

    /* draw 4 piece options */
    for (i = 0; i < 4; i++)
    {
        bx = popup_x + 6 + i * 34;
        by = popup_y + 18;
        px = bx + 4;

        /* highlight selected */
        if (i == promo_cursor)
        {
            gfx_SetColor(PAL_CURSOR);
            gfx_FillRectangle_NoClip(bx, by, 30, 36);
        }
        else
        {
            gfx_SetColor(PAL_DARK_SQ);
            gfx_FillRectangle_NoClip(bx, by, 30, 36);
        }

        /* draw the piece */
        draw_piece(pieces[i], px, by + 2);

        /* label below (unused, piece is self-explanatory) */
        (void)promo_labels;
    }
}

/* ========== Game Over Screen ========== */

static void draw_gameover(void)
{
    const char *msg;
    const char *reason = "";

    gfx_FillScreen(PAL_BG);

    gfx_SetTextScale(3, 3);
    gfx_SetTextFGColor(PAL_MENU_HL);

    if (winner == WHITE_TURN)
        msg = "White wins!";
    else if (winner == BLACK_TURN)
        msg = "Black wins!";
    else
        msg = "Draw";

    gfx_PrintStringXY(msg, (SCREEN_W - (int)strlen(msg) * 24) / 2, 60);

    /* show reason */
    gfx_SetTextScale(1, 1);
    gfx_SetTextFGColor(PAL_TEXT);

    switch (game_over_reason)
    {
        case ENGINE_STATUS_CHECKMATE: reason = "Checkmate"; break;
        case ENGINE_STATUS_STALEMATE: reason = "Stalemate"; break;
        case ENGINE_STATUS_DRAW_50:   reason = "50-move rule"; break;
        case ENGINE_STATUS_DRAW_REP:  reason = "Repetition"; break;
        case ENGINE_STATUS_DRAW_MAT:  reason = "Insufficient material"; break;
        default:                      reason = "Resignation"; break;
    }
    gfx_PrintStringXY(reason,
        (SCREEN_W - (int)strlen(reason) * 8) / 2, 100);

    gfx_PrintStringXY("Enter: Return to menu", 72, 160);

    gfx_SwapDraw();
}

/* ========== State: Menu ========== */

#define MENU_ITEMS 2

static void draw_menu(void)
{
    int i, y, text_w, bar_x, bar_w;
    const char *items[MENU_ITEMS] = { "Play vs. Human", "Play vs. Computer" };

    gfx_FillScreen(PAL_MENU_BG);

    /* title */
    gfx_SetTextScale(3, 3);
    gfx_SetTextFGColor(PAL_WHITE_PC);
    text_w = gfx_GetStringWidth("CHESS");
    gfx_PrintStringXY("CHESS", (SCREEN_W - text_w) / 2, 40);

    /* decorative line */
    gfx_SetColor(PAL_PIECE_OL);
    gfx_HorizLine_NoClip(80, 72, 160);

    gfx_SetTextScale(2, 2);

    /* find widest item to left-align group centered on screen */
    {
        int max_w = 0;
        int item_x;
        for (i = 0; i < MENU_ITEMS; i++)
        {
            text_w = gfx_GetStringWidth(items[i]);
            if (text_w > max_w) max_w = text_w;
        }
        item_x = (SCREEN_W - max_w) / 2;

        for (i = 0; i < MENU_ITEMS; i++)
        {
            y = 95 + i * 40;
            text_w = gfx_GetStringWidth(items[i]);
            bar_w = max_w + 12;
            bar_x = item_x - 6;

            if (menu_cursor == i)
            {
                gfx_SetColor(PAL_MENU_HL);
                gfx_FillRectangle_NoClip(bar_x, y - 2, bar_w, 22);
                gfx_SetTextFGColor(PAL_MENU_BG);
            }
            else
            {
                gfx_SetTextFGColor(PAL_TEXT);
            }
            gfx_PrintStringXY(items[i], item_x, y);
        }
    }

    /* help */
    gfx_SetTextScale(1, 1);
    gfx_SetTextFGColor(PAL_PIECE_OL);
    gfx_PrintStringXY("arrows: move  enter: select  clear: quit", 12, 222);

    gfx_SwapDraw();
}

/* ========== Engine Helpers ========== */

/* Apply a completed move to the engine and check game status.
   Updates current_turn. Returns 1 if game is over, 0 otherwise. */
static uint8_t apply_engine_move(engine_move_t move)
{
    uint8_t status = engine_make_move(move);

    if (status == ENGINE_STATUS_CHECKMATE)
    {
        /* The side that just moved wins */
        winner = current_turn;
        game_over_reason = status;
        current_turn = -current_turn;
        sel_r = -1; sel_c = -1;
        legal_target_count = 0;
        state = STATE_GAMEOVER;
        return 1;
    }

    if (status >= ENGINE_STATUS_STALEMATE)
    {
        winner = 0;
        game_over_reason = status;
        current_turn = -current_turn;
        sel_r = -1; sel_c = -1;
        legal_target_count = 0;
        state = STATE_GAMEOVER;
        return 1;
    }

    /* switch turn */
    current_turn = -current_turn;
    sel_r = -1; sel_c = -1;
    legal_target_count = 0;

    /* trigger AI if needed */
    if (game_mode == MODE_COMPUTER && current_turn == BLACK_TURN)
        ai_thinking = 1;

    return 0;
}

static void start_game(void)
{
    engine_hooks_t hooks;

    hooks.time_ms = ce_time_ms;
    engine_init(&hooks);
    engine_new_game();

    init_board();
    cur_r = 7; cur_c = 4;
    sel_r = -1; sel_c = -1;
    current_turn = WHITE_TURN;
    has_last_move = 0;
    legal_target_count = 0;
    ai_thinking = 0;
    anim_active = 0;
    game_over_reason = 0;
    state = STATE_PLAYING;
}

static void update_menu(void)
{
    uint8_t new7 = cur_g7 & ~prev_g7;
    uint8_t new6 = cur_g6 & ~prev_g6;
    uint8_t new1 = cur_g1 & ~prev_g1;

    if ((new7 & kb_Down) && menu_cursor < MENU_ITEMS - 1) menu_cursor++;
    if ((new7 & kb_Up) && menu_cursor > 0) menu_cursor--;

    if ((new6 & kb_Enter) || (new1 & kb_2nd))
    {
        if (menu_cursor == 0)
        {
            game_mode = MODE_HUMAN;
            start_game();
        }
        else
        {
            game_mode = MODE_COMPUTER;
            diff_cursor = 4; /* default difficulty 5 */
            state = STATE_DIFFICULTY;
        }
        return;
    }

    if (new6 & kb_Clear)
    {
        running = 0;
        return;
    }

    draw_menu();
}

/* ========== State: Difficulty Select ========== */

static void draw_difficulty(void)
{
    int i, bx, bw;

    gfx_FillScreen(PAL_MENU_BG);

    gfx_SetTextScale(2, 2);
    gfx_SetTextFGColor(PAL_WHITE_PC);
    gfx_PrintStringXY("Difficulty", 72, 40);

    /* 10 boxes horizontally */
    bw = 24;
    for (i = 0; i < 10; i++)
    {
        bx = 8 + i * 28;

        if (i == diff_cursor)
        {
            gfx_SetColor(PAL_MENU_HL);
            gfx_FillRectangle_NoClip(bx, 90, bw, bw);
            gfx_SetTextFGColor(PAL_MENU_BG);
        }
        else
        {
            gfx_SetColor(PAL_PIECE_OL);
            gfx_FillRectangle_NoClip(bx, 90, bw, bw);
            gfx_SetTextFGColor(PAL_TEXT);
        }

        gfx_SetTextScale(2, 2);
        if (i < 9)
            gfx_SetTextXY(bx + 4, 94);
        else
            gfx_SetTextXY(bx + 1, 94);

        gfx_PrintInt(i + 1, i < 9 ? 1 : 2);
    }

    gfx_SetTextScale(1, 1);
    gfx_SetTextFGColor(PAL_PIECE_OL);
    gfx_PrintStringXY("left/right: choose  enter: start  clear: back", 4, 222);

    gfx_SwapDraw();
}

static void update_difficulty(void)
{
    uint8_t new7 = cur_g7 & ~prev_g7;
    uint8_t new6 = cur_g6 & ~prev_g6;
    uint8_t new1 = cur_g1 & ~prev_g1;

    if ((new7 & kb_Right) && diff_cursor < 9) diff_cursor++;
    if ((new7 & kb_Left) && diff_cursor > 0) diff_cursor--;

    if ((new6 & kb_Enter) || (new1 & kb_2nd))
    {
        ai_difficulty = diff_cursor + 1;
        start_game();
        return;
    }

    if (new6 & kb_Clear)
    {
        state = STATE_MENU;
        return;
    }

    draw_difficulty();
}

/* ========== State: Playing ========== */

static void start_move_anim(int from_r, int from_c, int to_r, int to_c)
{
    int dr = from_r - to_r;
    int dc = from_c - to_c;
    int dist, ms;
    if (dr < 0) dr = -dr;
    if (dc < 0) dc = -dc;
    dist = (dr > dc) ? dr : dc; /* Chebyshev distance */
    ms = dist * ANIM_MS_PER_SQ;
    if (ms > ANIM_MAX_MS) ms = ANIM_MAX_MS;
    if (ms < 1) ms = 1; /* guard against zero duration */

    anim_piece = board[from_r][from_c];
    anim_captured = board[to_r][to_c];
    anim_from_r = from_r; anim_from_c = from_c;
    anim_to_r = to_r; anim_to_c = to_c;
    anim_duration = (clock_t)(CLOCKS_PER_SEC * ms / 1000);
    anim_start = clock();
    anim_active = 1;

    /* remove piece from origin during animation */
    board[from_r][from_c] = EMPTY;
    /* remove captured piece so it doesn't draw under the sliding piece */
    board[to_r][to_c] = EMPTY;

    /* for EP, also remove the captured pawn */
    if (pending_effects.has_ep_capture)
        board[pending_effects.ep_capture_row][pending_effects.ep_capture_col] = EMPTY;

    /* for castling, move the rook immediately (so it appears in new position during king slide) */
    if (pending_effects.has_rook_move)
    {
        int8_t rook = board[pending_effects.rook_from_row][pending_effects.rook_from_col];
        board[pending_effects.rook_from_row][pending_effects.rook_from_col] = EMPTY;
        board[pending_effects.rook_to_row][pending_effects.rook_to_col] = rook;
    }
}

static void finish_move(void)
{
    int8_t piece = anim_piece;
    int to_r = anim_to_r, to_c = anim_to_c;
    int is_ai_move;
    int prev_has_last = has_last_move;
    int prev_from_r = last_from_r, prev_from_c = last_from_c;
    int prev_to_r = last_to_r, prev_to_c = last_to_c;

    anim_active = 0;
    board[to_r][to_c] = piece;

    /* record last move for highlighting */
    last_from_r = anim_from_r; last_from_c = anim_from_c;
    last_to_r = to_r; last_to_c = to_c;
    has_last_move = 1;

    /* check pawn promotion */
    is_ai_move = (game_mode == MODE_COMPUTER && current_turn == BLACK_TURN);

    if (pending_move.flags & ENGINE_FLAG_PROMOTION)
    {
        if (is_ai_move)
        {
            /* AI promotion — apply the piece from engine move flags */
            uint8_t pt = pending_move.flags & ENGINE_FLAG_PROMO_MASK;
            if (pt == ENGINE_FLAG_PROMO_Q) board[to_r][to_c] = B_QUEEN;
            else if (pt == ENGINE_FLAG_PROMO_R) board[to_r][to_c] = B_ROOK;
            else if (pt == ENGINE_FLAG_PROMO_B) board[to_r][to_c] = B_BISHOP;
            else board[to_r][to_c] = B_KNIGHT;
            /* fall through to apply_engine_move */
        }
        else
        {
            /* Human promotion — show popup, defer engine move */
            promo_saved_has_last = prev_has_last;
            promo_saved_from_r = prev_from_r;
            promo_saved_from_c = prev_from_c;
            promo_saved_to_r = prev_to_r;
            promo_saved_to_c = prev_to_c;
            promo_r = to_r;
            promo_c = to_c;
            promo_from_r = anim_from_r;
            promo_from_c = anim_from_c;
            promo_captured = anim_captured;
            promo_cursor = 0;
            state = STATE_PROMOTION;
            return;
        }
    }

    apply_engine_move(pending_move);
}

static void draw_anim_frame(void)
{
    clock_t elapsed = clock() - anim_start;
    clock_t duration = anim_duration;
    int from_px, from_py, to_px, to_py;
    int cur_px, cur_py;

    if (elapsed >= duration)
    {
        finish_move();
        return;
    }

    /* compute interpolated pixel position */
    from_px = BOARD_X + anim_from_c * SQ_SIZE;
    from_py = BOARD_Y + anim_from_r * SQ_SIZE;
    to_px = BOARD_X + anim_to_c * SQ_SIZE;
    to_py = BOARD_Y + anim_to_r * SQ_SIZE;

    cur_px = from_px + (int)((long)(to_px - from_px) * elapsed / duration);
    cur_py = from_py + (int)((long)(to_py - from_py) * elapsed / duration);

    /* draw board without the moving piece (already removed from board[]) */
    render_playing();

    /* draw the sliding piece on top */
    draw_piece(anim_piece, cur_px, cur_py);

    gfx_SwapDraw();
}

/* Find a matching engine move from legal_targets for a target square */
static int find_legal_target(int to_r, int to_c, engine_move_t *out)
{
    uint8_t i;
    for (i = 0; i < legal_target_count; i++)
    {
        if (legal_targets[i].to_row == to_r && legal_targets[i].to_col == to_c)
        {
            *out = legal_targets[i];
            return 1;
        }
    }
    return 0;
}

static void update_playing(void)
{
    uint8_t new7 = cur_g7 & ~prev_g7;
    uint8_t new6 = cur_g6 & ~prev_g6;
    uint8_t new1 = cur_g1 & ~prev_g1;
    int8_t target;

    /* if animation is running, just draw it and return */
    if (anim_active)
    {
        draw_anim_frame();
        return;
    }

    /* AI thinking phase */
    if (ai_thinking)
    {
        if (ai_thinking == 1)
        {
            /* first frame: draw "Thinking..." on sidebar and return */
            ai_thinking = 2;
            draw_playing();
            return;
        }

        /* second frame: compute AI move (blocking) */
        {
            static const uint8_t depth_table[10] =
                { 1, 1, 2, 2, 3, 3, 4, 4, 5, 5 };
            static const uint32_t time_table[10] =
                { 1000, 2000, 3000, 5000, 8000, 10000, 15000, 20000, 25000, 30000 };
            uint8_t depth = depth_table[ai_difficulty - 1];
            uint32_t time_ms = time_table[ai_difficulty - 1];
            engine_move_t ai_move;

            ai_move = engine_think(depth, time_ms);

            if (ai_move.from_row == ENGINE_SQ_NONE)
            {
                /* no legal move — game should be over */
                uint8_t status = engine_get_status();
                if (status == ENGINE_STATUS_CHECKMATE)
                    winner = WHITE_TURN;
                else
                    winner = 0;
                game_over_reason = status ? status : ENGINE_STATUS_STALEMATE;
                state = STATE_GAMEOVER;
                ai_thinking = 0;
                return;
            }

            /* prepare and animate the AI move */
            pending_move = ai_move;
            engine_get_move_effects(ai_move, &pending_effects);
            start_move_anim(ai_move.from_row, ai_move.from_col,
                            ai_move.to_row, ai_move.to_col);
            ai_thinking = 0;
        }
        return;
    }

    /* cursor movement */
    if (new7 & kb_Up)    cur_r = (cur_r > 0) ? cur_r - 1 : 0;
    if (new7 & kb_Down)  cur_r = (cur_r < 7) ? cur_r + 1 : 7;
    if (new7 & kb_Left)  cur_c = (cur_c > 0) ? cur_c - 1 : 0;
    if (new7 & kb_Right) cur_c = (cur_c < 7) ? cur_c + 1 : 7;

    /* enter/2nd — select or move */
    if ((new6 & kb_Enter) || (new1 & kb_2nd))
    {
        if (sel_r < 0)
        {
            /* nothing selected — try to select a piece */
            target = board[cur_r][cur_c];
            if (target != EMPTY)
            {
                /* must be current player's piece */
                if ((current_turn == WHITE_TURN && PIECE_IS_WHITE(target)) ||
                    (current_turn == BLACK_TURN && !PIECE_IS_WHITE(target)))
                {
                    sel_r = cur_r;
                    sel_c = cur_c;
                    /* compute legal moves for this piece */
                    legal_target_count = engine_get_moves_from(
                        (uint8_t)cur_r, (uint8_t)cur_c, legal_targets, 64);
                }
            }
        }
        else
        {
            /* piece already selected — try to move */
            if (cur_r == sel_r && cur_c == sel_c)
            {
                /* deselect if clicking same square */
                sel_r = -1; sel_c = -1;
                legal_target_count = 0;
            }
            else
            {
                /* check if clicking own piece — reselect */
                target = board[cur_r][cur_c];
                if (target != EMPTY &&
                    ((current_turn == WHITE_TURN && PIECE_IS_WHITE(target)) ||
                     (current_turn == BLACK_TURN && !PIECE_IS_WHITE(target))))
                {
                    sel_r = cur_r;
                    sel_c = cur_c;
                    /* recompute legal moves for new piece */
                    legal_target_count = engine_get_moves_from(
                        (uint8_t)cur_r, (uint8_t)cur_c, legal_targets, 64);
                }
                else
                {
                    /* try to move to this square */
                    engine_move_t move;
                    if (find_legal_target(cur_r, cur_c, &move))
                    {
                        /* legal move — prepare and animate */
                        pending_move = move;
                        engine_get_move_effects(move, &pending_effects);
                        start_move_anim(sel_r, sel_c, cur_r, cur_c);
                    }
                    /* if not legal, do nothing (ignore the click) */
                }
            }
        }
    }

    /* clear — deselect */
    if (new6 & kb_Clear)
    {
        if (sel_r >= 0)
        {
            sel_r = -1; sel_c = -1;
            legal_target_count = 0;
        }
        else
        {
            /* no selection — resign */
            winner = -current_turn;
            game_over_reason = 0; /* resignation */
            state = STATE_GAMEOVER;
            return;
        }
    }

    /* mode button — resign */
    if (new1 & kb_Mode)
    {
        winner = -current_turn;
        game_over_reason = 0; /* resignation */
        state = STATE_GAMEOVER;
        return;
    }

    if (state == STATE_PLAYING)
        draw_playing();
}

/* ========== State: Promotion ========== */

static void update_promotion(void)
{
    uint8_t new7 = cur_g7 & ~prev_g7;
    uint8_t new6 = cur_g6 & ~prev_g6;
    uint8_t new1 = cur_g1 & ~prev_g1;
    const int8_t *pieces;

    static const uint8_t promo_flags[4] = {
        ENGINE_FLAG_PROMO_Q, ENGINE_FLAG_PROMO_R,
        ENGINE_FLAG_PROMO_B, ENGINE_FLAG_PROMO_N
    };

    if ((new7 & kb_Right) && promo_cursor < 3) promo_cursor++;
    if ((new7 & kb_Left) && promo_cursor > 0) promo_cursor--;

    if ((new6 & kb_Enter) || (new1 & kb_2nd))
    {
        pieces = (current_turn == WHITE_TURN) ? promo_pieces_white : promo_pieces_black;
        board[promo_r][promo_c] = pieces[promo_cursor];

        /* preserve capture flag from original move, set promotion type */
        pending_move.flags = (pending_move.flags & ENGINE_FLAG_CAPTURE)
                           | ENGINE_FLAG_PROMOTION | promo_flags[promo_cursor];

        /* apply the move to the engine and check status */
        if (apply_engine_move(pending_move))
            return; /* game over */

        state = STATE_PLAYING;
        return;
    }

    /* clear — cancel promotion, undo the move */
    if (new6 & kb_Clear)
    {
        int8_t pawn = (current_turn == WHITE_TURN) ? W_PAWN : B_PAWN;
        board[promo_from_r][promo_from_c] = pawn;
        board[promo_r][promo_c] = promo_captured;
        /* restore previous last-move highlight */
        has_last_move = promo_saved_has_last;
        last_from_r = promo_saved_from_r;
        last_from_c = promo_saved_from_c;
        last_to_r = promo_saved_to_r;
        last_to_c = promo_saved_to_c;
        sel_r = -1; sel_c = -1;
        legal_target_count = 0;
        state = STATE_PLAYING;
        return;
    }

    draw_promotion();
    gfx_SwapDraw();
}

/* ========== State: Game Over ========== */

static void update_gameover(void)
{
    uint8_t new6 = cur_g6 & ~prev_g6;
    uint8_t new1 = cur_g1 & ~prev_g1;

    if ((new6 & kb_Enter) || (new1 & kb_2nd) || (new6 & kb_Clear))
    {
        state = STATE_MENU;
        menu_cursor = 0;
        return;
    }

    draw_gameover();
}

/* ========== Main ========== */

int main(void)
{
    clock_t frame_start;

    gfx_Begin();
    gfx_SetDrawBuffer();
    setup_palette();

    srand(clock());
    state = STATE_MENU;
    menu_cursor = 0;
    running = 1;

    do
    {
        frame_start = clock();
        kb_Scan();

        cur_g1 = kb_Data[1];
        cur_g6 = kb_Data[6];
        cur_g7 = kb_Data[7];

        switch (state)
        {
            case STATE_MENU:       update_menu();       break;
            case STATE_DIFFICULTY: update_difficulty();  break;
            case STATE_PLAYING:    update_playing();     break;
            case STATE_PROMOTION:  update_promotion();   break;
            case STATE_GAMEOVER:   update_gameover();    break;
        }

        prev_g1 = cur_g1;
        prev_g6 = cur_g6;
        prev_g7 = cur_g7;

        while (clock() - frame_start < FRAME_TIME)
            ;

    } while (running);

    gfx_End();
    return 0;
}
