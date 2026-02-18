#include <graphx.h>
#include <keypadc.h>
#include <sys/util.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <fileioc.h>
#include "engine.h"
#include "book.h"
#include "piece_sprites.h"

#define VERSION "0.3"

#if BOOK_TIER == 0
#define BOOK_TAG ""
#elif BOOK_TIER == 1
#define BOOK_TAG " S"
#elif BOOK_TIER == 2
#define BOOK_TAG " M"
#elif BOOK_TIER == 3
#define BOOK_TAG " L"
#elif BOOK_TIER == 4
#define BOOK_TAG " XL"
#elif BOOK_TIER == 5
#define BOOK_TAG " XXL"
#endif

#define VERSION_STR "v" VERSION BOOK_TAG

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
#define PAL_CUR_LEGAL   17
#define PAL_CUR_ILLEGAL 18
#define PAL_SUBTEXT     19

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

#undef  PIECE_TYPE
#define PIECE_TYPE(p)  ((p) > 0 ? (p) : -(p))
#define PIECE_IS_WHITE(p) ((p) > 0)

#define WHITE_TURN  1
#define BLACK_TURN (-1)

/* ========== Game States ========== */

typedef enum {
    STATE_MENU,
    STATE_DIFFICULTY_SELECT,
    STATE_COLOR_SELECT,
    STATE_PLAYING,
    STATE_PROMOTION,
    STATE_GAMEOVER
} game_state_t;

#define MODE_HUMAN    0
#define MODE_COMPUTER 1

/* ========== Global State ========== */

static game_state_t state;
static int running;
static unsigned int frame_count; /* entropy source for RNG seeding */
static unsigned int game_number; /* increments each game start */

/* board — board[row][col], row 0 = top of screen (black back rank) */
static int8_t board[8][8];

/* cursor & selection */
static int cur_r, cur_c;
static int sel_r, sel_c;       /* -1 = nothing selected */
static int current_turn;       /* WHITE_TURN or BLACK_TURN */

/* game mode */
static int game_mode;
static int player_color;       /* WHITE_TURN or BLACK_TURN */
static int board_flipped;      /* 1 when playing as black */

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
static int difficulty_cursor;  /* 0=Easy, 1=Medium, 2=Hard, 3=Expert, 4=Master */
static uint32_t think_time_ms; /* AI think time (fallback limit) */
static int color_cursor;       /* 0=White, 1=Black, 2=Random */

/* keyboard */
static uint8_t cur_g1, cur_g6, cur_g7;
static uint8_t prev_g1, prev_g6, prev_g7;

/* dirty flag — skip redraw when nothing changed */
static int screen_dirty;

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
    gfx_palette[PAL_WHITE_PC]  = gfx_RGBTo1555(242, 239, 232);
    gfx_palette[PAL_BLACK_PC]  = gfx_RGBTo1555(20, 22, 26);
    gfx_palette[PAL_PIECE_OL]  = gfx_RGBTo1555(70, 70, 74);
    gfx_palette[PAL_CURSOR]    = gfx_RGBTo1555(50, 120, 255);
    gfx_palette[PAL_TEXT]      = gfx_RGBTo1555(220, 220, 220);
    gfx_palette[PAL_MENU_HL]   = gfx_RGBTo1555(70, 130, 230);
    gfx_palette[PAL_MENU_BG]   = gfx_RGBTo1555(20, 20, 30);
    gfx_palette[PAL_PROMO_BG]  = gfx_RGBTo1555(50, 50, 60);
    gfx_palette[PAL_SIDEBAR]   = gfx_RGBTo1555(44, 44, 44);
    gfx_palette[PAL_LEGAL]     = gfx_RGBTo1555(100, 180, 100);
    gfx_palette[PAL_CUR_LEGAL]  = gfx_RGBTo1555(80, 200, 80);
    gfx_palette[PAL_CUR_ILLEGAL]= gfx_RGBTo1555(220, 60, 60);
    gfx_palette[PAL_SUBTEXT]    = gfx_RGBTo1555(150, 150, 150);
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

/* Pre-rendered piece sprites: 6 types x 2 colors (white, black) = 12 */
static uint8_t piece_spr_data[12][2 + PIECE_SPR_W * PIECE_SPR_H];

static void prerender_pieces(void)
{
    int type, color, row, col, idx;
    uint8_t fill;
    gfx_sprite_t *spr;

    for (type = 0; type < 6; type++)
    {
        for (color = 0; color < 2; color++)
        {
            idx = type * 2 + color;
            fill = color ? PAL_BLACK_PC : PAL_WHITE_PC;
            spr = (gfx_sprite_t *)piece_spr_data[idx];
            spr->width = PIECE_SPR_W;
            spr->height = PIECE_SPR_H;

            /* base pass: fill + outline */
            for (row = 0; row < PIECE_SPR_H; row++)
            {
                for (col = 0; col < PIECE_SPR_W; col++)
                {
                    uint8_t v = piece_sprites[type][row][col];
                    uint8_t *px = &spr->data[row * PIECE_SPR_W + col];
                    if (v == 1)       *px = fill;
                    else if (v == 2)  *px = PAL_PIECE_OL;
                    else              *px = 0; /* transparent */
                }
            }

            /* contour reinforcement for crisp edges */
            for (row = 0; row < PIECE_SPR_H; row++)
            {
                for (col = 0; col < PIECE_SPR_W; col++)
                {
                    int up_open, down_open;
                    if (piece_sprites[type][row][col] != 1) continue;

                    up_open = (row == 0) || (piece_sprites[type][row - 1][col] == 0);
                    down_open = (row + 1 >= PIECE_SPR_H) || (piece_sprites[type][row + 1][col] == 0);

                    if (up_open || down_open)
                    {
                        spr->data[row * PIECE_SPR_W + col] = PAL_PIECE_OL;
                        if (up_open && row > 0)
                            spr->data[(row - 1) * PIECE_SPR_W + col] = PAL_PIECE_OL;
                        if (down_open && row + 1 < PIECE_SPR_H)
                            spr->data[(row + 1) * PIECE_SPR_W + col] = PAL_PIECE_OL;
                    }
                }
            }
        }
    }
}

static void draw_piece(int8_t piece, int sx, int sy)
{
    int idx;
    if (piece == EMPTY) return;

    idx = (PIECE_TYPE(piece) - 1) * 2 + (PIECE_IS_WHITE(piece) ? 0 : 1);
    gfx_TransparentSprite_NoClip((gfx_sprite_t *)piece_spr_data[idx],
                                  sx + PIECE_SPR_XOFF, sy + PIECE_SPR_YOFF);
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

/* Draw a single square at logical position (lr, lc) */
static void draw_square(int lr, int lc)
{
    int dr = board_flipped ? 7 - lr : lr;
    int dc = board_flipped ? 7 - lc : lc;
    int sx = BOARD_X + dc * SQ_SIZE;
    int sy = BOARD_Y + dr * SQ_SIZE;

    /* square background */
    gfx_SetColor(square_bg_color(lr, lc));
    gfx_FillRectangle_NoClip(sx, sy, SQ_SIZE, SQ_SIZE);

    /* legal move indicators */
    if (sel_r >= 0)
    {
        uint8_t lt = is_legal_target(lr, lc);
        if (lt)
        {
            gfx_SetColor(PAL_LEGAL);
            if (lt == 1 && board[lr][lc] == EMPTY)
            {
                gfx_FillCircle_NoClip(sx + SQ_SIZE / 2, sy + SQ_SIZE / 2, 4);
            }
            else
            {
                gfx_FillRectangle_NoClip(sx, sy, 4, 4);
                gfx_FillRectangle_NoClip(sx + SQ_SIZE - 4, sy, 4, 4);
                gfx_FillRectangle_NoClip(sx, sy + SQ_SIZE - 4, 4, 4);
                gfx_FillRectangle_NoClip(sx + SQ_SIZE - 4, sy + SQ_SIZE - 4, 4, 4);
            }
        }
    }

    /* piece */
    if (board[lr][lc] != EMPTY)
        draw_piece(board[lr][lc], sx, sy);
}

/* Draw cursor border at current cursor position */
static void draw_cursor_border(void)
{
    int dcr = board_flipped ? 7 - cur_r : cur_r;
    int dcc = board_flipped ? 7 - cur_c : cur_c;
    int cx = BOARD_X + dcc * SQ_SIZE;
    int cy = BOARD_Y + dcr * SQ_SIZE;
    uint8_t cur_color = PAL_CURSOR;
    if (sel_r >= 0 && !(cur_r == sel_r && cur_c == sel_c))
        cur_color = is_legal_target(cur_r, cur_c) ? PAL_CUR_LEGAL : PAL_CUR_ILLEGAL;
    gfx_SetColor(cur_color);
    gfx_FillRectangle_NoClip(cx, cy, SQ_SIZE, 2);
    gfx_FillRectangle_NoClip(cx, cy + SQ_SIZE - 2, SQ_SIZE, 2);
    gfx_FillRectangle_NoClip(cx, cy, 2, SQ_SIZE);
    gfx_FillRectangle_NoClip(cx + SQ_SIZE - 2, cy, 2, SQ_SIZE);
}

static void draw_board(void)
{
    int dr, dc, lr, lc;

    for (dr = 0; dr < 8; dr++)
    {
        for (dc = 0; dc < 8; dc++)
        {
            lr = board_flipped ? 7 - dr : dr;
            lc = board_flipped ? 7 - dc : dc;
            draw_square(lr, lc);
        }
    }

    draw_cursor_border();

    /* file labels */
    gfx_SetTextScale(1, 1);
    gfx_SetTextFGColor(PAL_TEXT);
    for (dc = 0; dc < 8; dc++)
    {
        lc = board_flipped ? 7 - dc : dc;
        gfx_SetTextXY(BOARD_X + dc * SQ_SIZE + 10, BOARD_Y + BOARD_PX + 2);
        gfx_PrintChar('a' + lc);
    }

    /* rank labels */
    for (dr = 0; dr < 8; dr++)
    {
        lr = board_flipped ? 7 - dr : dr;
        gfx_SetTextXY(BOARD_X - 10, BOARD_Y + dr * SQ_SIZE + 9);
        gfx_PrintChar('8' - lr);
    }
}

/* Partial redraw: only update old and new cursor squares */
static void draw_cursor_move(int old_r, int old_c)
{
    /* copy front buffer to back buffer so we have the current display */
    gfx_Blit(gfx_screen);

    /* redraw old cursor square (removes cursor border) */
    draw_square(old_r, old_c);

    /* redraw new cursor square */
    draw_square(cur_r, cur_c);

    /* draw cursor at new position */
    draw_cursor_border();

    gfx_SwapDraw();
}

static void draw_sidebar(void)
{
    gfx_SetColor(PAL_SIDEBAR);
    gfx_FillRectangle_NoClip(SIDEBAR_X, 0, SIDEBAR_W, SCREEN_H);

    gfx_SetTextScale(1, 1);

    /* game mode */
    gfx_SetTextFGColor(PAL_TEXT);
    if (game_mode == MODE_HUMAN) {
        gfx_PrintStringXY("vs. Human", SIDEBAR_X + 4, 6);
    } else {
        char cpu_buf[12];
        sprintf(cpu_buf, "vs. CPU %d", difficulty_cursor + 1);
        gfx_PrintStringXY(cpu_buf, SIDEBAR_X + 4, 6);
    }

    /* separator */
    gfx_SetColor(PAL_PIECE_OL);
    gfx_HorizLine_NoClip(SIDEBAR_X + 2, 20, SIDEBAR_W - 4);

    /* turn indicator */
    gfx_SetTextScale(1, 1);
    gfx_SetTextFGColor(current_turn == WHITE_TURN ? PAL_WHITE_PC : PAL_TEXT);
    gfx_PrintStringXY(current_turn == WHITE_TURN ? "White" : "Black", SIDEBAR_X + 4, 26);

    /* show check or thinking status */
    if (ai_thinking)
    {
        gfx_SetTextFGColor(PAL_MENU_HL);
        gfx_PrintStringXY("Thinking", SIDEBAR_X + 4, 38);
    }
    else if (engine_in_check())
    {
        gfx_SetTextFGColor(PAL_CURSOR);
        gfx_PrintStringXY("CHECK!", SIDEBAR_X + 4, 38);
    }
    else
    {
        gfx_PrintStringXY("to move", SIDEBAR_X + 4, 38);
    }

    /* selection indicator */
    if (sel_r >= 0)
    {
        gfx_SetColor(PAL_PIECE_OL);
        gfx_HorizLine_NoClip(SIDEBAR_X + 2, 53, SIDEBAR_W - 4);
        gfx_SetTextFGColor(PAL_CURSOR);
        gfx_SetTextXY(SIDEBAR_X + 4, 60);
        gfx_PrintString("Sel: ");
        gfx_PrintChar('a' + sel_c);
        gfx_PrintChar('8' - sel_r);
    }

    /* separator */
    gfx_SetColor(PAL_PIECE_OL);
    gfx_HorizLine_NoClip(SIDEBAR_X + 2, 78, SIDEBAR_W - 4);

    /* controls */
    gfx_SetTextFGColor(PAL_TEXT);
    gfx_PrintStringXY("D-pad", SIDEBAR_X + 2, 84);
    gfx_PrintStringXY(" Move", SIDEBAR_X + 2, 96);
    gfx_PrintStringXY("Enter", SIDEBAR_X + 2, 112);
    gfx_PrintStringXY(" Select", SIDEBAR_X + 2, 124);
    gfx_PrintStringXY("Clear", SIDEBAR_X + 2, 140);
    gfx_PrintStringXY(" Deselect", SIDEBAR_X + 2, 152);
    gfx_PrintStringXY("Mode", SIDEBAR_X + 2, 168);
    gfx_PrintStringXY(" Resign", SIDEBAR_X + 2, 180);

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
    int box_x, box_y, box_w, box_h;
    int msg_w, reason_w, hint_w;

    /* draw the board with final position */
    render_playing();

    /* determine text */
    if (winner == WHITE_TURN)
        msg = "White wins!";
    else if (winner == BLACK_TURN)
        msg = "Black wins!";
    else
        msg = "Draw";

    switch (game_over_reason)
    {
        case ENGINE_STATUS_CHECKMATE: reason = "Checkmate"; break;
        case ENGINE_STATUS_STALEMATE: reason = "Stalemate"; break;
        case ENGINE_STATUS_DRAW_50:   reason = "50-move rule"; break;
        case ENGINE_STATUS_DRAW_REP:  reason = "Repetition"; break;
        case ENGINE_STATUS_DRAW_MAT:  reason = "Insufficient material"; break;
        default:                      reason = "Resignation"; break;
    }

    /* measure text widths */
    gfx_SetTextScale(2, 2);
    msg_w = gfx_GetStringWidth(msg);
    gfx_SetTextScale(1, 1);
    reason_w = gfx_GetStringWidth(reason);
    hint_w = gfx_GetStringWidth("Enter: menu");

    /* compute overlay box size */
    box_w = msg_w;
    if (reason_w > box_w) box_w = reason_w;
    if (hint_w > box_w) box_w = hint_w;
    box_w += 24;
    box_h = 60;
    box_x = (SIDEBAR_X - box_w) / 2;
    box_y = (SCREEN_H - box_h) / 2;

    /* draw overlay box */
    gfx_SetColor(PAL_PROMO_BG);
    gfx_FillRectangle_NoClip(box_x, box_y, box_w, box_h);
    gfx_SetColor(PAL_PIECE_OL);
    gfx_Rectangle_NoClip(box_x, box_y, box_w, box_h);

    /* winner text */
    gfx_SetTextScale(2, 2);
    gfx_SetTextFGColor(PAL_MENU_HL);
    gfx_PrintStringXY(msg, box_x + (box_w - msg_w) / 2, box_y + 6);

    /* reason */
    gfx_SetTextScale(1, 1);
    gfx_SetTextFGColor(PAL_TEXT);
    gfx_PrintStringXY(reason, box_x + (box_w - reason_w) / 2, box_y + 28);

    /* hint */
    gfx_SetTextFGColor(PAL_PIECE_OL);
    gfx_PrintStringXY("Enter: menu", box_x + (box_w - hint_w) / 2, box_y + 44);

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

    /* version + book tag */
    gfx_SetTextScale(1, 1);
    gfx_SetTextFGColor(PAL_PIECE_OL);
    text_w = gfx_GetStringWidth(VERSION_STR);
    gfx_PrintStringXY(VERSION_STR, SCREEN_W - text_w - 4, 4);

    /* help */
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
    if (game_mode == MODE_COMPUTER && current_turn != player_color)
        ai_thinking = 1;

    return 0;
}

static void start_game(void)
{
    engine_hooks_t hooks;

    game_number++;
    book_random_seed = (uint32_t)clock() ^ (frame_count * 2654435761u) ^ (game_number * 1103515245u);
    srand((unsigned int)book_random_seed);

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

    /* set player color based on color_cursor selection */
    if (game_mode == MODE_COMPUTER) {
        if (color_cursor == 0)      player_color = WHITE_TURN;
        else if (color_cursor == 1) player_color = BLACK_TURN;
        else                        player_color = (rand() & 1) ? WHITE_TURN : BLACK_TURN;
    } else {
        player_color = WHITE_TURN;
    }
    board_flipped = (player_color == BLACK_TURN);

    /* if player is black, AI moves first */
    if (game_mode == MODE_COMPUTER && player_color == BLACK_TURN)
        ai_thinking = 1;

    screen_dirty = 1;
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
            difficulty_cursor = 1; /* default to Medium */
            state = STATE_DIFFICULTY_SELECT;
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

#define DIFFICULTY_ITEMS 5

static const char *difficulty_labels[DIFFICULTY_ITEMS] = {
    "Easy", "Medium", "Hard", "Expert", "Master"
};
static const char *difficulty_subtexts[DIFFICULTY_ITEMS] = {
    "~1s think time", "~5s think time", "~10s think time", "~15s think time", "~30s think time"
};
static const uint32_t difficulty_times[DIFFICULTY_ITEMS] = {
    1000, 5000, 10000, 15000, 30000
};
/* Node limits removed — difficulty is purely time-based */

static void draw_difficulty_select(void)
{
    int i, y, text_w, bar_x, bar_w;

    gfx_FillScreen(PAL_MENU_BG);

    gfx_SetTextScale(2, 2);
    gfx_SetTextFGColor(PAL_WHITE_PC);
    text_w = gfx_GetStringWidth("Difficulty");
    gfx_PrintStringXY("Difficulty", (SCREEN_W - text_w) / 2, 24);

    /* decorative line */
    gfx_SetColor(PAL_PIECE_OL);
    gfx_HorizLine_NoClip(80, 48, 160);

    gfx_SetTextScale(2, 2);

    {
        int max_w = 0;
        int item_x;
        for (i = 0; i < DIFFICULTY_ITEMS; i++)
        {
            text_w = gfx_GetStringWidth(difficulty_labels[i]);
            if (text_w > max_w) max_w = text_w;
        }
        item_x = (SCREEN_W - max_w) / 2;

        for (i = 0; i < DIFFICULTY_ITEMS; i++)
        {
            y = 58 + i * 30;
            bar_w = max_w + 12;
            bar_x = item_x - 6;

            if (difficulty_cursor == i)
            {
                gfx_SetColor(PAL_MENU_HL);
                gfx_FillRectangle_NoClip(bar_x, y - 2, bar_w, 26);
                gfx_SetTextScale(2, 2);
                gfx_SetTextFGColor(PAL_MENU_BG);
            }
            else
            {
                gfx_SetTextScale(2, 2);
                gfx_SetTextFGColor(PAL_TEXT);
            }
            gfx_PrintStringXY(difficulty_labels[i], item_x, y);

            /* subtext: think time */
            gfx_SetTextScale(1, 1);
            if (difficulty_cursor == i)
                gfx_SetTextFGColor(PAL_MENU_BG);
            else
                gfx_SetTextFGColor(PAL_SUBTEXT);
            gfx_PrintStringXY(difficulty_subtexts[i], item_x, y + 16);
        }
    }

    gfx_SetTextScale(1, 1);
    gfx_SetTextFGColor(PAL_PIECE_OL);
    gfx_PrintStringXY("arrows: move  enter: select  clear: back", 12, 222);

    gfx_SwapDraw();
}

static void update_difficulty_select(void)
{
    uint8_t new7 = cur_g7 & ~prev_g7;
    uint8_t new6 = cur_g6 & ~prev_g6;
    uint8_t new1 = cur_g1 & ~prev_g1;

    if ((new7 & kb_Down) && difficulty_cursor < DIFFICULTY_ITEMS - 1) difficulty_cursor++;
    if ((new7 & kb_Up) && difficulty_cursor > 0) difficulty_cursor--;

    if ((new6 & kb_Enter) || (new1 & kb_2nd))
    {
        think_time_ms = difficulty_times[difficulty_cursor];
        engine_set_max_nodes(difficulty_cursor == 0 ? 250 : 30000);
        engine_set_use_book(1);
        engine_set_book_max_ply(difficulty_cursor == 0 ? 3 : 0);
        engine_set_eval_noise(0);
        engine_set_move_variance(difficulty_cursor == 0 ? 30 : 15);
        color_cursor = 2; /* default to Random */
        state = STATE_COLOR_SELECT;
        return;
    }

    if (new6 & kb_Clear)
    {
        state = STATE_MENU;
        return;
    }

    draw_difficulty_select();
}

/* ========== State: Color Select ========== */

#define COLOR_ITEMS 3

static void draw_color_select(void)
{
    int i, y, text_w, bar_x, bar_w;
    const char *items[COLOR_ITEMS] = { "White", "Black", "Random" };

    gfx_FillScreen(PAL_MENU_BG);

    gfx_SetTextScale(2, 2);
    gfx_SetTextFGColor(PAL_WHITE_PC);
    text_w = gfx_GetStringWidth("Play as...");
    gfx_PrintStringXY("Play as...", (SCREEN_W - text_w) / 2, 40);

    /* decorative line */
    gfx_SetColor(PAL_PIECE_OL);
    gfx_HorizLine_NoClip(80, 62, 160);

    gfx_SetTextScale(2, 2);

    {
        int max_w = 0;
        int item_x;
        for (i = 0; i < COLOR_ITEMS; i++)
        {
            text_w = gfx_GetStringWidth(items[i]);
            if (text_w > max_w) max_w = text_w;
        }
        item_x = (SCREEN_W - max_w) / 2;

        for (i = 0; i < COLOR_ITEMS; i++)
        {
            y = 80 + i * 36;
            bar_w = max_w + 12;
            bar_x = item_x - 6;

            if (color_cursor == i)
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

    gfx_SetTextScale(1, 1);
    gfx_SetTextFGColor(PAL_PIECE_OL);
    gfx_PrintStringXY("arrows: move  enter: start  clear: back", 16, 222);

    gfx_SwapDraw();
}

static void update_color_select(void)
{
    uint8_t new7 = cur_g7 & ~prev_g7;
    uint8_t new6 = cur_g6 & ~prev_g6;
    uint8_t new1 = cur_g1 & ~prev_g1;

    if ((new7 & kb_Down) && color_cursor < COLOR_ITEMS - 1) color_cursor++;
    if ((new7 & kb_Up) && color_cursor > 0) color_cursor--;

    if ((new6 & kb_Enter) || (new1 & kb_2nd))
    {
        start_game();
        return;
    }

    if (new6 & kb_Clear)
    {
        state = STATE_DIFFICULTY_SELECT;
        return;
    }

    draw_color_select();
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
    screen_dirty = 1;
    board[to_r][to_c] = piece;

    /* record last move for highlighting */
    last_from_r = anim_from_r; last_from_c = anim_from_c;
    last_to_r = to_r; last_to_c = to_c;
    has_last_move = 1;

    /* check pawn promotion */
    is_ai_move = (game_mode == MODE_COMPUTER && current_turn != player_color);

    if (pending_move.flags & ENGINE_FLAG_PROMOTION)
    {
        if (is_ai_move)
        {
            /* AI promotion — apply the piece from engine move flags */
            uint8_t pt = pending_move.flags & ENGINE_FLAG_PROMO_MASK;
            int8_t ai_color = -player_color;
            if (pt == ENGINE_FLAG_PROMO_Q) board[to_r][to_c] = (ai_color == WHITE_TURN) ? W_QUEEN : B_QUEEN;
            else if (pt == ENGINE_FLAG_PROMO_R) board[to_r][to_c] = (ai_color == WHITE_TURN) ? W_ROOK : B_ROOK;
            else if (pt == ENGINE_FLAG_PROMO_B) board[to_r][to_c] = (ai_color == WHITE_TURN) ? W_BISHOP : B_BISHOP;
            else board[to_r][to_c] = (ai_color == WHITE_TURN) ? W_KNIGHT : B_KNIGHT;
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

    /* compute interpolated pixel position (map logical to display coords) */
    {
        int dfc = board_flipped ? 7 - anim_from_c : anim_from_c;
        int dfr = board_flipped ? 7 - anim_from_r : anim_from_r;
        int dtc = board_flipped ? 7 - anim_to_c : anim_to_c;
        int dtr = board_flipped ? 7 - anim_to_r : anim_to_r;
        from_px = BOARD_X + dfc * SQ_SIZE;
        from_py = BOARD_Y + dfr * SQ_SIZE;
        to_px = BOARD_X + dtc * SQ_SIZE;
        to_py = BOARD_Y + dtr * SQ_SIZE;
    }

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
            engine_move_t ai_move;

            ai_move = engine_think(0, think_time_ms);

            if (ai_move.from_row == ENGINE_SQ_NONE)
            {
                /* no legal move — game should be over */
                uint8_t status = engine_get_status();
                if (status == ENGINE_STATUS_CHECKMATE)
                    winner = player_color;
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

    /* skip redraw if no input and screen is clean */
    if (!new7 && !new6 && !new1 && !screen_dirty)
        return;

    /* cursor movement — save old position for partial redraw */
    {
        int old_r = cur_r, old_c = cur_c;

        if (board_flipped)
        {
            if (new7 & kb_Up)    cur_r = (cur_r < 7) ? cur_r + 1 : 7;
            if (new7 & kb_Down)  cur_r = (cur_r > 0) ? cur_r - 1 : 0;
            if (new7 & kb_Left)  cur_c = (cur_c < 7) ? cur_c + 1 : 7;
            if (new7 & kb_Right) cur_c = (cur_c > 0) ? cur_c - 1 : 0;
        }
        else
        {
            if (new7 & kb_Up)    cur_r = (cur_r > 0) ? cur_r - 1 : 0;
            if (new7 & kb_Down)  cur_r = (cur_r < 7) ? cur_r + 1 : 7;
            if (new7 & kb_Left)  cur_c = (cur_c > 0) ? cur_c - 1 : 0;
            if (new7 & kb_Right) cur_c = (cur_c < 7) ? cur_c + 1 : 7;
        }

        /* if only the cursor moved (no buttons), do a fast partial redraw */
        if (new7 && !new6 && !new1 && (cur_r != old_r || cur_c != old_c))
        {
            draw_cursor_move(old_r, old_c);
            return;
        }
    }

    /* enter/2nd — select or move (needs full redraw) */
    if ((new6 & kb_Enter) || (new1 & kb_2nd))
    {
        screen_dirty = 1;

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
        screen_dirty = 1;
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

    if (state == STATE_PLAYING && screen_dirty)
    {
        draw_playing();
        screen_dirty = 0;
    }
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
    prerender_pieces();

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
            case STATE_MENU:              update_menu();              break;
            case STATE_DIFFICULTY_SELECT: update_difficulty_select(); break;
            case STATE_COLOR_SELECT:      update_color_select();     break;
            case STATE_PLAYING:           update_playing();          break;
            case STATE_PROMOTION:         update_promotion();        break;
            case STATE_GAMEOVER:          update_gameover();         break;
        }

        prev_g1 = cur_g1;
        prev_g6 = cur_g6;
        prev_g7 = cur_g7;
        frame_count++;

        while (clock() - frame_start < FRAME_TIME)
            ;

    } while (running);

    engine_cleanup();
    gfx_End();
    return 0;
}
