#ifndef SEARCH_H
#define SEARCH_H

#include "board.h"

/* Time management callback */
typedef uint32_t (*time_ms_fn)(void);

/* Search result */
typedef struct {
    move_t best_move;
    int score;
    uint8_t depth;       /* depth of last completed iteration */
    uint32_t nodes;      /* total nodes searched */
} search_result_t;

/* Search limits */
typedef struct {
    uint8_t  max_depth;   /* 0 = no limit */
    uint32_t max_time_ms; /* 0 = no limit */
    uint32_t max_nodes;   /* 0 = no limit */
    time_ms_fn time_fn;   /* NULL = no time checks */
} search_limits_t;

/* Initialize search state (call once at startup or new game) */
void search_init(void);

/* Run iterative deepening search.
   Returns the best move and associated info.
   The board position is restored after search. */
search_result_t search_go(board_t *b, const search_limits_t *limits);

/* Position history for repetition detection.
   Must be maintained by the caller across moves. */
void search_history_push(uint32_t hash);
void search_history_pop(void);
void search_history_clear(void);
void search_history_set_irreversible(void);

/* ========== Search Profiling ========== */

#ifdef SEARCH_PROFILE

typedef struct {
    uint32_t eval_cy;
    uint32_t movegen_cy;
    uint32_t legal_info_cy;
    uint32_t moveorder_cy;
    uint32_t make_unmake_cy;
    uint32_t is_legal_cy;
    uint32_t tt_cy;
    uint32_t eval_cnt;
    uint32_t movegen_cnt;
    uint32_t make_cnt;     /* make/unmake pairs for legal moves */
    uint32_t legal_cnt;    /* board_is_legal calls */
    uint32_t tt_cnt;       /* tt_probe + tt_store calls */
    uint32_t nodes;
} search_profile_t;

void search_profile_reset(void);
const search_profile_t *search_profile_get(void);

#endif /* SEARCH_PROFILE */

#endif /* SEARCH_H */
