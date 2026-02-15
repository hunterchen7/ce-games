#ifndef BOOK_H
#define BOOK_H

#include "board.h"

#if BOOK_TIER != 0

/* Initialize the opening book (open AppVar, get flash pointer).
   Returns 1 on success, 0 if AppVar not found. */
uint8_t book_init(void);

/* Probe the book for the current position.
   If a book move is found, writes it to *out and returns 1.
   Returns 0 if no book move available. */
uint8_t book_probe(board_t *b, move_t *out);

/* Release the AppVar handle. */
void book_close(void);

#else

/* No-op stubs when book is disabled */
#define book_init()          ((uint8_t)0)
#define book_probe(b, out)   ((uint8_t)0)
#define book_close()         ((void)0)

#endif /* BOOK_TIER != 0 */

#endif /* BOOK_H */
