#ifndef ENGINE_H
#define ENGINE_H

#include <stdint.h>

/* ---- Types ---- */

typedef uint32_t (*engine_time_ms_fn)(void);

typedef struct {
    engine_time_ms_fn time_ms;   /* required for time-limited search */
} engine_hooks_t;

typedef struct {
    int8_t board[8][8];    /* W_PAWN=1..W_KING=6, negatives for black, 0=empty */
    int8_t turn;           /* 1 = white, -1 = black */
    uint8_t castling;      /* ENGINE_CASTLE_* bits */
    uint8_t ep_row;        /* 0..7, or ENGINE_EP_NONE */
    uint8_t ep_col;        /* 0..7, or ENGINE_EP_NONE */
    uint8_t halfmove_clock;
    uint16_t fullmove_number;
} engine_position_t;

typedef struct {
    uint8_t from_row;   /* 0-7, matching board[row][col] */
    uint8_t from_col;   /* 0-7 */
    uint8_t to_row;     /* 0-7 */
    uint8_t to_col;     /* 0-7 */
    uint8_t flags;      /* ENGINE_FLAG_* bits */
} engine_move_t;

/* Castling rights */
#define ENGINE_CASTLE_WK  0x01
#define ENGINE_CASTLE_WQ  0x02
#define ENGINE_CASTLE_BK  0x04
#define ENGINE_CASTLE_BQ  0x08
#define ENGINE_EP_NONE    0xFF
#define ENGINE_SQ_NONE    0xFF

/* Flag bits (same encoding as internal flags) */
#define ENGINE_FLAG_CAPTURE     0x01
#define ENGINE_FLAG_CASTLE      0x02
#define ENGINE_FLAG_EN_PASSANT  0x04
#define ENGINE_FLAG_PROMOTION   0x08
#define ENGINE_FLAG_PROMO_Q     0x00
#define ENGINE_FLAG_PROMO_R     0x10
#define ENGINE_FLAG_PROMO_B     0x20
#define ENGINE_FLAG_PROMO_N     0x30
#define ENGINE_FLAG_PROMO_MASK  0x30

/* Game status */
#define ENGINE_STATUS_NORMAL     0
#define ENGINE_STATUS_CHECK      1
#define ENGINE_STATUS_CHECKMATE  2
#define ENGINE_STATUS_STALEMATE  3
#define ENGINE_STATUS_DRAW_50    4
#define ENGINE_STATUS_DRAW_REP   5
#define ENGINE_STATUS_DRAW_MAT   6

/* ---- Lifecycle ---- */

void engine_init(const engine_hooks_t *hooks);
void engine_new_game(void);

/* ---- Position ---- */

void engine_set_position(const engine_position_t *pos);
void engine_get_position(engine_position_t *out);

/* ---- Legal Moves ---- */

uint8_t engine_get_moves_from(uint8_t row, uint8_t col,
                              engine_move_t *out, uint8_t max);
uint8_t engine_get_all_moves(engine_move_t *out, uint8_t max);
uint8_t engine_is_legal_move(engine_move_t move);

/* ---- Move Side Effects ---- */

typedef struct {
    uint8_t has_rook_move;  /* castling: rook also moves */
    uint8_t rook_from_row, rook_from_col;
    uint8_t rook_to_row, rook_to_col;
    uint8_t has_ep_capture; /* en passant: captured pawn not on destination */
    uint8_t ep_capture_row, ep_capture_col;
} engine_move_effects_t;

/* Compute side effects of a move from the CURRENT position.
   Call BEFORE engine_make_move() so the UI can animate properly. */
void engine_get_move_effects(engine_move_t move, engine_move_effects_t *fx);

/* ---- Making Moves ---- */

uint8_t engine_make_move(engine_move_t move);

/* ---- AI ---- */

void engine_set_max_nodes(uint32_t n);
void engine_set_use_book(uint8_t enabled);
void engine_set_book_max_ply(uint8_t ply); /* 0 = unlimited */
void engine_set_eval_noise(int noise);
void engine_set_move_variance(int cp); /* pick randomly among moves within N cp of best */
engine_move_t engine_think(uint8_t max_depth, uint32_t max_time_ms);

/* ---- Benchmark ---- */

typedef struct {
    uint32_t nodes;
    uint8_t depth;
} engine_bench_result_t;

engine_bench_result_t engine_bench(uint8_t max_depth, uint32_t max_time_ms);

/* ---- Query ---- */

uint8_t engine_get_status(void);
uint8_t engine_in_check(void);

/* ---- Book Diagnostics ---- */

typedef struct {
    uint8_t  ready;         /* 1 if book loaded successfully */
    uint8_t  num_segments;  /* number of AppVar chunks found */
    uint32_t total_entries; /* total book entries across all segments */
} engine_book_info_t;

void engine_get_book_info(engine_book_info_t *out);

/* Returns 1 if the last engine_think() returned a book move */
uint8_t engine_last_move_was_book(void);

/* ---- Cleanup ---- */

void engine_cleanup(void);

#endif /* ENGINE_H */
