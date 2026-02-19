/*
 * EMU UCI — eZ80 chess engine bridge for emulator-based tournaments.
 *
 * Command format in cmd_slot (patched into binary before each run):
 *   "<time_ms> <max_nodes> <variance> <book_ply> <fen>"
 *
 * FEN is standard Forsyth-Edwards Notation (always <90 bytes).
 * Output: "MOVE <uci>\n" via dbg_printf, then terminates.
 *
 * The 512-byte cmd_slot is initialized with "@@CMDSLOT@@" sentinel
 * so the controller can locate and patch it in the binary.
 */

#undef NDEBUG
#include <debug.h>
#include <string.h>
#include <stdint.h>
#include <sys/timers.h>

#include "engine.h"

/* ========== Command Slot (patched by controller) ========== */

static char cmd_slot[512] = "@@CMDSLOT@@";

/* ========== Hardware Timer (48 MHz) ========== */

static uint32_t time_base;
static uint32_t last_raw;

static uint32_t time_ms(void)
{
    uint32_t raw = timer_GetSafe(1, TIMER_UP) / 48000UL;
    if (raw < last_raw)
        time_base += 89478UL;
    last_raw = raw;
    return time_base + raw;
}

static void time_reset(void)
{
    timer_Set(1, 0);
    time_base = 0;
    last_raw = 0;
}

/* ========== Helpers ========== */

static const char *skip_ws(const char *p)
{
    while (*p == ' ') p++;
    return p;
}

static uint32_t parse_uint(const char **pp)
{
    const char *p = *pp;
    uint32_t v = 0;
    while (*p >= '0' && *p <= '9') {
        v = v * 10 + (*p - '0');
        p++;
    }
    *pp = p;
    return v;
}

/* Parse FEN string into engine_position_t.
   FEN: "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1" */
static const char *parse_fen(const char *p, engine_position_t *pos)
{
    int row = 0, col = 0;
    int8_t piece;

    memset(pos, 0, sizeof(*pos));

    /* 1. Piece placement */
    while (*p && *p != ' ') {
        if (*p == '/') {
            row++;
            col = 0;
        } else if (*p >= '1' && *p <= '8') {
            col += *p - '0';
        } else {
            piece = 0;
            switch (*p) {
                case 'P': piece =  1; break;  /* W_PAWN */
                case 'N': piece =  2; break;  /* W_KNIGHT */
                case 'B': piece =  3; break;  /* W_BISHOP */
                case 'R': piece =  4; break;  /* W_ROOK */
                case 'Q': piece =  5; break;  /* W_QUEEN */
                case 'K': piece =  6; break;  /* W_KING */
                case 'p': piece = -1; break;  /* B_PAWN */
                case 'n': piece = -2; break;
                case 'b': piece = -3; break;
                case 'r': piece = -4; break;
                case 'q': piece = -5; break;
                case 'k': piece = -6; break;
            }
            if (row < 8 && col < 8)
                pos->board[row][col] = piece;
            col++;
        }
        p++;
    }

    /* 2. Side to move */
    p = skip_ws(p);
    pos->turn = (*p == 'w') ? 1 : -1;
    p++;

    /* 3. Castling */
    p = skip_ws(p);
    pos->castling = 0;
    while (*p && *p != ' ') {
        switch (*p) {
            case 'K': pos->castling |= ENGINE_CASTLE_WK; break;
            case 'Q': pos->castling |= ENGINE_CASTLE_WQ; break;
            case 'k': pos->castling |= ENGINE_CASTLE_BK; break;
            case 'q': pos->castling |= ENGINE_CASTLE_BQ; break;
        }
        p++;
    }

    /* 4. En passant */
    p = skip_ws(p);
    if (*p == '-') {
        pos->ep_row = ENGINE_EP_NONE;
        pos->ep_col = ENGINE_EP_NONE;
        p++;
    } else {
        pos->ep_col = p[0] - 'a';
        pos->ep_row = '8' - p[1];
        p += 2;
    }

    /* 5. Halfmove clock */
    p = skip_ws(p);
    pos->halfmove_clock = (uint8_t)parse_uint(&p);

    /* 6. Fullmove number */
    p = skip_ws(p);
    pos->fullmove_number = (uint16_t)parse_uint(&p);

    return p;
}

/* Format engine_move_t as UCI string (e.g. "e2e4") */
static void format_uci_move(engine_move_t m, char *out)
{
    out[0] = 'a' + m.from_col;
    out[1] = '8' - m.from_row;
    out[2] = 'a' + m.to_col;
    out[3] = '8' - m.to_row;
    out[4] = '\0';
    if (m.flags & ENGINE_FLAG_PROMOTION) {
        switch (m.flags & ENGINE_FLAG_PROMO_MASK) {
            case ENGINE_FLAG_PROMO_Q: out[4] = 'q'; break;
            case ENGINE_FLAG_PROMO_R: out[4] = 'r'; break;
            case ENGINE_FLAG_PROMO_B: out[4] = 'b'; break;
            case ENGINE_FLAG_PROMO_N: out[4] = 'n'; break;
        }
        out[5] = '\0';
    }
}

/* ========== Main ========== */

int main(void)
{
    const char *p;
    uint32_t max_time_ms, max_nodes;
    int variance;
    uint32_t book_ply;
    engine_hooks_t hooks;
    engine_position_t pos;
    engine_move_t move;
    char mbuf[6];

    /* Start hardware timer */
    timer_Enable(1, TIMER_CPU, TIMER_NOINT, TIMER_UP);
    time_reset();

    /* Parse command from cmd_slot:
       "<time_ms> <max_nodes> <variance> <book_ply> <fen>" */
    p = cmd_slot;
    p = skip_ws(p);
    max_time_ms = parse_uint(&p);
    p = skip_ws(p);
    max_nodes = parse_uint(&p);
    p = skip_ws(p);
    variance = (int)parse_uint(&p);
    p = skip_ws(p);
    book_ply = parse_uint(&p);
    p = skip_ws(p);

    /* Parse FEN and set position */
    parse_fen(p, &pos);

    /* Initialize engine with timer hook */
    hooks.time_ms = time_ms;
    engine_init(&hooks);
    engine_new_game();
    engine_set_position(&pos);

    /* Configure engine settings */
    engine_set_max_nodes(max_nodes);
    engine_set_move_variance(variance);
    if (book_ply > 0) {
        engine_set_use_book(1);
        engine_set_book_max_ply((uint8_t)book_ply);
    } else {
        engine_set_use_book(0);
    }

    /* Reset timer for search timing */
    time_reset();

    /* Search */
    move = engine_think(0, max_time_ms);

    /* Output result */
    if (move.from_row == ENGINE_SQ_NONE) {
        dbg_printf("MOVE none\n");
    } else {
        format_uci_move(move, mbuf);
        dbg_printf("MOVE %s\n", mbuf);
    }

    /* Signal emulator to terminate */
    *(volatile uint8_t *)0xFB0000 = 0;

    return 0;
}
