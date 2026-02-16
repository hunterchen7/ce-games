# TI-84 Plus CE Chess Engine
## Technical Planning Document (Engine Submodule Only)

**Target Platform:** eZ80 @ 48 MHz | 133 KB RAM | 3.5 MB Flash
**Target Elo:** 1400–1600 on TI-84 CE, 1800+ on desktop build
**Scope:** Engine library only — the UI/program already exists

---

## 1. Overview

This document covers the design of a standalone chess engine library meant to be integrated into an existing TI-84 Plus CE chess program. The engine is a git submodule with zero UI dependencies. It compiles for both eZ80 (calculator) and desktop (x86/ARM) via a UCI wrapper for testing and tuning.

`ce-games` generally keeps each game in a single `src/main.c`. The engine is an explicit exception: it remains a separate module so search/eval code can stay testable and reusable.

### Existing Program Interface

The existing program uses an `int8_t board[8][8]` (row 0 = black back rank, row 7 = white back rank) with piece constants `W_PAWN=1` through `W_KING=6` and negatives for black. The engine will use a different internal representation (0x88) for performance, but the public API will translate to/from the program's row/col format so the UI code requires minimal changes.

### What the Engine Provides

- Legal move generation (the existing program has no move validation)
- Legal move highlighting for the UI (given a selected square, which destinations are legal?)
- AI move computation (think for a given depth or time)
- Game state detection (check, checkmate, stalemate, draw by repetition/50-move)
- Position management (set up from the UI's board array, or maintain internally)

---

## 2. Repository Structure

```
engine/                    ← Git submodule root
  src/
    engine.h               ← Public API (the ONLY file the UI includes)
    engine.c               ← API implementation, board translation
    platform.h             ← Platform hook interface (time/input/data pointers)
    board.h / board.c      ← 0x88 board, piece lists, make/unmake, Zobrist
    movegen.h / movegen.c  ← Move generation (staged, pseudo-legal)
    search.h / search.c    ← Negamax, alpha-beta, iterative deepening, TT
    eval.h / eval.c        ← Evaluation function
    tt.h / tt.c            ← Transposition table
    types.h                ← Fixed-width types, move_t, constants
  test/
    perft.c                ← Perft test suite (desktop only)
    test_positions.c       ← Tactical test positions (WAC, etc.)
    test_main.c            ← Test runner
  uci/
    uci.c                  ← Desktop UCI wrapper (desktop only)
  Makefile                 ← Desktop build (gcc/clang → test binary + UCI engine)
```

**Key rule:** Engine core modules (`board/search/movegen/eval/tt`) include no TI-specific headers. TI-only code lives in a tiny adapter on the host side and is passed in via callbacks/hooks.

---

## 3. Public API (`engine.h`)

This is the only header the UI code includes. All internal state is hidden.

```c
#ifndef ENGINE_H
#define ENGINE_H

#include <stdint.h>

/* ---- Types ---- */

typedef uint32_t (*engine_time_ms_fn)(void);
typedef uint8_t  (*engine_input_pending_fn)(void);

typedef struct {
    engine_time_ms_fn time_ms;              /* required for time-limited search */
    engine_input_pending_fn input_pending;  /* optional, for cooperative pondering */
} engine_hooks_t;

typedef struct {
    int8_t board[8][8];    /* UI encoding: W_PAWN=1..W_KING=6, negatives for black */
    int8_t turn;           /* 1 = white, -1 = black */
    uint8_t castling;      /* ENGINE_CASTLE_* bits */
    uint8_t ep_row;        /* 0..7, or ENGINE_EP_NONE */
    uint8_t ep_col;        /* 0..7, or ENGINE_EP_NONE */
    uint8_t halfmove_clock;
    uint16_t fullmove_number;
} engine_position_t;

typedef struct {
    uint8_t from_row;   /* 0-7, matching the UI's board[row][col] */
    uint8_t from_col;   /* 0-7 */
    uint8_t to_row;     /* 0-7 */
    uint8_t to_col;     /* 0-7 */
    uint8_t flags;      /* ENGINE_FLAG_* bits */
} engine_move_t;

typedef struct {
    uint8_t has_rook_move;  /* castling side effect */
    uint8_t rook_from_row, rook_from_col;
    uint8_t rook_to_row, rook_to_col;
    uint8_t has_ep_capture; /* en passant side effect */
    uint8_t ep_capture_row, ep_capture_col;
} engine_move_effects_t;

/* Castling rights */
#define ENGINE_CASTLE_WK  0x01
#define ENGINE_CASTLE_WQ  0x02
#define ENGINE_CASTLE_BK  0x04
#define ENGINE_CASTLE_BQ  0x08
#define ENGINE_EP_NONE    0xFF
#define ENGINE_SQ_NONE    0xFF

/* Flag bits */
#define ENGINE_FLAG_CAPTURE     0x01
#define ENGINE_FLAG_CASTLE      0x02
#define ENGINE_FLAG_EN_PASSANT  0x04
#define ENGINE_FLAG_PROMOTION   0x08
#define ENGINE_FLAG_PROMO_Q     0x00  /* default when PROMOTION set */
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
#define ENGINE_STATUS_DRAW_MAT   6  /* insufficient material */

/* ---- Lifecycle ---- */

void engine_init(const engine_hooks_t *hooks);
void engine_new_game(void);
void engine_cleanup(void);

/* ---- Position ---- */

/* Fully sync engine position from host state. */
void engine_set_position(const engine_position_t *pos);
void engine_get_position(engine_position_t *out);

/* ---- Legal Moves ---- */

/* Get all legal moves for the piece at (row, col).
   Returns number of moves written to out[] (up to max).
   If no piece or wrong color, returns 0. */
uint8_t engine_get_moves_from(uint8_t row, uint8_t col,
                              engine_move_t *out, uint8_t max);

/* Get all legal moves for the side to move.
   Returns count written to out[] (up to max). */
uint8_t engine_get_all_moves(engine_move_t *out, uint8_t max);

/* Check if any move from->to is legal (promotion choice not distinguished). */
uint8_t engine_is_legal(uint8_t from_r, uint8_t from_c,
                        uint8_t to_r, uint8_t to_c);
/* Full legality check including promotion flags. */
uint8_t engine_is_legal_move(engine_move_t move);

/* ---- Making Moves ---- */

/* Apply a move to the engine's internal position.
   Returns the game status after the move (ENGINE_STATUS_*).
   The UI should call this after animating; the engine updates
   its internal board, hash, repetition history, etc. */
uint8_t engine_make_move(engine_move_t move);
/* Computes side effects from the CURRENT position; call before engine_make_move(). */
void engine_get_move_effects(engine_move_t move, engine_move_effects_t *fx);

/* ---- AI ---- */

/* Limits:
   - max_depth == 0: no depth cap
   - max_time_ms == 0: no time cap
   - if both are 0, engine falls back to depth 1
   Returns move with from_row=ENGINE_SQ_NONE if no legal move exists. */
engine_move_t engine_think(uint8_t max_depth, uint16_t max_time_ms);

/* Optional (Phase 4): cooperative pondering for single-threaded UI loops. */
void engine_ponder_begin(engine_move_t predicted_reply);
void engine_ponder_step(uint16_t node_budget);
void engine_ponder_stop(void);

/* ---- Query ---- */

/* Current game status without making a move. */
uint8_t engine_get_status(void);

/* Is the side to move in check? */
uint8_t engine_in_check(void);

#endif /* ENGINE_H */
```

### Integration with the Existing UI

The existing UI code changes are minimal:

1. **On game start/load:** build an `engine_position_t` (board, turn, castling, EP square, 50-move clocks) and call `engine_set_position()`.
2. **On piece selection:** call `engine_get_moves_from(sel_r, sel_c, moves, 64)` to get legal destinations for highlighting.
3. **Before animation:** call `engine_is_legal(sel_r, sel_c, cur_r, cur_c)` for destination checks, then `engine_is_legal_move()` once promotion type is chosen.
4. **Before applying board side effects:** call `engine_get_move_effects(move, &fx)` so the UI can animate castling rook moves and en-passant captures.
5. **After animation:** call `engine_make_move(move)` to update hash/history/status.
6. **For AI turn:** call `engine_think(depth, time_ms)` with at least one non-zero limit, then animate the returned move.
7. **Promotion:** the engine returns multiple legal moves with different `ENGINE_FLAG_PROMO_*` bits. The UI picks one and passes it to `engine_make_move()`.
8. **Optional pondering (Phase 4):** UI main loop calls `engine_ponder_step(node_budget)` during idle frames.

---

## 4. Internal Board Representation

### 4.1 The 0x88 Board

Internally the engine uses a 128-byte array where valid squares satisfy `index & 0x88 == 0`. This gives free off-board detection with a single AND instruction (no branching, no bounds checks). The 0x88 index for a square is `row * 16 + col`.

```
  0x88 index layout:

  00 01 02 03 04 05 06 07 | 08 09 0A 0B 0C 0D 0E 0F  ← rank 8 (row 0)
  10 11 12 13 14 15 16 17 | 18 19 1A 1B 1C 1D 1E 1F  ← rank 7 (row 1)
  ...                     |
  70 71 72 73 74 75 76 77 | 78 79 7A 7B 7C 7D 7E 7F  ← rank 1 (row 7)
                          |
  valid squares           | invalid (off-board)
```

Conversion between the UI's `(row, col)` and the engine's 0x88 index:
```c
#define RC_TO_SQ(r, c)  ((r) * 16 + (c))
#define SQ_TO_ROW(sq)   ((sq) >> 4)
#define SQ_TO_COL(sq)   ((sq) & 7)
#define SQ_VALID(sq)    (!((sq) & 0x88))
```

### 4.2 Piece Encoding

```c
#define PIECE_NONE    0
#define PIECE_PAWN    1
#define PIECE_KNIGHT  2
#define PIECE_BISHOP  3
#define PIECE_ROOK    4
#define PIECE_QUEEN   5
#define PIECE_KING    6

#define COLOR_WHITE   0x00
#define COLOR_BLACK   0x80
#define COLOR_MASK    0x80
#define TYPE_MASK     0x07

#define MAKE_PIECE(color, type)  ((color) | (type))
#define PIECE_TYPE(p)            ((p) & TYPE_MASK)
#define PIECE_COLOR(p)           ((p) & COLOR_MASK)
```

Translation from the UI's `W_PAWN=1..W_KING=6` / `B_PAWN=-1..B_KING=-6`:
```c
uint8_t ui_to_engine_piece(int8_t ui_piece) {
    if (ui_piece == 0) return PIECE_NONE;
    if (ui_piece > 0) return MAKE_PIECE(COLOR_WHITE, ui_piece);
    return MAKE_PIECE(COLOR_BLACK, -ui_piece);
}
```

### 4.3 Board State Structure

```c
typedef struct {
    uint8_t squares[128];         /* 0x88 board array */
    uint8_t piece_list[2][16];    /* sq index for each piece, per side */
    uint8_t piece_count[2];       /* number of pieces per side */
    uint8_t king_sq[2];           /* king square per side */
    uint8_t side;                 /* 0 = white, 1 = black */
    uint8_t castling;             /* 4 bits: WK WQ BK BQ */
    uint8_t ep_square;            /* en passant target square, or 0xFF */
    uint8_t halfmove;             /* halfmove clock (50-move rule) */
    uint16_t fullmove;            /* fullmove counter */
    uint32_t hash;                /* Zobrist hash */
    uint16_t lock;                /* independent TT lock key */
} board_t;
```

Total: ~182 bytes.

### 4.4 Castling Rights Encoding

```c
#define CASTLE_WK  0x01  /* white kingside */
#define CASTLE_WQ  0x02  /* white queenside */
#define CASTLE_BK  0x04  /* black kingside */
#define CASTLE_BQ  0x08  /* black queenside */
```

A pre-computed `castling_rights[128]` table (stored in flash) maps each square to a mask that is AND'd with the castling rights after any move touching that square. For example, `castling_rights[0x00]` (a8 rook) = `~CASTLE_BQ`, `castling_rights[0x74]` (e1 king) = `~(CASTLE_WK | CASTLE_WQ)`.

---

## 5. Move Representation

```c
typedef struct __attribute__((packed)) {
    uint8_t from;       /* 0x88 square index */
    uint8_t to;         /* 0x88 square index */
    uint8_t flags;      /* capture, castle, EP, promotion, promo type */
    int16_t score;      /* move ordering score (set by pick_move) */
} move_t;
```

5 bytes per move. Flag encoding:

`__attribute__((packed))` avoids padding between `flags` and `score`. If profiling shows `pick_move`/`score_moves` bottlenecks from unaligned 16-bit access, reorder fields (e.g. `score, from, to, flags`) and remove `packed`.

| Bit(s) | Meaning |
|--------|---------|
| 0 | Capture |
| 1 | Castling |
| 2 | En passant |
| 3 | Promotion |
| 4–5 | Promotion type: 00=Q, 01=R, 10=B, 11=N |
| 6 | Double pawn push |
| 7 | (reserved) |

### Move Pool

A global array of 2,048 `move_t` entries (~10 KB with 5-byte moves) shared across all search plies via a stack pointer. The search is depth-first, so only moves for the current ply and its ancestors coexist.

`MOVE_POOL_SIZE` is compile-time configurable (`2048` default, `4096` fallback if overflow diagnostics trigger in deep searches).

```c
static move_t move_pool[2048];
static uint16_t move_sp;  /* stack pointer into pool */

/* Each ply records its base and count */
typedef struct {
    uint16_t base;    /* index into move_pool */
    uint8_t  count;   /* number of moves generated */
} ply_moves_t;
```

---

## 6. Make / Unmake

### 6.1 Undo Stack

```c
typedef struct {
    uint8_t  captured;      /* piece on destination (PIECE_NONE if quiet) */
    uint8_t  castling;      /* previous castling rights */
    uint8_t  ep_square;     /* previous en passant square */
    uint8_t  halfmove;      /* previous halfmove clock */
    uint32_t hash;          /* previous Zobrist hash */
    uint16_t lock;          /* previous TT lock key */
    uint8_t  moved_piece;   /* piece that moved (for unmake) */
    uint8_t  flags;         /* move flags (for unmake of castling/EP) */
} undo_t;
```

~12 bytes per ply. Stack depth of 64 plies = ~768 bytes.

### 6.2 Make

```c
void board_make(board_t *b, move_t m, undo_t *u);
```

1. Save undo state (castling, EP, halfmove, hash, lock, captured piece).
2. Update halfmove clock (reset on capture or pawn move).
3. Move piece: `squares[to] = squares[from]; squares[from] = PIECE_NONE`.
4. Handle special cases: castling (move rook), en passant (remove captured pawn), double push (set EP square), promotion (replace pawn with promoted piece).
5. Update `castling &= castling_rights[from] & castling_rights[to]`.
6. Update piece list (scan for `from`, replace with `to`; on capture, remove victim).
7. Update king square if king moved.
8. Incrementally update Zobrist hash + lock (XOR out old piece/square, XOR in new).
9. Flip side to move.

### 6.3 Unmake

```c
void board_unmake(board_t *b, move_t m, const undo_t *u);
```

Reverse of make: restore piece positions, castling, EP, halfmove, hash, and lock from undo entry. No need to recompute anything — the undo entry has all prior state.

---

## 7. Move Generation

### 7.1 Direction Tables (Flash)

```c
/* static const tables are placed in program flash by the toolchain */
static const int8_t knight_offsets[8] = {
    -33, -31, -18, -14, 14, 18, 31, 33
};
static const int8_t bishop_offsets[4] = { -17, -15, 15, 17 };
static const int8_t rook_offsets[4]   = { -16, -1, 1, 16 };
static const int8_t king_offsets[8]   = { -17, -16, -15, -1, 1, 15, 16, 17 };
/* queen uses bishop_offsets + rook_offsets */
```

### 7.2 Generation Strategy

The generator iterates over the piece list (not the board), producing moves for each piece by type:

- **Pawns:** Direction depends on color (+16 or -16). Check single push, double push (from starting rank), captures (diagonal ±1), en passant. Generate 4 moves for each promotion (Q/R/B/N).
- **Knights:** 8 fixed offsets. Check `SQ_VALID(target)` and target not friendly.
- **Bishops/Rooks/Queen:** Loop along each direction offset until `!SQ_VALID(target)` or hitting a piece. If enemy piece, add capture and stop. If friendly, stop.
- **King:** 8 fixed offsets (same as above but no looping). Castling checked separately.

### 7.3 Staged Generation

In the search, moves are generated in stages to maximize beta-cutoff efficiency:

```
Stage 1: TT move (no generation — just try the stored best move)
Stage 2: Generate captures → score with MVV-LVA → pick best
Stage 3: Killer moves (2 per ply, already stored)
Stage 4: Generate quiet moves → score with history heuristic → pick best
```

The generator accepts a mode parameter:

```c
#define GEN_ALL      0
#define GEN_CAPTURES 1
#define GEN_QUIETS   2

uint8_t generate_moves(const board_t *b, move_t *list, uint8_t mode);
uint8_t generate_moves_from(const board_t *b, uint8_t from_sq, move_t *list);
```

For the UI's `engine_get_moves_from()`, use `generate_moves_from()` to avoid generating unrelated moves.

### 7.4 Legality Filtering

Generation is pseudo-legal. After `board_make()`, we check if the king is in check via `is_square_attacked()`. If yes, `board_unmake()` and skip the move.

```c
uint8_t is_square_attacked(const board_t *b, uint8_t sq, uint8_t by_side);
```

This function is the workhorse — reused for:
- Legality filtering (is own king attacked after move?)
- Check detection (is current side's king attacked?)
- Castling validation (king doesn't cross attacked squares)
- Checkmate/stalemate detection

Implementation: for each attack direction from `sq`, walk outward and check if an appropriate attacker exists. Also check knight offsets. No move generation needed — this is a pure query.

---

## 8. Move Ordering

| Priority | Source | Score | Storage |
|----------|--------|-------|---------|
| 1 (highest) | TT best move | 127 | From TT probe |
| 2 | Winning captures (MVV-LVA) | 64 + `mvv_lva[victim][attacker]` | 49-byte lookup table |
| 3 | Killer move 1 | 52 | `uint16_t killer[64][2]` packed moves |
| 4 | Killer move 2 | 51 | Same array (`64 * 2 * 2 = 256 B`) |
| 5 (lowest) | Quiet moves (history) | `history[side][to_sq]` scaled to 0–50 | `int8_t[2][128]` = 256 B |

**Selection sort** (`pick_move`): scan the remaining move list, swap the highest-scored move to the front, return it. No full sort — we stop as soon as a beta cutoff occurs.

```c
void score_moves(move_t *list, uint8_t count, uint8_t ply, move_t tt_move);
move_t pick_move(move_t *list, uint8_t count, uint8_t index);
```

### MVV-LVA Table

```
Victim →    P   N   B   R   Q   K
Attacker ↓
P          15  25  25  35  45  0
N          14  24  24  34  44  0
B          13  23  23  33  43  0
R          12  22  22  32  42  0
Q          11  21  21  31  41  0
K          10  20  20  30  40  0
```

Higher value = search first. Stored as `uint8_t mvv_lva[7][7]` (49 bytes).

---

## 9. Search

### 9.1 Core: Negamax with Alpha-Beta

```
int16_t negamax(board_t *b, int8_t depth, int16_t alpha, int16_t beta, uint8_t ply) {
    // 1. Check time / depth limits
    // 2. TT probe → early return on exact hit or cutoff
    // 3. If depth <= 0 → quiescence search
    // 4. Null move pruning (if depth >= 3, not in check, has non-pawn material)
    // 5. Generate moves (staged)
    // 6. Loop over moves:
    //    a. pick_move (selection sort)
    //    b. board_make
    //    c. legality check (unmake if illegal, continue)
    //    d. Late move reductions (after move 4, quiet moves, reduce by 1)
    //    e. score = -negamax(b, depth - 1, -beta, -alpha, ply + 1)
    //    f. board_unmake
    //    g. Update alpha, check beta cutoff
    //    h. Update killer / history on cutoff
    // 7. Detect checkmate (no legal moves + in check) or stalemate (no legal moves)
    // 8. TT store
    // 9. Return best score
}
```

### 9.2 Iterative Deepening

```
engine_move_t engine_think(uint8_t max_depth, uint16_t max_time_ms) {
    engine_move_t best_move = { ENGINE_SQ_NONE, 0, 0, 0, 0 };
    uint8_t have_completed_iteration = 0;
    if (max_depth == 0 && max_time_ms == 0) max_depth = 1; /* safe fallback */
    for (depth = 1; (max_depth == 0) || (depth <= max_depth); depth++) {
        score = negamax(board, depth, -INF, +INF, 0);
        if (time_expired()) break; /* abort incomplete iteration */
        best_move = root_best;  // save completed iteration's result
        have_completed_iteration = 1;
    }
    if (!have_completed_iteration) best_move = first_legal_or_invalid();
    return best_move;  // from last COMPLETED iteration
}
```

### 9.3 Quiescence Search

At depth ≤ 0, search only captures (and check evasions when in check) until the position is quiet.

```
int16_t quiescence(board_t *b, int16_t alpha, int16_t beta, uint8_t ply) {
    if (in_check(b, b->side)) {
        generate_moves(b, evasions, GEN_ALL);  /* legal-filtered evasions only */
        for each legal evasion:
            board_make → score = -quiescence(...) → board_unmake
            update alpha, check beta cutoff
        return alpha;
    } else {
        int16_t stand_pat = evaluate(b);
        if (stand_pat >= beta) return beta;
        if (stand_pat > alpha) alpha = stand_pat;

        // Delta pruning: skip if stand_pat + QUEEN_VALUE < alpha

        generate_moves(b, captures, GEN_CAPTURES);
        for each legal capture (MVV-LVA order):
            board_make → score = -quiescence(...) → board_unmake
            update alpha, check beta cutoff
    }

    return alpha;
}
```

Depth-limited to 4–6 plies beyond the main search horizon.

### 9.4 Enhancements

| Technique | When | Details | Elo Gain |
|-----------|------|---------|----------|
| **Transposition table** | Every node | Probe before search, store after. See §10. | +200–300 |
| **Null move pruning** | depth ≥ 3, not in check, non-pawn material | Pass (skip turn), search at depth - 1 - R (R=2). If score ≥ beta, prune. | +75–100 |
| **Late move reductions** | After move 4, quiet moves, not in check | Reduce depth by 1. Re-search at full depth if score > alpha. | +50–75 |
| **Check extensions** | When side-to-move is in check | Extend search by 1 ply. Prevents horizon effect on tactics. | +25–50 |
| **Futility pruning** | depth ≤ 2, not in check | Skip quiet moves where static eval + margin < alpha. | +25–50 |

### 9.5 Time Management

The search checks elapsed time every 1,024 nodes. When the budget is exhausted mid-iteration, the search aborts and returns the best move from the last completed iteration.

```c
static uint32_t node_count;
static uint32_t search_start_ms;
static uint32_t search_deadline_ms;

static int time_check(void) {
    if ((node_count & 1023) == 0 && search_deadline_ms) {
        if (hooks.time_ms() >= search_deadline_ms) return 1;
    }
    return 0;
}
```

### 9.6 Pondering

Pondering is deferred to **Phase 4 (stretch goal)**. The MVP engine is non-pondering.

If enabled later, pondering is cooperative (single-threaded): the UI loop gives the engine short work slices via `engine_ponder_step(node_budget)`.

On a ponder hit (opponent plays the predicted move), the TT is already loaded with relevant positions. On a miss, the TT still has useful data from shared subtrees.

Implementation requirement: explicit resumable search state (not a blocking background loop).

---

## 10. Transposition Table

### 10.1 Entry Format

```c
typedef struct {
    uint16_t lock16;      /* independent 16-bit verification key */
    int16_t  score;       /* evaluation score */
    uint16_t best_move16; /* packed from64/to64/promo type */
    int8_t   depth;       /* search depth */
    uint8_t  flag;        /* TT_EXACT, TT_ALPHA, TT_BETA */
} tt_entry_t;
```

8 bytes per entry. `best_move16` preserves promotion choice:

```
bits 0-5:   from square (0..63)
bits 6-11:  to square (0..63)
bit  12:    is_promotion
bits 13-14: promo piece (Q/R/B/N)
bit  15:    reserved
```

### 10.2 Sizing

The TT gets whatever RAM remains after all other allocations. Use power-of-2 sizes only (e.g., 2048, 4096, 8192 entries) for fast index computation:

```c
#define TT_SIZE  4096               /* entries (power of two) */
#define TT_MASK  (TT_SIZE - 1)
static tt_entry_t tt[TT_SIZE];     /* 32,768 bytes */

uint16_t tt_index(uint32_t hash) {
    return (uint16_t)(hash & TT_MASK);
}
```

### 10.3 Replacement Policy

Always-replace. Simple and effective for small tables — keeps the most recent positions, which are most likely to be revisited.

### 10.4 Mate Score Adjustment

Mate scores are stored relative to the TT entry, not the root. Before storing: `score - ply`. After retrieval: `score + ply`. This ensures mate-in-N is reported correctly regardless of where the position appears in the search tree.

---

## 11. Zobrist Hashing

### 11.1 Key Set

```c
/* Primary hash keys (index square as 0..63, not 0x88) */
uint32_t zobrist_piece[12][64];
uint32_t zobrist_castle[16];
uint32_t zobrist_ep_file[8];
uint32_t zobrist_side;

/* Independent 16-bit lock keys for TT verification */
uint16_t lock_piece[12][64];
uint16_t lock_castle[16];
uint16_t lock_ep_file[8];
uint16_t lock_side;
```

Total: ~4.7 KB. Stored in flash as an AppVar and loaded by a platform adapter (`ti_GetDataPtr()` on calculator, static arrays/files on desktop).

### 11.2 Incremental Update

The hash is updated incrementally inside `board_make()`:
```c
uint8_t from64 = sq88_to_sq64(from);
uint8_t to64   = sq88_to_sq64(to);

hash ^= zobrist_piece[old_piece][from64];  /* remove piece from origin */
hash ^= zobrist_piece[old_piece][to64];    /* place piece at destination */
if (captured) hash ^= zobrist_piece[captured][to64];  /* remove victim */
hash ^= zobrist_side;                    /* flip side */
/* + castling and EP key updates as needed */

lock ^= lock_piece[old_piece][from64];
lock ^= lock_piece[old_piece][to64];
if (captured) lock ^= lock_piece[captured][to64];
lock ^= lock_side;
```

TT indexing uses only the low `log2(TT_SIZE)` bits (12 bits for 4096 entries), and verification uses `lock16`. Effective probe discrimination is 28 bits at TT_SIZE=4096. Therefore TT hits and TT best moves are **always** legality-checked before use.

---

## 12. Evaluation

### 12.1 Principle

Speed over sophistication. Every centipawn of eval complexity that costs search depth is a net loss on this hardware. The eval targets ~50–100 instructions per call.

### 12.2 Terms

**Material (always active):**

| Piece | Value (cp) |
|-------|------------|
| Pawn | 100 |
| Knight | 320 |
| Bishop | 330 |
| Rook | 500 |
| Queen | 900 |

Computed incrementally: maintained as a running sum, updated in make/unmake. Cost: 0 at eval time.

**Piece-Square Tables (always active):**

One `int8_t[64]` table per piece type per phase (middlegame and endgame). 6 types × 2 phases × 64 squares = 768 bytes, stored in flash.

The PST score is also maintained incrementally in make/unmake (add destination PST value, subtract origin PST value). Cost at eval time: 0.

**Tapered Evaluation:**

```c
#define MAX_PHASE 256

int16_t phase = scaled_phase_0_to_256(total_non_pawn_material);
int16_t mg_score = material_mg + pst_mg;
int16_t eg_score = material_eg + pst_eg;
int16_t score = (mg_score * phase + eg_score * (MAX_PHASE - phase)) >> 8;
```

Two multiplies + one shift; no integer divide in the hot path.

**Pawn Structure (Phase 4):**

- Doubled pawns: -15cp per doubled pawn (two pawns on same file)
- Isolated pawns: -10cp (no friendly pawns on adjacent files)
- Passed pawns: +20cp base, +10cp per rank advanced

Scan the pawn piece list (≤8 pawns per side). Not incrementally maintained — recomputed at eval time, but the piece list keeps it fast.

**King Safety (Phase 4):**

- Pawn shield: +10cp per friendly pawn on the 3 files around the king, on the 2 ranks in front of it. Quick scan of 6 squares max.

**Bishop Pair (Phase 2):**

- +30cp if side has ≥2 bishops. One comparison per side.

### 12.3 Texel Tuning

After the desktop UCI build works, all eval weights (piece values, PST entries, pawn penalties, king safety bonuses) are optimized via Texel's tuning method: minimize the mean squared error between `sigmoid(eval)` and actual game outcomes from a database of master games. The tuned tables are then packaged as an AppVar for the calculator. This gives the quality of machine-learned evaluation with zero runtime cost.

---

## 13. Repetition and Draw Detection

### 13.1 Threefold Repetition

An array of `uint32_t` hashes for every position in the game (from the root, plus up to 64 search plies). Before each move in the search, check if the current hash has appeared before.

```c
static uint32_t position_history[512];  /* ~2 KB */
static uint16_t history_count;
static uint16_t history_irreversible;   /* index after last pawn move/capture */

uint8_t is_repetition(uint32_t hash) {
    int i;
    if (history_count < 4) return 0;
    for (i = (int)history_count - 4; i >= (int)history_irreversible; i -= 2) {
        if (position_history[i] == hash) return 1;
    }
    return 0;
}
```

Step by 2 because the same side can only repeat on every other move. Limit the scan to reversible moves only. In the search, a single repetition returns a draw score (0) to avoid repetitions by default.

### 13.2 Fifty-Move Rule

The `halfmove` counter in the board state counts half-moves since the last capture or pawn move. If it reaches 100, the game is a draw.

### 13.3 Insufficient Material

Minimum support: K vs K, K+B vs K, K+N vs K, K+B vs K+B (same-color bishops only). Checked in `engine_get_status()`.

---

## 14. Memory Budget (Engine Only)

### 14.1 RAM

| Item | Size | Notes |
|------|------|-------|
| **Transposition table** | 32,768 B | 4,096 entries × 8 B. The big item. |
| **Move pool** | 10,240 B | 2,048 × 5 B (`move_t`). Shared by search + UI queries. |
| **Board state** | ~182 B | 0x88 board + piece lists + flags |
| **Undo stack** | ~768 B | 64 plies × 12 B |
| **Killers** | 256 B | packed move16, 2 moves × 64 plies |
| **History table** | 256 B | `int8_t[2][128]` |
| **Position history** | 2,048 B | 512 hashes for repetition detection |
| **MVV-LVA table** | 49 B | `uint8_t[7][7]` |
| **Search state** | ~64 B | Counters, flags, time tracking |
| **TOTAL** | **~45.6 KB** | |

### 14.2 Flash (AppVars)

| AppVar | Size | Notes |
|--------|------|-------|
| Zobrist + lock keys | ~4.7 KB | 64-square indexing + independent 16-bit lock keys |
| Piece-square tables | ~768 B | 6 types × 2 phases × 64 int8 |
| Opening book (stretch) | 12–30 KB | Position hash → move |
| Endgame tablebases (stretch) | 24–50 KB | KPK, KRK (future) |

All accessed through the platform adapter at zero RAM cost (`ti_GetDataPtr()` on calculator).

### 14.3 Code Size Estimate

| Module | Size |
|--------|------|
| board.c (make/unmake, 0x88 ops) | 3–5 KB |
| movegen.c (generation, legality) | 5–8 KB |
| search.c (negamax, quiescence, ID) | 6–10 KB |
| eval.c (evaluation) | 2–4 KB |
| tt.c (transposition table) | 1–2 KB |
| engine.c (API, translation) | 2–3 KB |
| **TOTAL** | **19–32 KB** |

---

## 15. Desktop Build & Testing

### 15.1 UCI Wrapper

`uci/uci.c` implements the Universal Chess Interface protocol, wrapping the engine API:

- `uci` → print engine ID
- `isready` → `readyok`
- `position startpos moves ...` → `engine_new_game()` + `engine_make_move()` for each move
- `go depth N` / `go movetime N` → `engine_think(N, ...)` → print `bestmove`
- `quit` → exit

This lets the engine play in CuteChess, Arena, or any UCI-compatible GUI for Elo testing.

### 15.2 Perft Testing

Perft counts the total number of leaf nodes at a given depth from a position. This validates that move generation, make/unmake, and legality filtering are all correct.

| Position | Depth | Expected Nodes |
|----------|-------|---------------|
| Starting position | 5 | 4,865,609 |
| Kiwipete | 4 | 4,085,603 |
| Position 3 | 5 | 674,624 |
| Position 4 | 5 | 15,833,292 |
| Position 5 | 4 | 2,103,487 |
| Position 6 | 4 | 3,894,594 |

**Rule: no search code is written until all perft tests pass.**

### 15.3 Strength Testing

Run automated tournaments via CuteChess-cli against engines of known strength:

- TSCP (~1724 Elo on desktop)
- Micro-Max (~1900 Elo)
- Vice (~2000 Elo)

100+ game matches at fixed time controls (e.g. 10s+0.1s) to estimate Elo with confidence intervals.

Desktop Elo numbers are used for regression tracking only; calculator strength must be measured separately on CE-like time budgets in CEmu/real hardware.

### 15.4 CE Build Integration (Submodule in a Single-File Game Repo)

`ce-games` conventions keep each game in `src/main.c`, but the chess engine is intentionally multi-file. Keep the UI as single-file while linking engine sources separately.

Recommended approach:

1. Keep chess UI logic in `chess/src/main.c`.
2. Add engine submodule under `chess/engine/`.
3. Extend `chess/Makefile` to compile/link engine sources explicitly (or build a static `libengine.a` and link it).
4. Export only `engine/src/engine.h` to the UI.

This preserves the repository style for gameplay code while avoiding an unmaintainable unity-build for search code.

---

## 16. Development Phases

### Phase 1: Move Generation & Validation

**Deliverable:** Desktop library passing all perft tests.

1. Implement `types.h` — piece encoding, move_t, flag constants.
2. Implement `board.c` — 0x88 board, `engine_position_t` translation, piece list management.
3. Implement `movegen.c` — `generate_moves()` and `generate_moves_from()`.
4. Implement `board_make()` / `board_unmake()` with undo stack.
5. Implement `is_square_attacked()`.
6. Implement Zobrist hashing (incremental in make/unmake).
7. Write perft test suite. **All 6 positions must pass before proceeding.**

**Exit criterion:** Perft depth 5+ correct for all test positions.

### Phase 2: Search & Evaluation

**Deliverable:** UCI engine playing ~1200–1400 Elo on desktop.

1. Implement `eval.c` — material + PST (incremental), tapered eval.
2. Implement `tt.c` — transposition table with packed promotion-aware move and lock16 verification.
3. Implement `search.c` — negamax + alpha-beta + iterative deepening.
4. Implement quiescence search with capture search + in-check evasions.
5. Implement move ordering — TT move, MVV-LVA, killers, history.
6. Build UCI wrapper. Test in CuteChess.
7. Add null move pruning, LMR, check extensions, futility pruning.
8. Add repetition detection (reversible-history bounded, no underflow) and 50-move rule.

**Exit criterion:** 1200+ Elo in CuteChess tournaments.

### Phase 3: Integration with Existing Program

**Deliverable:** The existing UI program uses the engine for validation and AI.

1. Add `engine.c` — API layer translating between UI's `board[8][8]` and engine's 0x88.
2. Wire `engine_init(hooks)` from the UI (time source, optional input callback).
3. Modify UI: game start/load passes full `engine_position_t` (board, turn, castling, EP, clocks).
4. Modify UI: piece selection calls `engine_get_moves_from()` for legal move highlighting.
5. Modify UI: move execution checks `engine_is_legal()` before animating.
6. Modify UI: call `engine_get_move_effects()` so castling rook moves and en-passant captures animate correctly.
7. Modify UI: after animation, call `engine_make_move()` and check game status.
8. Implement AI turn: call `engine_think()`, animate the result.
9. Package Zobrist keys and PSTs as AppVars.
10. Test in CEmu.

**Exit criterion:** Full game playable in CEmu with legal move enforcement and AI opponent.

### Phase 4: Optimization & Polish

**Deliverable:** 1500–1700 Elo on TI-84 CE, stable on real hardware.

1. Texel-tune eval weights on desktop, flash to calculator.
2. Add pawn structure and king safety eval terms.
3. Add opening book AppVar (12–30 KB, stretch).
4. Add endgame tablebases (KPK, stretch).
5. Implement optional cooperative pondering (`engine_ponder_step`) if profiling shows benefit.
6. Profile in CEmu — identify hot paths for potential assembly optimization.
7. Add difficulty levels (depth 3–6 mapped to AI difficulty 1–10).
8. Test on real TI-84 Plus CE hardware.

**Exit criterion:** 1500+ Elo on CE time budgets and stable play on real hardware.

---

## 17. Risks

| Risk | Mitigation |
|------|------------|
| Engine code too large (>30 KB) | Compile with `-Oz`. Move tables to flash. Simplify eval if needed. |
| RAM overflow (TT + move pool + board) | Shrink TT first (always the biggest item). Reduce move pool to 2048. |
| Movegen bugs cause silent search errors | Perft before search. Non-negotiable. Run perft in CI. |
| Too slow on calculator | Profile in CEmu. Assembly for `is_square_attacked()` and `make/unmake` (hottest paths). Reduce eval complexity. |
| Translation bugs (UI ↔ engine) | Write unit tests for `engine_set_position()` translation and move conversion (including promotion/castling/EP side effects). |
| Build integration drift (single-file game vs multi-file engine) | Keep `main.c` as UI-only, link engine as submodule/static lib, and add CI build for both desktop and CE targets. |
| Hash/TT collisions cause incorrect play | TT probe discrimination is 28 bits at TT_SIZE=4096; therefore TT hits and TT moves are always legality-checked before use. |
| Pondering complexity hurts responsiveness | Keep pondering optional and cooperative (`engine_ponder_step`), only enable after profiling shows net gain. |
