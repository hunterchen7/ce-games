#ifndef BOOK_H
#define BOOK_H

#include "board.h"

#if BOOK_TIER != 0

/* Initialize the opening book (open AppVar, get flash pointer).
   Returns 1 on success, 0 if AppVar not found. */
uint8_t book_init(void);

/* Seed for book move randomization — set before probing */
extern uint32_t book_random_seed;

/* Probe the book for the current position.
   If a book move is found, writes it to *out and returns 1.
   Returns 0 if no book move available. */
uint8_t book_probe(board_t *b, move_t *out);

/* Release the AppVar handle. */
void book_close(void);

/* Diagnostic: fill out ready state, segment count, entry count */
void book_get_info(uint8_t *ready, uint8_t *n_seg, uint32_t *n_entries);

#else

/* No-op stubs when book is disabled */
#define book_init()                    ((uint8_t)0)
#define book_probe(b, out)             ((uint8_t)0)
#define book_close()                   ((void)0)
#define book_get_info(r, ns, ne)       do { *(r)=0; *(ns)=0; *(ne)=0; } while(0)

#endif /* BOOK_TIER != 0 */

#endif /* BOOK_H */
