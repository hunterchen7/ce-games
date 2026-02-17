#ifndef BOARD_H
#define BOARD_H

#include "types.h"

/* ========== Board State ========== */

typedef struct {
    uint8_t  squares[128];       /* 0x88 board array */
    uint8_t  piece_list[2][16];  /* square index per piece, per side */
    uint8_t  piece_index[128];   /* square -> piece_list index, 0xFF if empty */
    uint8_t  piece_count[2];     /* number of pieces per side */
    uint8_t  bishop_count[2];    /* number of bishops per side */
    uint8_t  king_sq[2];         /* king square per side */
    uint8_t  side;               /* WHITE or BLACK */
    uint8_t  castling;           /* CASTLE_WK | CASTLE_WQ | CASTLE_BK | CASTLE_BQ */
    uint8_t  ep_square;          /* en passant target square, or SQ_NONE */
    uint8_t  halfmove;           /* halfmove clock (50-move rule) */
    uint16_t fullmove;           /* fullmove counter */
    uint32_t pawn_hash;          /* Zobrist hash of pawns only (for eval cache) */
    uint32_t hash;               /* Zobrist hash */
    uint16_t lock;               /* independent TT lock key */
    /* incremental eval (material + PST combined) */
    int16_t  mg[2];              /* middlegame score per side */
    int16_t  eg[2];              /* endgame score per side */
    uint8_t  phase;              /* game phase (24=opening, 0=endgame) */
} board_t;

/* ========== Undo State ========== */

typedef struct {
    uint8_t  captured;    /* piece on destination (PIECE_NONE if quiet) */
    uint8_t  castling;    /* previous castling rights */
    uint8_t  ep_square;   /* previous en passant square */
    uint8_t  halfmove;    /* previous halfmove clock */
    uint16_t fullmove;    /* previous fullmove counter */
    uint32_t pawn_hash;   /* previous pawn-only Zobrist hash */
    uint32_t hash;        /* previous Zobrist hash */
    uint16_t lock;        /* previous TT lock key */
    uint8_t  moved_piece; /* piece that moved (for unmake) */
    uint8_t  flags;       /* move flags (for unmake of castling/EP) */
} undo_t;

/* ========== UI Piece Constants (for translation) ========== */

#define UI_EMPTY     0
#define UI_W_PAWN    1
#define UI_W_KNIGHT  2
#define UI_W_BISHOP  3
#define UI_W_ROOK    4
#define UI_W_QUEEN   5
#define UI_W_KING    6

/* ========== Functions ========== */

/* Initialize board to empty state */
void board_init(board_t *b);

/* Set board from the UI's int8_t board[8][8] encoding.
   ui_board[row][col]: row 0 = black back rank (rank 8), row 7 = white back rank (rank 1).
   Piece values: W_PAWN=1..W_KING=6, negatives for black, 0=empty.
   turn: 1 = white, -1 = black.
   castling: ENGINE_CASTLE_* bits.
   ep_row/ep_col: en passant target, or 0xFF for none.
   halfmove_clock, fullmove_number: clock values. */
void board_set_from_ui(board_t *b,
                       const int8_t ui_board[8][8],
                       int8_t turn,
                       uint8_t castling,
                       uint8_t ep_row, uint8_t ep_col,
                       uint8_t halfmove_clock,
                       uint16_t fullmove_number);

/* Set board to standard starting position */
void board_startpos(board_t *b);

/* Make/unmake a move */
void board_make(board_t *b, move_t m, undo_t *u);
void board_unmake(board_t *b, move_t m, const undo_t *u);

/* Translate a UI piece (signed int8) to engine piece encoding */
uint8_t ui_to_engine_piece(int8_t ui_piece);

/* Translate an engine piece back to UI encoding */
int8_t engine_to_ui_piece(uint8_t piece);

#endif /* BOARD_H */
