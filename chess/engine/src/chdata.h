#ifndef CHDATA_H
#define CHDATA_H

/*
 * CHDATA.8xv layout — shared chess data appvar
 *
 * Offset 0:     piece sprite data (6 pieces × 20 × 20 bytes)
 * Offset 2400:  Polyglot random numbers (781 × 8-byte uint64, LE)
 */

#define CHDATA_APPVAR     "CHDATA"
#define CHDATA_SPR_OFFSET 0
#define CHDATA_SPR_SIZE   2400   /* 6 × 20 × 20 */
#define CHDATA_RND_OFFSET 2400
#define CHDATA_RND_SIZE   6248   /* 781 × 8 */
#define CHDATA_TOTAL_SIZE 8648

#endif /* CHDATA_H */
