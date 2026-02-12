#include <graphx.h>
#include <keypadc.h>
#include <fileioc.h>
#include <sys/util.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>

/* ========== Screen & Layout ========== */

#define SCREEN_W 320
#define SCREEN_H 240

#define CELL_SIZE 24
#define THIN_LINE 1
#define THICK_LINE 2
#define GRID_ORIGIN_X 3
#define GRID_ORIGIN_Y 5
/* Grid total: 2 + (3*24+2*1) + 2 + (3*24+2*1) + 2 + (3*24+2*1) + 2 = 230 */
#define GRID_TOTAL 230
#define SIDEBAR_X (GRID_ORIGIN_X + GRID_TOTAL + 4)
#define SIDEBAR_W (SCREEN_W - SIDEBAR_X - 2)

/* ========== Framerate ========== */

#define TARGET_FPS 60
#define FRAME_TIME (CLOCKS_PER_SEC / TARGET_FPS)

/* ========== Palette Indices ========== */

#define PAL_BG          0
#define PAL_CELL_BG     1
#define PAL_GRID_THIN   2
#define PAL_GRID_THICK  3
#define PAL_GIVEN       4
#define PAL_PLAYER      5
#define PAL_ERROR       6
#define PAL_PENCIL      7
#define PAL_SEL_BG      8
#define PAL_SAME_BG     9
#define PAL_HOUSE_BG    10
#define PAL_SIDEBAR_BG  11
#define PAL_SIDEBAR_TXT 12
#define PAL_MENU_TXT    13
#define PAL_HIGHLIGHT   14
#define PAL_PENCIL_IND  15

/* ========== Game Constants ========== */

#define NUM_SETTINGS    8
#define MAX_UNDO        128
#define SAVE_VERSION    1
#define APPVAR_NAME     "SUDKSAV"

#define KEY_REPEAT_DELAY 12
#define KEY_REPEAT_RATE  3

/* ========== Types ========== */

typedef struct {
    uint16_t bg, cell_bg, grid_thin, grid_thick;
    uint16_t given, player, error, pencil;
    uint16_t sel_bg, same_bg, house_bg;
    uint16_t sidebar_bg, sidebar_txt, menu_txt, highlight, pencil_ind;
} color_theme_t;

typedef struct {
    uint8_t value;      /* 0=empty, 1-9 */
    uint16_t marks;     /* pencil marks bitmask, bits 1-9 */
    uint8_t given;      /* 1=clue */
    uint8_t error;      /* 1=conflict */
} cell_t;

typedef struct {
    uint8_t row, col;
    uint8_t old_value;
    uint16_t old_marks;
    /* auto-erase restore info */
    uint8_t erase_digit;    /* 0=none, 1-9=digit erased from peers */
    uint16_t erase_row;     /* bitmask: which cols in row had mark erased */
    uint16_t erase_col;     /* bitmask: which rows in col had mark erased */
    uint16_t erase_box;     /* bitmask: which box positions had mark erased */
} undo_entry_t;

typedef struct {
    uint8_t auto_error;
    uint8_t auto_erase;
    uint8_t hl_same;
    uint8_t hl_house;
    uint8_t show_remaining;
    uint8_t hide_completed;
    uint8_t show_timer;
    uint8_t dark_mode;
} settings_t;

typedef struct {
    uint16_t games_played;
    uint16_t best_time; /* seconds, 0=no record */
} score_entry_t;

typedef struct {
    uint8_t version;
    settings_t settings;
    score_entry_t scores[3];
    uint8_t has_save;
    uint8_t difficulty;
    uint8_t solution[81];
    uint8_t given_mask[11];
    uint8_t player_values[81];
    uint16_t pencil_marks[81];
    uint16_t elapsed_seconds;
} save_data_t;

typedef enum {
    STATE_MENU,
    STATE_DIFFICULTY,
    STATE_GENERATING,
    STATE_PLAYING,
    STATE_PAUSED,
    STATE_SETTINGS,
    STATE_SCORES,
    STATE_COMPLETE
} game_state_t;

/* ========== Theme Data ========== */

static const color_theme_t light_theme = {
    gfx_RGBTo1555(210, 210, 215),   /* bg */
    gfx_RGBTo1555(255, 255, 255),   /* cell_bg */
    gfx_RGBTo1555(170, 170, 180),   /* grid_thin */
    gfx_RGBTo1555(30, 30, 40),      /* grid_thick */
    gfx_RGBTo1555(40, 70, 170),     /* given (blue) */
    gfx_RGBTo1555(20, 20, 20),      /* player (near-black) */
    gfx_RGBTo1555(210, 30, 30),     /* error (red) */
    gfx_RGBTo1555(110, 110, 120),   /* pencil (grey) */
    gfx_RGBTo1555(170, 210, 255),   /* sel_bg (light blue) */
    gfx_RGBTo1555(190, 225, 190),   /* same_bg (light green) */
    gfx_RGBTo1555(225, 225, 235),   /* house_bg (very light) */
    gfx_RGBTo1555(195, 195, 200),   /* sidebar_bg */
    gfx_RGBTo1555(30, 30, 30),      /* sidebar_txt */
    gfx_RGBTo1555(20, 20, 20),      /* menu_txt */
    gfx_RGBTo1555(60, 120, 210),    /* highlight */
    gfx_RGBTo1555(220, 140, 30),    /* pencil_ind */
};

static const color_theme_t dark_theme = {
    gfx_RGBTo1555(18, 18, 28),      /* bg */
    gfx_RGBTo1555(38, 38, 52),      /* cell_bg */
    gfx_RGBTo1555(60, 60, 75),      /* grid_thin */
    gfx_RGBTo1555(140, 140, 160),   /* grid_thick */
    gfx_RGBTo1555(90, 140, 255),    /* given (blue) */
    gfx_RGBTo1555(220, 220, 225),   /* player (near-white) */
    gfx_RGBTo1555(255, 55, 55),     /* error (red) */
    gfx_RGBTo1555(120, 120, 140),   /* pencil (grey) */
    gfx_RGBTo1555(50, 70, 110),     /* sel_bg */
    gfx_RGBTo1555(50, 80, 50),      /* same_bg */
    gfx_RGBTo1555(48, 48, 62),      /* house_bg */
    gfx_RGBTo1555(25, 25, 35),      /* sidebar_bg */
    gfx_RGBTo1555(190, 190, 200),   /* sidebar_txt */
    gfx_RGBTo1555(210, 210, 215),   /* menu_txt */
    gfx_RGBTo1555(60, 110, 200),    /* highlight */
    gfx_RGBTo1555(255, 170, 30),    /* pencil_ind */
};

/* ========== Global State ========== */

static game_state_t state;
static int running;
static int menu_cursor;
static int diff_cursor;
static int settings_cursor;

/* board */
static cell_t cells[81];
static uint8_t solution[81];
static uint8_t difficulty; /* 0=easy 1=med 2=hard */

/* cursor */
static uint8_t cur_row, cur_col;
static int pencil_mode;

/* undo */
static undo_entry_t undo_stack[MAX_UNDO];
static int undo_top;

/* timer */
static uint16_t elapsed_seconds;
static clock_t last_second_tick;

/* generator workspace */
static uint16_t row_used[9], col_used[9], box_used[9];
static int solve_count;

/* settings & scores */
static settings_t settings;
static score_entry_t scores[3];
static int new_best;
static int has_saved_game;

/* precomputed cell pixel positions */
static uint16_t cell_px[9];
static uint16_t cell_py[9];

/* digit counts */
static uint8_t digit_count[10]; /* index 1-9 */

/* keyboard */
static uint8_t cur_g1, cur_g2, cur_g3, cur_g4, cur_g5, cur_g6, cur_g7;
static uint8_t prev_g1, prev_g2, prev_g3, prev_g4, prev_g5, prev_g6, prev_g7;
static uint8_t key_repeat_timer;

/* ========== String Data ========== */

static const char *diff_names[3] = { "Easy", "Medium", "Hard" };
static const char *setting_names[NUM_SETTINGS] = {
    "Auto-check errors",
    "Auto-erase pencils",
    "Highlight same digit",
    "Highlight row/col/box",
    "Show remaining count",
    "Hide completed digits",
    "Show timer",
    "Dark mode"
};

/* ========== Helpers ========== */

static void apply_theme(const color_theme_t *t)
{
    gfx_palette[PAL_BG]          = t->bg;
    gfx_palette[PAL_CELL_BG]     = t->cell_bg;
    gfx_palette[PAL_GRID_THIN]   = t->grid_thin;
    gfx_palette[PAL_GRID_THICK]  = t->grid_thick;
    gfx_palette[PAL_GIVEN]       = t->given;
    gfx_palette[PAL_PLAYER]      = t->player;
    gfx_palette[PAL_ERROR]       = t->error;
    gfx_palette[PAL_PENCIL]      = t->pencil;
    gfx_palette[PAL_SEL_BG]      = t->sel_bg;
    gfx_palette[PAL_SAME_BG]     = t->same_bg;
    gfx_palette[PAL_HOUSE_BG]    = t->house_bg;
    gfx_palette[PAL_SIDEBAR_BG]  = t->sidebar_bg;
    gfx_palette[PAL_SIDEBAR_TXT] = t->sidebar_txt;
    gfx_palette[PAL_MENU_TXT]    = t->menu_txt;
    gfx_palette[PAL_HIGHLIGHT]   = t->highlight;
    gfx_palette[PAL_PENCIL_IND]  = t->pencil_ind;
}

static void apply_current_theme(void)
{
    apply_theme(settings.dark_mode ? &dark_theme : &light_theme);
}

static void init_cell_positions(void)
{
    int i, pos;

    pos = GRID_ORIGIN_X + THICK_LINE;
    for (i = 0; i < 9; i++)
    {
        cell_px[i] = (uint16_t)pos;
        pos += CELL_SIZE;
        if ((i % 3) == 2)
            pos += THICK_LINE;
        else
            pos += THIN_LINE;
    }

    pos = GRID_ORIGIN_Y + THICK_LINE;
    for (i = 0; i < 9; i++)
    {
        cell_py[i] = (uint16_t)pos;
        pos += CELL_SIZE;
        if ((i % 3) == 2)
            pos += THICK_LINE;
        else
            pos += THIN_LINE;
    }
}

static int get_new_digit(void)
{
    uint8_t n3 = cur_g3 & ~prev_g3;
    uint8_t n4 = cur_g4 & ~prev_g4;
    uint8_t n5 = cur_g5 & ~prev_g5;
    if (n3 & kb_1) return 1;
    if (n4 & kb_2) return 2;
    if (n5 & kb_3) return 3;
    if (n3 & kb_4) return 4;
    if (n4 & kb_5) return 5;
    if (n5 & kb_6) return 6;
    if (n3 & kb_7) return 7;
    if (n4 & kb_8) return 8;
    if (n5 & kb_9) return 9;
    return 0;
}

static void update_digit_counts(void)
{
    int i;
    memset(digit_count, 0, sizeof(digit_count));
    for (i = 0; i < 81; i++)
    {
        if (cells[i].value >= 1 && cells[i].value <= 9)
            digit_count[cells[i].value]++;
    }
}

/* popcount for uint16_t */
static int popcount16(uint16_t v)
{
    int c = 0;
    while (v) { c++; v &= v - 1; }
    return c;
}

/* ========== Puzzle Generator ========== */

static int fill_grid(int pos)
{
    int r, c, b, i, j;
    uint16_t used, cand;
    uint8_t order[9], tmp;

    if (pos == 81) return 1;
    r = pos / 9;
    c = pos % 9;
    b = (r / 3) * 3 + (c / 3);
    used = row_used[r] | col_used[c] | box_used[b];
    cand = (~used) & 0x3FE; /* bits 1-9 */
    if (!cand) return 0;

    for (i = 0; i < 9; i++) order[i] = (uint8_t)(i + 1);
    for (i = 8; i > 0; i--)
    {
        j = rand() % (i + 1);
        tmp = order[i]; order[i] = order[j]; order[j] = tmp;
    }

    for (i = 0; i < 9; i++)
    {
        uint8_t d = order[i];
        if (!(cand & (1 << d))) continue;
        solution[pos] = d;
        row_used[r] |= (1 << d);
        col_used[c] |= (1 << d);
        box_used[b] |= (1 << d);
        if (fill_grid(pos + 1)) return 1;
        row_used[r] &= ~(1 << d);
        col_used[c] &= ~(1 << d);
        box_used[b] &= ~(1 << d);
    }
    solution[pos] = 0;
    return 0;
}

/* temp grid used by solver and difficulty checker */
static uint8_t temp_grid[81];

static void rebuild_used(const uint8_t *grid)
{
    int i, r, c, b;
    memset(row_used, 0, sizeof(row_used));
    memset(col_used, 0, sizeof(col_used));
    memset(box_used, 0, sizeof(box_used));
    for (i = 0; i < 81; i++)
    {
        if (grid[i])
        {
            r = i / 9; c = i % 9; b = (r / 3) * 3 + (c / 3);
            row_used[r] |= (1 << grid[i]);
            col_used[c] |= (1 << grid[i]);
            box_used[b] |= (1 << grid[i]);
        }
    }
}

/* count solutions using MCV heuristic, stop at 2 */
static void count_solutions_inner(void)
{
    int i, r, c, b, d;
    int best_pos, best_count, cnt;
    uint16_t used, cand;

    if (solve_count >= 2) return;

    best_pos = -1;
    best_count = 10;
    for (i = 0; i < 81; i++)
    {
        if (temp_grid[i]) continue;
        r = i / 9; c = i % 9; b = (r / 3) * 3 + (c / 3);
        used = row_used[r] | col_used[c] | box_used[b];
        cand = (~used) & 0x3FE;
        if (!cand) return; /* dead end */
        cnt = popcount16(cand);
        if (cnt < best_count)
        {
            best_count = cnt;
            best_pos = i;
            if (cnt == 1) break;
        }
    }

    if (best_pos == -1) { solve_count++; return; }

    r = best_pos / 9; c = best_pos % 9; b = (r / 3) * 3 + (c / 3);
    used = row_used[r] | col_used[c] | box_used[b];
    for (d = 1; d <= 9; d++)
    {
        if (used & (1 << d)) continue;
        temp_grid[best_pos] = (uint8_t)d;
        row_used[r] |= (1 << d);
        col_used[c] |= (1 << d);
        box_used[b] |= (1 << d);
        count_solutions_inner();
        temp_grid[best_pos] = 0;
        row_used[r] &= ~(1 << d);
        col_used[c] &= ~(1 << d);
        box_used[b] &= ~(1 << d);
        if (solve_count >= 2) return;
    }
}

static int has_unique_solution(const uint8_t *puzzle)
{
    memcpy(temp_grid, puzzle, 81);
    rebuild_used(temp_grid);
    solve_count = 0;
    count_solutions_inner();
    return (solve_count == 1);
}

/* difficulty check: can the puzzle be solved with singles only? */
static int solvable_by_singles(const uint8_t *puzzle)
{
    uint8_t work[81];
    int i, r, c, d, j, br, bc, dr, dc, progress;
    uint16_t used, cand;

    memcpy(work, puzzle, 81);

    do {
        progress = 0;

        /* naked singles */
        for (i = 0; i < 81; i++)
        {
            if (work[i]) continue;
            r = i / 9; c = i % 9;
            used = 0;
            for (j = 0; j < 9; j++)
            {
                if (work[r * 9 + j]) used |= (1 << work[r * 9 + j]);
                if (work[j * 9 + c]) used |= (1 << work[j * 9 + c]);
            }
            br = (r / 3) * 3; bc = (c / 3) * 3;
            for (dr = 0; dr < 3; dr++)
                for (dc = 0; dc < 3; dc++)
                    if (work[(br + dr) * 9 + (bc + dc)])
                        used |= (1 << work[(br + dr) * 9 + (bc + dc)]);
            cand = (~used) & 0x3FE;
            if (popcount16(cand) == 1)
            {
                for (d = 1; d <= 9; d++)
                    if (cand & (1 << d)) { work[i] = (uint8_t)d; break; }
                progress = 1;
            }
        }

        /* hidden singles — check rows, columns, and boxes */
        /* For each house, find digits that can only go in one cell */
        for (j = 0; j < 27; j++)
        {
            /* enumerate cells in house j: rows 0-8, cols 9-17, boxes 18-26 */
            int hi, hcells[9];
            if (j < 9)
            {
                for (hi = 0; hi < 9; hi++) hcells[hi] = j * 9 + hi;
            }
            else if (j < 18)
            {
                for (hi = 0; hi < 9; hi++) hcells[hi] = hi * 9 + (j - 9);
            }
            else
            {
                int bx = ((j - 18) % 3) * 3;
                int by = ((j - 18) / 3) * 3;
                for (dr = 0; dr < 3; dr++)
                    for (dc = 0; dc < 3; dc++)
                        hcells[dr * 3 + dc] = (by + dr) * 9 + (bx + dc);
            }

            for (d = 1; d <= 9; d++)
            {
                int count = 0, last_pos = -1;
                for (hi = 0; hi < 9; hi++)
                {
                    int ci = hcells[hi];
                    if (work[ci] == (uint8_t)d) { count = 2; break; } /* already placed */
                    if (work[ci]) continue;
                    /* check if d is a candidate for this cell */
                    r = ci / 9; c = ci % 9;
                    used = 0;
                    {
                        int k;
                        for (k = 0; k < 9; k++)
                        {
                            if (work[r * 9 + k]) used |= (1 << work[r * 9 + k]);
                            if (work[k * 9 + c]) used |= (1 << work[k * 9 + c]);
                        }
                        br = (r / 3) * 3; bc = (c / 3) * 3;
                        for (dr = 0; dr < 3; dr++)
                            for (dc = 0; dc < 3; dc++)
                                if (work[(br + dr) * 9 + (bc + dc)])
                                    used |= (1 << work[(br + dr) * 9 + (bc + dc)]);
                    }
                    if (!(used & (1 << d)))
                    {
                        count++;
                        last_pos = ci;
                    }
                }
                if (count == 1 && last_pos >= 0)
                {
                    work[last_pos] = (uint8_t)d;
                    progress = 1;
                }
            }
        }

    } while (progress);

    for (i = 0; i < 81; i++)
        if (!work[i]) return 0;
    return 1;
}

static void generate_puzzle(uint8_t diff)
{
    uint8_t puzzle[81];
    uint8_t order[81], tmp;
    int i, j, clue_count;
    int target_min;
    int attempts;

    switch (diff)
    {
        case 0: target_min = 36; break; /* easy */
        case 1: target_min = 28; break; /* medium */
        default: target_min = 22; break; /* hard */
    }

    /* re-seed RNG with current clock for better entropy (user has been navigating menus) */
    srand(clock());

    attempts = 0;
retry:
    attempts++;

    /* generate a full solved grid */
    memset(solution, 0, 81);
    memset(row_used, 0, sizeof(row_used));
    memset(col_used, 0, sizeof(col_used));
    memset(box_used, 0, sizeof(box_used));
    fill_grid(0);

    memcpy(puzzle, solution, 81);
    clue_count = 81;

    /* shuffled removal order */
    for (i = 0; i < 81; i++) order[i] = (uint8_t)i;
    for (i = 80; i > 0; i--)
    {
        j = rand() % (i + 1);
        tmp = order[i]; order[i] = order[j]; order[j] = tmp;
    }

    for (i = 0; i < 81 && clue_count > target_min; i++)
    {
        int pos = order[i];
        uint8_t saved = puzzle[pos];
        puzzle[pos] = 0;
        clue_count--;

        if (!has_unique_solution(puzzle))
        {
            puzzle[pos] = saved;
            clue_count++;
        }
    }

    /* difficulty check */
    if (diff == 0)
    {
        /* easy must be solvable by singles only */
        if (!solvable_by_singles(puzzle) && attempts < 10)
            goto retry;
    }
    else if (diff == 1)
    {
        /* medium: shouldn't be too easy (solvable by singles) or keep too many clues */
        if (solvable_by_singles(puzzle) && clue_count > 32 && attempts < 10)
            goto retry;
    }
    /* hard: anything that has few clues and unique solution is fine */

    /* populate cells */
    for (i = 0; i < 81; i++)
    {
        cells[i].value = puzzle[i];
        cells[i].marks = 0;
        cells[i].given = (puzzle[i] != 0) ? 1 : 0;
        cells[i].error = 0;
    }
}

/* ========== Save/Load ========== */

static void gc_before(void) { gfx_End(); }
static void gc_after(void)
{
    gfx_Begin();
    gfx_SetDrawBuffer();
    apply_current_theme();
}

static void save_data(void)
{
    save_data_t save;
    uint8_t handle;
    int i;

    save.version = SAVE_VERSION;
    save.settings = settings;
    memcpy(save.scores, scores, sizeof(scores));

    if (state == STATE_PLAYING || state == STATE_PAUSED)
    {
        save.has_save = 1;
        save.difficulty = difficulty;
        memcpy(save.solution, solution, 81);
        memset(save.given_mask, 0, 11);
        for (i = 0; i < 81; i++)
        {
            if (cells[i].given)
                save.given_mask[i / 8] |= (uint8_t)(1 << (i % 8));
            save.player_values[i] = cells[i].value;
            save.pencil_marks[i] = cells[i].marks;
        }
        save.elapsed_seconds = elapsed_seconds;
    }
    else
    {
        save.has_save = 0;
        save.difficulty = 0;
        memset(save.solution, 0, 81);
        memset(save.given_mask, 0, 11);
        memset(save.player_values, 0, 81);
        memset(save.pencil_marks, 0, sizeof(save.pencil_marks));
        save.elapsed_seconds = 0;
    }

    ti_SetGCBehavior(gc_before, gc_after);
    handle = ti_Open(APPVAR_NAME, "w");
    if (handle)
    {
        ti_Write(&save, sizeof(save_data_t), 1, handle);
        ti_SetArchiveStatus(1, handle);
        ti_Close(handle);
    }
}

static int load_data(void)
{
    save_data_t save;
    uint8_t handle;
    int i;

    handle = ti_Open(APPVAR_NAME, "r");
    if (!handle) return 0;

    if (ti_Read(&save, sizeof(save_data_t), 1, handle) != 1)
    {
        ti_Close(handle);
        return 0;
    }
    ti_Close(handle);

    if (save.version != SAVE_VERSION) return 0;

    settings = save.settings;
    memcpy(scores, save.scores, sizeof(scores));

    if (save.has_save)
    {
        has_saved_game = 1;
        difficulty = save.difficulty;
        memcpy(solution, save.solution, 81);
        for (i = 0; i < 81; i++)
        {
            cells[i].given = (save.given_mask[i / 8] >> (i % 8)) & 1;
            cells[i].value = save.player_values[i];
            cells[i].marks = save.pencil_marks[i];
            cells[i].error = 0;
        }
        elapsed_seconds = save.elapsed_seconds;
    }

    return 1;
}

static void default_settings(void)
{
    settings.auto_error = 1;
    settings.auto_erase = 1;
    settings.hl_same = 1;
    settings.hl_house = 1;
    settings.show_remaining = 1;
    settings.hide_completed = 1;
    settings.show_timer = 1;
    settings.dark_mode = 0;
}

/* ========== Game Logic ========== */

static void check_errors(void)
{
    int i, r, c, j, br, bc, dr, dc;

    for (i = 0; i < 81; i++) cells[i].error = 0;
    if (!settings.auto_error) return;

    for (i = 0; i < 81; i++)
    {
        if (!cells[i].value) continue;
        r = i / 9; c = i % 9;

        /* row */
        for (j = 0; j < 9; j++)
        {
            if (j == c) continue;
            if (cells[r * 9 + j].value == cells[i].value)
            {
                cells[i].error = 1;
                goto next_cell;
            }
        }
        /* col */
        for (j = 0; j < 9; j++)
        {
            if (j == r) continue;
            if (cells[j * 9 + c].value == cells[i].value)
            {
                cells[i].error = 1;
                goto next_cell;
            }
        }
        /* box */
        br = (r / 3) * 3; bc = (c / 3) * 3;
        for (dr = 0; dr < 3; dr++)
            for (dc = 0; dc < 3; dc++)
            {
                int idx = (br + dr) * 9 + (bc + dc);
                if (idx == i) continue;
                if (cells[idx].value == cells[i].value)
                {
                    cells[i].error = 1;
                    goto next_cell;
                }
            }
        next_cell:;
    }
}

/* Erase pencil marks from peers and record what was erased into the undo entry.
   Call AFTER push_undo so undo_stack[undo_top-1] is the current entry. */
static void auto_erase_pencils(int row, int col, uint8_t digit)
{
    int j, br, bc, dr, dc;
    uint16_t bit;
    undo_entry_t *e;

    if (!settings.auto_erase) return;
    bit = (uint16_t)(1 << digit);

    e = &undo_stack[undo_top - 1];
    e->erase_digit = digit;
    e->erase_row = 0;
    e->erase_col = 0;
    e->erase_box = 0;

    for (j = 0; j < 9; j++)
    {
        if (cells[row * 9 + j].marks & bit) e->erase_row |= (uint16_t)(1 << j);
        cells[row * 9 + j].marks &= ~bit;
    }
    for (j = 0; j < 9; j++)
    {
        if (cells[j * 9 + col].marks & bit) e->erase_col |= (uint16_t)(1 << j);
        cells[j * 9 + col].marks &= ~bit;
    }
    br = (row / 3) * 3; bc = (col / 3) * 3;
    for (dr = 0; dr < 3; dr++)
        for (dc = 0; dc < 3; dc++)
        {
            int bi = dr * 3 + dc;
            if (cells[(br + dr) * 9 + (bc + dc)].marks & bit)
                e->erase_box |= (uint16_t)(1 << bi);
            cells[(br + dr) * 9 + (bc + dc)].marks &= ~bit;
        }
}

static int check_complete(void)
{
    int i;
    for (i = 0; i < 81; i++)
    {
        if (cells[i].value != solution[i]) return 0;
    }
    return 1;
}

static void push_undo(uint8_t row, uint8_t col)
{
    int idx = row * 9 + col;
    if (undo_top < MAX_UNDO)
    {
        undo_stack[undo_top].row = row;
        undo_stack[undo_top].col = col;
        undo_stack[undo_top].old_value = cells[idx].value;
        undo_stack[undo_top].old_marks = cells[idx].marks;
        undo_stack[undo_top].erase_digit = 0;
        undo_stack[undo_top].erase_row = 0;
        undo_stack[undo_top].erase_col = 0;
        undo_stack[undo_top].erase_box = 0;
        undo_top++;
    }
}

static void perform_undo(void)
{
    int idx;
    undo_entry_t *e;

    if (undo_top == 0) return;
    undo_top--;
    e = &undo_stack[undo_top];

    /* restore the cell itself */
    idx = e->row * 9 + e->col;
    cells[idx].value = e->old_value;
    cells[idx].marks = e->old_marks;

    /* restore auto-erased pencil marks in peers */
    if (e->erase_digit)
    {
        uint16_t bit = (uint16_t)(1 << e->erase_digit);
        int j, br, bc, dr, dc;

        for (j = 0; j < 9; j++)
            if (e->erase_row & (1 << j))
                cells[e->row * 9 + j].marks |= bit;
        for (j = 0; j < 9; j++)
            if (e->erase_col & (1 << j))
                cells[j * 9 + e->col].marks |= bit;
        br = (e->row / 3) * 3; bc = (e->col / 3) * 3;
        for (dr = 0; dr < 3; dr++)
            for (dc = 0; dc < 3; dc++)
                if (e->erase_box & (1 << (dr * 3 + dc)))
                    cells[(br + dr) * 9 + (bc + dc)].marks |= bit;
    }

    check_errors();
    update_digit_counts();
}

static void jump_next_empty(void)
{
    int start = cur_row * 9 + cur_col;
    int i, pos;
    for (i = 1; i <= 81; i++)
    {
        pos = (start + i) % 81;
        if (cells[pos].value == 0)
        {
            cur_row = (uint8_t)(pos / 9);
            cur_col = (uint8_t)(pos % 9);
            return;
        }
    }
}

/* ========== Drawing ========== */

static void draw_grid(void)
{
    int r, c, idx;
    uint8_t sel_val;
    int sel_box_r, sel_box_c;

    /* fill grid background with thick line color */
    gfx_SetColor(PAL_GRID_THICK);
    gfx_FillRectangle_NoClip(GRID_ORIGIN_X, GRID_ORIGIN_Y, GRID_TOTAL, GRID_TOTAL);

    /* fill thin-line areas within each 3x3 box */
    {
        int bx, by;
        for (r = 0; r < 3; r++)
        {
            for (c = 0; c < 3; c++)
            {
                bx = cell_px[c * 3];
                by = cell_py[r * 3];
                gfx_SetColor(PAL_GRID_THIN);
                gfx_FillRectangle_NoClip(bx, by,
                    (uint24_t)(cell_px[c * 3 + 2] + CELL_SIZE - bx),
                    (uint8_t)(cell_py[r * 3 + 2] + CELL_SIZE - by));
            }
        }
    }

    sel_val = cells[cur_row * 9 + cur_col].value;
    sel_box_r = cur_row / 3;
    sel_box_c = cur_col / 3;

    /* fill each cell with appropriate background */
    for (r = 0; r < 9; r++)
    {
        for (c = 0; c < 9; c++)
        {
            uint8_t bg;
            idx = r * 9 + c;

            if (r == (int)cur_row && c == (int)cur_col)
                bg = PAL_SEL_BG;
            else if (settings.hl_same && sel_val && cells[idx].value == sel_val)
                bg = PAL_SAME_BG;
            else if (settings.hl_house &&
                     (r == (int)cur_row || c == (int)cur_col ||
                      (r / 3 == sel_box_r && c / 3 == sel_box_c)))
                bg = PAL_HOUSE_BG;
            else
                bg = PAL_CELL_BG;

            gfx_SetColor(bg);
            gfx_FillRectangle_NoClip(cell_px[c], cell_py[r], CELL_SIZE, CELL_SIZE);
        }
    }

    /* draw cell contents */
    for (r = 0; r < 9; r++)
    {
        for (c = 0; c < 9; c++)
        {
            int px = cell_px[c];
            int py = cell_py[r];
            idx = r * 9 + c;

            if (cells[idx].value)
            {
                uint8_t fg;
                if (cells[idx].error && !cells[idx].given)
                    fg = PAL_ERROR;
                else if (cells[idx].given)
                    fg = PAL_GIVEN;
                else
                    fg = PAL_PLAYER;

                gfx_SetTextScale(2, 2);
                gfx_SetTextFGColor(fg);
                gfx_SetTextXY(px + 4, py + 4);
                gfx_PrintChar('0' + cells[idx].value);
            }
            else if (cells[idx].marks)
            {
                int d, mc, mr;
                gfx_SetTextScale(1, 1);
                gfx_SetMonospaceFont(7);
                gfx_SetTextFGColor(PAL_PENCIL);
                for (d = 1; d <= 9; d++)
                {
                    if (cells[idx].marks & (1 << d))
                    {
                        mc = (d - 1) % 3;
                        mr = (d - 1) / 3;
                        gfx_SetTextXY(px + 2 + mc * 7, py + 1 + mr * 7);
                        gfx_PrintChar('0' + d);
                    }
                }
                gfx_SetMonospaceFont(0);
            }
        }
    }
}

static void draw_sidebar(void)
{
    int d, dc, dr, remaining;

    gfx_SetColor(PAL_SIDEBAR_BG);
    gfx_FillRectangle_NoClip(SIDEBAR_X, 0, SIDEBAR_W, SCREEN_H);

    gfx_SetTextScale(1, 1);
    gfx_SetTextFGColor(PAL_SIDEBAR_TXT);

    /* difficulty */
    gfx_PrintStringXY(diff_names[difficulty], SIDEBAR_X + 4, 6);

    /* timer */
    if (settings.show_timer)
    {
        int m = elapsed_seconds / 60;
        int s = elapsed_seconds % 60;
        gfx_SetTextXY(SIDEBAR_X + 4, 20);
        if (m < 10) gfx_PrintChar('0');
        gfx_PrintUInt((unsigned)m, m < 10 ? 1 : (m < 100 ? 2 : 3));
        gfx_PrintChar(':');
        if (s < 10) gfx_PrintChar('0');
        gfx_PrintUInt((unsigned)s, s < 10 ? 1 : 2);
    }

    /* separator */
    gfx_SetColor(PAL_GRID_THIN);
    gfx_HorizLine_NoClip(SIDEBAR_X + 2, 35, SIDEBAR_W - 4);

    /* remaining digit counts */
    if (settings.show_remaining)
    {
        gfx_SetTextFGColor(PAL_SIDEBAR_TXT);
        gfx_PrintStringXY("Left:", SIDEBAR_X + 4, 40);
        for (d = 1; d <= 9; d++)
        {
            dc = (d - 1) % 3;
            dr = (d - 1) / 3;
            remaining = 9 - digit_count[d];

            if (settings.hide_completed && remaining == 0)
                gfx_SetTextFGColor(PAL_GRID_THIN);
            else
                gfx_SetTextFGColor(PAL_SIDEBAR_TXT);

            gfx_SetTextXY(SIDEBAR_X + 4 + dc * 26, 54 + dr * 14);
            gfx_PrintChar('0' + d);
            gfx_PrintChar(':');
            gfx_PrintChar('0' + remaining);
        }
    }

    /* separator */
    gfx_SetColor(PAL_GRID_THIN);
    gfx_HorizLine_NoClip(SIDEBAR_X + 2, 100, SIDEBAR_W - 4);

    /* pencil mode indicator */
    if (pencil_mode)
    {
        gfx_SetTextFGColor(PAL_PENCIL_IND);
        gfx_PrintStringXY("PENCIL", SIDEBAR_X + 4, 106);
    }
    else
    {
        gfx_SetTextFGColor(PAL_SIDEBAR_TXT);
        gfx_PrintStringXY("Normal", SIDEBAR_X + 4, 106);
    }

    /* separator */
    gfx_SetColor(PAL_GRID_THIN);
    gfx_HorizLine_NoClip(SIDEBAR_X + 2, 120, SIDEBAR_W - 4);

    /* controls help */
    gfx_SetTextFGColor(PAL_SIDEBAR_TXT);
    gfx_PrintStringXY("1-9:Digit", SIDEBAR_X + 2, 126);
    gfx_PrintStringXY("2nd:Pencl", SIDEBAR_X + 2, 138);
    gfx_PrintStringXY("Del:Erase", SIDEBAR_X + 2, 150);
    gfx_PrintStringXY("Alph:Undo", SIDEBAR_X + 2, 162);
    gfx_PrintStringXY("Mode:Next", SIDEBAR_X + 2, 174);
    gfx_PrintStringXY("Clr:Pause", SIDEBAR_X + 2, 186);
}

static void draw_playing(void)
{
    gfx_FillScreen(PAL_BG);
    draw_grid();
    draw_sidebar();
    gfx_SwapDraw();
}

/* ========== State: Playing ========== */

static void process_cursor_move(uint8_t dir_keys)
{
    if (dir_keys & kb_Up)    cur_row = (cur_row == 0) ? 8 : cur_row - 1;
    if (dir_keys & kb_Down)  cur_row = (cur_row == 8) ? 0 : cur_row + 1;
    if (dir_keys & kb_Left)  cur_col = (cur_col == 0) ? 8 : cur_col - 1;
    if (dir_keys & kb_Right) cur_col = (cur_col == 8) ? 0 : cur_col + 1;
}

static void update_playing(void)
{
    uint8_t new_g1 = cur_g1 & ~prev_g1;
    uint8_t new_g2 = cur_g2 & ~prev_g2;
    uint8_t new_g6 = cur_g6 & ~prev_g6;
    uint8_t new_g7 = cur_g7 & ~prev_g7;
    int digit, idx;

    /* pause */
    if (new_g6 & kb_Clear)
    {
        state = STATE_PAUSED;
        return;
    }

    /* cursor movement with key repeat */
    if (new_g7 & (kb_Up | kb_Down | kb_Left | kb_Right))
    {
        process_cursor_move(new_g7);
        key_repeat_timer = KEY_REPEAT_DELAY;
    }
    else if (cur_g7 & (kb_Up | kb_Down | kb_Left | kb_Right))
    {
        if (key_repeat_timer > 0)
            key_repeat_timer--;
        if (key_repeat_timer == 0)
        {
            process_cursor_move(cur_g7);
            key_repeat_timer = KEY_REPEAT_RATE;
        }
    }

    /* pencil mode toggle */
    if (new_g1 & kb_2nd)
        pencil_mode = !pencil_mode;

    /* jump to next empty */
    if (new_g1 & kb_Mode)
        jump_next_empty();

    /* undo */
    if (new_g2 & kb_Alpha)
        perform_undo();

    /* digit input */
    digit = get_new_digit();
    if (digit)
    {
        idx = cur_row * 9 + cur_col;
        if (!cells[idx].given)
        {
            push_undo(cur_row, cur_col);
            if (pencil_mode)
            {
                cells[idx].marks ^= (1 << digit);
            }
            else
            {
                if (cells[idx].value == (uint8_t)digit)
                    cells[idx].value = 0; /* toggle off if same digit */
                else
                    cells[idx].value = (uint8_t)digit;
                cells[idx].marks = 0;
                if (cells[idx].value)
                    auto_erase_pencils(cur_row, cur_col, cells[idx].value);
            }
            check_errors();
            update_digit_counts();

            if (check_complete())
            {
                state = STATE_COMPLETE;
                new_best = 0;
                scores[difficulty].games_played++;
                if (scores[difficulty].best_time == 0 ||
                    elapsed_seconds < scores[difficulty].best_time)
                {
                    scores[difficulty].best_time = elapsed_seconds;
                    new_best = 1;
                }
                has_saved_game = 0;
                save_data();
                return;
            }
        }
    }

    /* erase */
    if (new_g1 & kb_Del)
    {
        idx = cur_row * 9 + cur_col;
        if (!cells[idx].given && (cells[idx].value || cells[idx].marks))
        {
            push_undo(cur_row, cur_col);
            cells[idx].value = 0;
            cells[idx].marks = 0;
            check_errors();
            update_digit_counts();
        }
    }

    /* update timer */
    {
        clock_t now = clock();
        if (now - last_second_tick >= CLOCKS_PER_SEC)
        {
            elapsed_seconds++;
            last_second_tick = now;
        }
    }

    draw_playing();
}

/* ========== State: Paused ========== */

static void draw_paused(void)
{
    gfx_FillScreen(PAL_BG);
    gfx_SetTextScale(2, 2);
    gfx_SetTextFGColor(PAL_MENU_TXT);
    gfx_PrintStringXY("PAUSED", 104, 70);

    gfx_SetTextScale(1, 1);
    gfx_SetTextFGColor(PAL_SIDEBAR_TXT);
    gfx_PrintStringXY("Enter: Resume", 104, 110);
    gfx_PrintStringXY("Clear: Save & Quit", 88, 130);
    gfx_SwapDraw();
}

static void update_paused(void)
{
    uint8_t new_g6 = cur_g6 & ~prev_g6;

    if (new_g6 & kb_Enter)
    {
        state = STATE_PLAYING;
        last_second_tick = clock();
        return;
    }

    if (new_g6 & kb_Clear)
    {
        save_data();
        has_saved_game = 1;
        state = STATE_MENU;
        return;
    }

    draw_paused();
}

/* ========== State: Complete ========== */

static void draw_complete(void)
{
    int m, s;

    gfx_FillScreen(PAL_BG);
    gfx_SetTextScale(2, 2);
    gfx_SetTextFGColor(PAL_HIGHLIGHT);
    gfx_PrintStringXY("Complete!", 84, 50);

    gfx_SetTextScale(1, 1);
    gfx_SetTextFGColor(PAL_MENU_TXT);
    gfx_PrintStringXY(diff_names[difficulty], 140, 80);

    m = elapsed_seconds / 60;
    s = elapsed_seconds % 60;
    gfx_SetTextXY(110, 100);
    gfx_PrintString("Time: ");
    if (m < 10) gfx_PrintChar('0');
    gfx_PrintUInt((unsigned)m, m < 10 ? 1 : (m < 100 ? 2 : 3));
    gfx_PrintChar(':');
    if (s < 10) gfx_PrintChar('0');
    gfx_PrintUInt((unsigned)s, s < 10 ? 1 : 2);

    if (new_best)
    {
        gfx_SetTextFGColor(PAL_PENCIL_IND);
        gfx_PrintStringXY("New Best!", 120, 125);
    }

    gfx_SetTextFGColor(PAL_SIDEBAR_TXT);
    gfx_PrintStringXY("Enter: Return to menu", 72, 180);
    gfx_SwapDraw();
}

static void update_complete(void)
{
    uint8_t new_g6 = cur_g6 & ~prev_g6;
    uint8_t new_g1 = cur_g1 & ~prev_g1;

    if ((new_g6 & kb_Enter) || (new_g1 & kb_2nd))
    {
        state = STATE_MENU;
        return;
    }

    draw_complete();
}

/* ========== State: Generating ========== */

static void update_generating(void)
{
    gfx_FillScreen(PAL_BG);
    gfx_SetTextScale(2, 2);
    gfx_SetTextFGColor(PAL_MENU_TXT);
    gfx_PrintStringXY("Generating", 64, 90);
    gfx_SetTextScale(1, 1);
    gfx_SetTextFGColor(PAL_SIDEBAR_TXT);
    gfx_PrintStringXY(diff_names[difficulty], 136, 120);
    gfx_PrintStringXY("Please wait...", 104, 140);
    gfx_SwapDraw();

    generate_puzzle(difficulty);

    cur_row = 4;
    cur_col = 4;
    pencil_mode = 0;
    undo_top = 0;
    elapsed_seconds = 0;
    last_second_tick = clock();
    update_digit_counts();

    state = STATE_PLAYING;
}

/* ========== State: Menu ========== */

#define MENU_ITEMS_BASE 3  /* Play, Settings, Scores */
#define MENU_ITEMS_CONT 4  /* Continue, Play, Settings, Scores */

static int menu_item_count(void)
{
    return has_saved_game ? MENU_ITEMS_CONT : MENU_ITEMS_BASE;
}

static void draw_menu_item(int idx, int y, const char *text, int cursor, int total)
{
    int text_w = (int)strlen(text) * 16;
    int text_x = (SCREEN_W - text_w) / 2;
    int bar_x = text_x - 6;
    int bar_w = text_w + 12;

    (void)total;

    if (cursor == idx)
    {
        gfx_SetColor(PAL_HIGHLIGHT);
        gfx_FillRectangle_NoClip(bar_x, y - 2, (uint24_t)bar_w, 22);
        gfx_SetTextFGColor(PAL_BG);
    }
    else
    {
        gfx_SetTextFGColor(PAL_MENU_TXT);
    }
    gfx_PrintStringXY(text, text_x, y);
}

/* decorative puzzle for menu background */
// source is NYT 2025-07-07 hard
static const uint8_t menu_bg_digits[81] = {
    3,0,0,0,0,9,6,5,0,
    0,0,0,2,0,0,0,0,8,
    0,0,4,5,0,0,0,0,2,
    4,7,0,0,0,0,0,0,0,
    0,2,0,0,0,0,7,8,0,
    0,0,5,0,0,2,0,0,1,
    6,0,7,0,0,1,0,0,0,
    0,0,0,0,8,3,0,4,0,
    0,0,0,0,0,0,3,0,0
};

static void draw_menu_grid_bg(void)
{
    /* decorative faint 9x9 grid centered on screen */
    int gs = 180; /* grid size in pixels */
    int cs = gs / 9; /* cell size = 20 */
    int ox = (SCREEN_W - gs) / 2; /* origin x */
    int oy = (SCREEN_H - gs) / 2; /* origin y */
    int i, r, c;

    gfx_SetColor(PAL_GRID_THIN);

    /* thin lines between cells */
    for (i = 1; i < 9; i++)
    {
        gfx_VertLine_NoClip(ox + i * cs, oy, gs);
        gfx_HorizLine_NoClip(ox, oy + i * cs, gs);
    }

    /* thick lines for 3x3 box borders + outer border */
    gfx_SetColor(PAL_GRID_THICK);
    for (i = 0; i <= 3; i++)
    {
        int p = i * 3 * cs;
        gfx_FillRectangle_NoClip(ox + p, oy, 2, gs);
        gfx_FillRectangle_NoClip(ox, oy + p, gs, 2);
    }

    /* scattered digits for flavour */
    gfx_SetTextFGColor(PAL_GRID_THICK);
    gfx_SetTextScale(1, 1);
    for (r = 0; r < 9; r++)
    {
        for (c = 0; c < 9; c++)
        {
            uint8_t d = menu_bg_digits[r * 9 + c];
            if (d)
            {
                gfx_SetTextXY(ox + c * cs + 6, oy + r * cs + 6);
                gfx_PrintChar('0' + d);
            }
        }
    }
}

static void draw_menu(void)
{
    int items = menu_item_count();
    int y_start = 85;
    int i = 0;

    gfx_FillScreen(PAL_BG);

    /* background grid */
    draw_menu_grid_bg();

    /* title */
    gfx_SetTextScale(3, 3);
    gfx_SetTextFGColor(PAL_HIGHLIGHT);
    gfx_PrintStringXY("SUDOKU", 88, 28);

    gfx_SetTextScale(2, 2);

    if (has_saved_game)
    {
        draw_menu_item(i, y_start + i * 32, "Continue", menu_cursor, items);
        i++;
    }
    draw_menu_item(i, y_start + i * 32, "New Game", menu_cursor, items);
    i++;
    draw_menu_item(i, y_start + i * 32, "Settings", menu_cursor, items);
    i++;
    draw_menu_item(i, y_start + i * 32, "Scores", menu_cursor, items);

    gfx_SetTextScale(1, 1);
    gfx_SetTextFGColor(PAL_SIDEBAR_TXT);
    gfx_PrintStringXY("arrows: move  enter: select  clear: quit", 12, 226);

    gfx_SwapDraw();
}

static void update_menu(void)
{
    uint8_t new_g7 = cur_g7 & ~prev_g7;
    uint8_t new_g6 = cur_g6 & ~prev_g6;
    uint8_t new_g1 = cur_g1 & ~prev_g1;
    int items = menu_item_count();
    int sel;

    if ((new_g7 & kb_Down) && menu_cursor < items - 1) menu_cursor++;
    if ((new_g7 & kb_Up) && menu_cursor > 0) menu_cursor--;

    if ((new_g6 & kb_Enter) || (new_g1 & kb_2nd))
    {
        sel = menu_cursor;
        if (!has_saved_game) sel++; /* shift indices when no continue option */

        switch (sel)
        {
            case 0: /* continue */
                state = STATE_PLAYING;
                last_second_tick = clock();
                check_errors();
                update_digit_counts();
                break;
            case 1: /* new game */
                diff_cursor = 0;
                state = STATE_DIFFICULTY;
                break;
            case 2: /* settings */
                settings_cursor = 0;
                state = STATE_SETTINGS;
                break;
            case 3: /* scores */
                state = STATE_SCORES;
                break;
        }
        return;
    }

    if (new_g6 & kb_Clear)
    {
        running = 0;
        return;
    }

    draw_menu();
}

/* ========== State: Difficulty Select ========== */

static void draw_difficulty(void)
{
    int i, y;
    const char *descs[3] = {
        "Relaxed, singles only",
        "Moderate challenge",
        "Tough, few clues"
    };

    gfx_FillScreen(PAL_BG);

    gfx_SetTextScale(2, 2);
    gfx_SetTextFGColor(PAL_MENU_TXT);
    gfx_PrintStringXY("Difficulty", 80, 20);

    gfx_SetTextScale(2, 2);
    for (i = 0; i < 3; i++)
    {
        y = 65 + i * 50;

        if (i == diff_cursor)
        {
            gfx_SetColor(PAL_HIGHLIGHT);
            gfx_FillRectangle_NoClip(50, y - 2, 220, 22);
            gfx_SetTextFGColor(PAL_BG);
        }
        else
        {
            gfx_SetTextFGColor(PAL_MENU_TXT);
        }

        gfx_PrintStringXY(diff_names[i],
            (SCREEN_W - (int)strlen(diff_names[i]) * 16) / 2, y);

        gfx_SetTextScale(1, 1);
        if (i == diff_cursor)
            gfx_SetTextFGColor(PAL_BG);
        else
            gfx_SetTextFGColor(PAL_SIDEBAR_TXT);
        gfx_PrintStringXY(descs[i],
            (SCREEN_W - (int)strlen(descs[i]) * 8) / 2, y + 22);
        gfx_SetTextScale(2, 2);
    }

    gfx_SetTextScale(1, 1);
    gfx_SetTextFGColor(PAL_SIDEBAR_TXT);
    gfx_PrintStringXY("enter: select  clear: back", 68, 226);

    gfx_SwapDraw();
}

static void update_difficulty(void)
{
    uint8_t new_g7 = cur_g7 & ~prev_g7;
    uint8_t new_g6 = cur_g6 & ~prev_g6;
    uint8_t new_g1 = cur_g1 & ~prev_g1;

    if ((new_g7 & kb_Down) && diff_cursor < 2) diff_cursor++;
    if ((new_g7 & kb_Up) && diff_cursor > 0) diff_cursor--;

    if ((new_g6 & kb_Enter) || (new_g1 & kb_2nd))
    {
        difficulty = (uint8_t)diff_cursor;
        state = STATE_GENERATING;
        return;
    }

    if (new_g6 & kb_Clear)
    {
        state = STATE_MENU;
        return;
    }

    draw_difficulty();
}

/* ========== State: Settings ========== */

static uint8_t get_setting(int idx)
{
    switch (idx)
    {
        case 0: return settings.auto_error;
        case 1: return settings.auto_erase;
        case 2: return settings.hl_same;
        case 3: return settings.hl_house;
        case 4: return settings.show_remaining;
        case 5: return settings.hide_completed;
        case 6: return settings.show_timer;
        case 7: return settings.dark_mode;
        default: return 0;
    }
}

static void toggle_setting(int idx)
{
    switch (idx)
    {
        case 0: settings.auto_error      = !settings.auto_error; break;
        case 1: settings.auto_erase      = !settings.auto_erase; break;
        case 2: settings.hl_same         = !settings.hl_same; break;
        case 3: settings.hl_house        = !settings.hl_house; break;
        case 4: settings.show_remaining  = !settings.show_remaining; break;
        case 5: settings.hide_completed  = !settings.hide_completed; break;
        case 6: settings.show_timer      = !settings.show_timer; break;
        case 7: settings.dark_mode       = !settings.dark_mode; break;
    }
}

static void draw_settings(void)
{
    int i, y;

    gfx_FillScreen(PAL_BG);

    gfx_SetTextScale(2, 2);
    gfx_SetTextFGColor(PAL_MENU_TXT);
    gfx_PrintStringXY("Settings", 96, 8);

    gfx_SetTextScale(1, 1);
    for (i = 0; i < NUM_SETTINGS; i++)
    {
        y = 38 + i * 22;

        if (i == settings_cursor)
        {
            gfx_SetColor(PAL_HIGHLIGHT);
            gfx_FillRectangle_NoClip(8, y - 2, 304, 18);
            gfx_SetTextFGColor(PAL_BG);
        }
        else
        {
            gfx_SetTextFGColor(PAL_MENU_TXT);
        }

        gfx_SetTextXY(14, y);
        gfx_PrintString(get_setting(i) ? "[X] " : "[ ] ");
        gfx_PrintString(setting_names[i]);
    }

    gfx_SetTextFGColor(PAL_SIDEBAR_TXT);
    gfx_PrintStringXY("enter: toggle  clear: back", 68, 226);

    gfx_SwapDraw();
}

static void update_settings(void)
{
    uint8_t new_g7 = cur_g7 & ~prev_g7;
    uint8_t new_g6 = cur_g6 & ~prev_g6;
    uint8_t new_g1 = cur_g1 & ~prev_g1;

    if ((new_g7 & kb_Down) && settings_cursor < NUM_SETTINGS - 1) settings_cursor++;
    if ((new_g7 & kb_Up) && settings_cursor > 0) settings_cursor--;

    if ((new_g6 & kb_Enter) || (new_g1 & kb_2nd))
    {
        toggle_setting(settings_cursor);

        /* if dark mode toggled, re-apply theme immediately */
        if (settings_cursor == 7)
            apply_current_theme();
    }

    if (new_g6 & kb_Clear)
    {
        save_data(); /* persist settings */
        state = STATE_MENU;
        return;
    }

    draw_settings();
}

/* ========== State: Scores ========== */

static void draw_scores(void)
{
    int d, y, m, s;

    gfx_FillScreen(PAL_BG);

    gfx_SetTextScale(2, 2);
    gfx_SetTextFGColor(PAL_MENU_TXT);
    gfx_PrintStringXY("Scoreboard", 72, 10);

    gfx_SetTextScale(1, 1);
    for (d = 0; d < 3; d++)
    {
        y = 45 + d * 55;

        gfx_SetTextFGColor(PAL_HIGHLIGHT);
        gfx_SetTextXY(30, y);
        gfx_PrintString(diff_names[d]);

        gfx_SetTextFGColor(PAL_MENU_TXT);
        gfx_SetTextXY(40, y + 16);
        gfx_PrintString("Played: ");
        gfx_PrintUInt(scores[d].games_played, 1);

        gfx_SetTextXY(40, y + 30);
        gfx_PrintString("Best:   ");
        if (scores[d].best_time > 0)
        {
            m = scores[d].best_time / 60;
            s = scores[d].best_time % 60;
            if (m < 10) gfx_PrintChar('0');
            gfx_PrintUInt((unsigned)m, m < 10 ? 1 : (m < 100 ? 2 : 3));
            gfx_PrintChar(':');
            if (s < 10) gfx_PrintChar('0');
            gfx_PrintUInt((unsigned)s, s < 10 ? 1 : 2);
        }
        else
        {
            gfx_PrintString("--:--");
        }
    }

    gfx_SetTextFGColor(PAL_SIDEBAR_TXT);
    gfx_PrintStringXY("clear: back", 112, 226);

    gfx_SwapDraw();
}

static void update_scores(void)
{
    uint8_t new_g6 = cur_g6 & ~prev_g6;

    if (new_g6 & kb_Clear)
    {
        state = STATE_MENU;
        return;
    }

    draw_scores();
}

/* ========== Main ========== */

int main(void)
{
    clock_t frame_start;

    gfx_Begin();
    gfx_SetDrawBuffer();

    srand(clock());

    /* defaults */
    default_settings();
    memset(scores, 0, sizeof(scores));
    has_saved_game = 0;

    /* try loading saved data */
    load_data();
    apply_current_theme();

    state = STATE_MENU;
    menu_cursor = 0;
    running = 1;

    init_cell_positions();

    do
    {
        frame_start = clock();
        kb_Scan();

        cur_g1 = kb_Data[1];
        cur_g2 = kb_Data[2];
        cur_g3 = kb_Data[3];
        cur_g4 = kb_Data[4];
        cur_g5 = kb_Data[5];
        cur_g6 = kb_Data[6];
        cur_g7 = kb_Data[7];

        switch (state)
        {
            case STATE_MENU:       update_menu();       break;
            case STATE_DIFFICULTY: update_difficulty();  break;
            case STATE_GENERATING: update_generating();  break;
            case STATE_PLAYING:    update_playing();     break;
            case STATE_PAUSED:     update_paused();      break;
            case STATE_SETTINGS:   update_settings();    break;
            case STATE_SCORES:     update_scores();      break;
            case STATE_COMPLETE:   update_complete();    break;
        }

        prev_g1 = cur_g1;
        prev_g2 = cur_g2;
        prev_g3 = cur_g3;
        prev_g4 = cur_g4;
        prev_g5 = cur_g5;
        prev_g6 = cur_g6;
        prev_g7 = cur_g7;

        while (clock() - frame_start < FRAME_TIME)
            ;

    } while (running);

    gfx_End();
    return 0;
}
