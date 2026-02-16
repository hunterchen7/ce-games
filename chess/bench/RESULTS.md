# TI-84 CE Chess Engine Benchmark Results

Benchmarked on eZ80 @ 48 MHz (cycle-accurate emulator).
5 test positions, averaged over 1000 iterations per position.

## Commits

| #   | Commit    | Optimization                               |
| --- | --------- | ------------------------------------------ |
| 0   | `6198639` | **Baseline** (pre-optimizations)           |
| 1   | `1b2436d` | O(1) piece-list updates (square index map) |
| 2   | `898f712` | Staged movegen (captures then quiets)      |
| 3   | `09a9d02` | Skip castling attack probes when in check  |
| 4   | `0868f82` | Precompute check/pin info                  |
| 5   | `b8d7cf9` | Track bishop counts incrementally          |

## Memory

| Commit         | `board_t` | `undo_t` | `move_t` |
| -------------- | --------- | -------- | -------- |
| baseline       | 186 B     | 14 B     | 3 B      |
| piece_index    | 314 B     | 14 B     | 3 B      |
| staged_movegen | 314 B     | 14 B     | 3 B      |
| castle_skip    | 314 B     | 14 B     | 3 B      |
| check_pin      | 314 B     | 14 B     | 3 B      |
| bishop_count   | 316 B     | 14 B     | 3 B      |

## Movegen (avg cy/call, 5 positions x 1000 iters)

| Commit         | P0     | P1     | P2      | P3     | P4      | **Avg**    | Delta          |
| -------------- | ------ | ------ | ------- | ------ | ------- | ---------- | -------------- |
| baseline       | 77,620 | 86,886 | 115,007 | 87,781 | 94,848  | **92,428** | —              |
| piece_index    | 77,616 | 86,886 | 115,003 | 87,781 | 94,844  | **92,426** | -2 (-0.0%)     |
| staged_movegen | 77,616 | 86,886 | 115,003 | 87,781 | 94,844  | **92,426** | -2 (-0.0%)     |
| castle_skip    | 85,264 | 94,644 | 115,095 | 95,579 | 102,520 | **98,620** | +6,192 (+6.7%) |
| check_pin      | 85,264 | 94,644 | 115,095 | 95,579 | 102,520 | **98,620** | +6,192 (+6.7%) |
| bishop_count   | 85,264 | 94,644 | 115,095 | 95,579 | 102,520 | **98,620** | +6,192 (+6.7%) |

## Make/Unmake (avg cy/pair, 5 positions x 1000 iters)

| Commit         | P0     | P1     | P2     | P3     | P4     | **Avg**    | Delta        |
| -------------- | ------ | ------ | ------ | ------ | ------ | ---------- | ------------ |
| baseline       | 14,412 | 16,779 | 14,467 | 16,779 | 23,593 | **17,206** | —            |
| piece_index    | 14,724 | 17,091 | 14,779 | 17,091 | 20,714 | **16,880** | -326 (-1.9%) |
| staged_movegen | 14,724 | 17,091 | 14,779 | 17,091 | 20,714 | **16,880** | -326 (-1.9%) |
| castle_skip    | 14,724 | 17,091 | 14,779 | 17,091 | 20,714 | **16,880** | -326 (-1.9%) |
| check_pin      | 14,724 | 17,091 | 14,779 | 17,091 | 20,714 | **16,880** | -326 (-1.9%) |
| bishop_count   | 14,786 | 17,153 | 14,841 | 17,153 | 20,907 | **16,968** | -238 (-1.4%) |

## Eval (avg cy/call, 5 positions x 1000 iters)

| Commit         | P0      | P1      | P2      | P3      | P4      | **Avg**     | Delta          |
| -------------- | ------- | ------- | ------- | ------- | ------- | ----------- | -------------- |
| baseline       | 135,084 | 149,067 | 163,343 | 158,146 | 152,963 | **151,720** | —              |
| piece_index    | 135,088 | 149,071 | 163,347 | 158,150 | 152,967 | **151,724** | +4 (+0.0%)     |
| staged_movegen | 135,088 | 149,071 | 163,347 | 158,150 | 152,967 | **151,724** | +4 (+0.0%)     |
| castle_skip    | 135,088 | 149,071 | 163,347 | 158,150 | 152,967 | **151,724** | +4 (+0.0%)     |
| check_pin      | 135,088 | 149,071 | 163,347 | 158,150 | 152,967 | **151,724** | +4 (+0.0%)     |
| bishop_count   | 125,484 | 139,797 | 154,227 | 149,392 | 145,175 | **142,815** | -8,905 (-5.9%) |

## Perft (startpos, total cycles)

| Commit         | depth 1 | depth 2    | depth 3         | Delta (d3)          |
| -------------- | ------- | ---------- | --------------- | ------------------- |
| baseline       | 569,086 | 12,695,258 | **270,543,361** | —                   |
| piece_index    | 546,478 | 11,679,130 | **256,768,371** | -13,774,990 (-5.1%) |
| staged_movegen | 546,478 | 11,679,130 | **256,768,371** | -13,774,990 (-5.1%) |
| castle_skip    | 554,126 | 11,838,438 | **260,005,599** | -10,537,762 (-3.9%) |
| check_pin      | 554,126 | 11,838,438 | **260,005,599** | -10,537,762 (-3.9%) |
| bishop_count   | 555,366 | 11,864,478 | **260,588,017** | -9,955,344 (-3.7%)  |

## Single Op (startpos, single call)

| Commit         | movegen (cy) | attacked(e1) (cy) | mk/unmk avg (cy) | eval (cy) |
| -------------- | ------------ | ----------------- | ---------------- | --------- |
| baseline       | 77,658       | 7,572             | 16,326           | 135,130   |
| piece_index    | 77,654       | 7,572             | 15,200           | 135,134   |
| staged_movegen | 77,654       | 7,572             | 15,200           | 135,134   |
| castle_skip    | 85,302       | 7,572             | 15,200           | 135,134   |
| check_pin      | 85,302       | 7,572             | 15,200           | 135,134   |
| bishop_count   | 85,302       | 7,572             | 15,262           | 125,530   |

## Benchmark Positions (50)

Positions drawn from well-known chess engine test suites.

| #   | Description                        | Source                    |
| --- | ---------------------------------- | ------------------------- |
| P0  | Starting position                  | Chessprogramming Wiki     |
| P1  | Kiwipete (Peter McKenzie)          | Chessprogramming Wiki     |
| P2  | Sparse endgame                     | Chessprogramming Wiki     |
| P3  | Promotion-heavy                    | Chessprogramming Wiki     |
| P4  | Pawn on d7 promotes                | Chessprogramming Wiki     |
| P5  | Steven Edwards symmetrical         | Chessprogramming Wiki     |
| P6  | Illegal EP — bishop pins pawn (W)  | TalkChess / Martin Sedlak |
| P7  | Illegal EP — bishop pins pawn (B)  | TalkChess / Martin Sedlak |
| P8  | EP gives discovered check          | TalkChess / Martin Sedlak |
| P9  | Short castling gives check         | TalkChess / Martin Sedlak |
| P10 | Long castling gives check          | TalkChess / Martin Sedlak |
| P11 | Castling rights lost by rook cap   | TalkChess / Martin Sedlak |
| P12 | Castling prevented by attack       | TalkChess / Martin Sedlak |
| P13 | Promote out of check               | TalkChess / Martin Sedlak |
| P14 | Discovered check                   | TalkChess / Martin Sedlak |
| P15 | Promote to give check              | TalkChess / Martin Sedlak |
| P16 | Under-promote to avoid stalemate   | TalkChess / Martin Sedlak |
| P17 | Self stalemate                     | TalkChess / Martin Sedlak |
| P18 | Stalemate vs checkmate             | TalkChess / Martin Sedlak |
| P19 | Rook vs bishop endgame             | Peterellisjones           |
| P20 | EP discovered check (bishop a2)    | Peterellisjones           |
| P21 | Kiwipete variant — Qe6+            | Peterellisjones           |
| P22 | Simple rook vs pawn                | Peterellisjones           |
| P23 | Illegal EP — king exposed to rook  | Peterellisjones           |
| P24 | Bishop pin prevents EP             | Peterellisjones           |
| P25 | Tactical middlegame                | Stockfish benchmark.cpp   |
| P26 | Open game                          | Stockfish benchmark.cpp   |
| P27 | Sicilian-type                      | Stockfish benchmark.cpp   |
| P28 | Attacking position                 | Stockfish benchmark.cpp   |
| P29 | Active rook                        | Stockfish benchmark.cpp   |
| P30 | Closed pawns                       | Stockfish benchmark.cpp   |
| P31 | Pawn endgame                       | Stockfish benchmark.cpp   |
| P32 | Rook + pawn endgame                | Stockfish benchmark.cpp   |
| P33 | Bishop + pawn endgame              | Stockfish benchmark.cpp   |
| P34 | Rook endgame passed pawn           | Stockfish benchmark.cpp   |
| P35 | Minor piece endgame                | Stockfish benchmark.cpp   |
| P36 | Queen middlegame                   | Stockfish benchmark.cpp   |
| P37 | Opposite colored bishops           | Stockfish benchmark.cpp   |
| P38 | Promotion bug catcher              | TalkChess movegen tests   |
| P39 | Mirrored position 4                | TalkChess movegen tests   |
| P40 | EP after double push (real game)   | TalkChess movegen tests   |
| P41 | Complex castling + EP + extra rook | TalkChess movegen tests   |
| P42 | Real game endgame with EP          | TalkChess movegen tests   |
| P43 | Deep endgame — Q+R+B               | TalkChess movegen tests   |
| P44 | Castling with rook threat          | TalkChess movegen tests   |
| P45 | Castling + pawn structure + pins   | TalkChess movegen tests   |
| P46 | Pure pawn race                     | TalkChess movegen tests   |
| P47 | K+P endgame — distant pawns        | TalkChess movegen tests   |
| P48 | Realistic middlegame — both castle | TalkChess movegen tests   |
| P49 | Double check position              | TalkChess movegen tests   |

## 5s Search on eZ80 (50 positions)

Searched each of 50 benchmark positions for 5 seconds on the eZ80 (48 MHz, cycle-accurate emulator).
Post-optimization engine (commit `293b90d`, includes movegen optimizations).

| Pos | Nodes | Depth |   ms | Pos | Nodes | Depth |   ms |
| --- | ----: | ----: | ---: | --- | ----: | ----: | ---: |
| P0  |   539 |     3 | 5903 | P25 |   690 |     1 | 8167 |
| P1  |   509 |     1 | 5150 | P26 |  1111 |     2 | 7905 |
| P2  |  2945 |     5 | 5885 | P27 |   835 |     1 | 8833 |
| P3  |     0 |     0 | 5157 | P28 |   153 |     1 | 9000 |
| P4  |  1132 |     3 | 6777 | P29 |   450 |     1 | 7888 |
| P5  |   350 |     1 | 5137 | P30 |  2234 |     5 | 6685 |
| P6  |  3222 |     6 | 6110 | P31 |  2665 |     6 | 6258 |
| P7  |  3345 |     6 | 6036 | P32 |  1383 |     4 | 6612 |
| P8  |  2382 |     5 | 5730 | P33 |  1511 |     4 | 5786 |
| P9  |  3847 |     5 | 5760 | P34 |  2414 |     4 | 6418 |
| P10 |  4035 |     5 | 5731 | P35 |   521 |     3 | 6912 |
| P11 |   442 |     3 | 6462 | P36 |   562 |     2 | 6922 |
| P12 |  1355 |     3 | 6699 | P37 |  1372 |     3 | 5349 |
| P13 |   992 |     3 | 5169 | P38 |  1648 |     3 | 6871 |
| P14 |  1771 |     3 | 5588 | P39 |  1024 |     0 | 5241 |
| P15 |  1795 |     5 | 5083 | P40 |   742 |     3 | 7667 |
| P16 |  1370 |     5 | 5377 | P41 |  3052 |     5 | 6257 |
| P17 |  3370 |    11 | 5693 | P42 |   966 |     3 | 6307 |
| P18 |  3442 |     6 | 5974 | P43 |  1537 |     3 | 5215 |
| P19 |   219 |     1 | 5793 | P44 |  3662 |     5 | 5922 |
| P20 |  4783 |     5 | 6089 | P45 |  1816 |     4 | 6186 |
| P21 |   192 |     1 | 9007 | P46 |  2435 |     7 | 5276 |
| P22 |  1871 |     5 | 5099 | P47 |  4792 |     7 | 6180 |
| P23 |  3647 |     5 | 5749 | P48 |  1173 |     3 | 7028 |
| P24 |  2101 |     5 | 5718 | P49 |  1698 |     3 | 5291 |

- **Total: 90,102 nodes** across 50 positions
- **Average: 1,802 nodes/position** in 5 seconds (~360 NPS)
- Depth range: 0-11 (simple endgames reach d6-11, complex middlegames d1-3)
- Time overshoot due to time check granularity (every 1024 nodes)

## 10s Search on eZ80 (50 positions)

Searched each of 50 benchmark positions for 10 seconds on the eZ80 (48 MHz, cycle-accurate emulator).

| Pos | Nodes | Depth |    ms | Pos | Nodes | Depth |    ms |
| --- | ----: | ----: | ----: | --- | ----: | ----: | ----: |
| P0  |  2379 |     4 | 11441 | P25 |  3128 |     2 | 13227 |
| P1  |  1522 |     2 | 12096 | P26 |  1987 |     3 | 12952 |
| P2  |  2733 |     5 | 11393 | P27 |   855 |     1 | 10291 |
| P3  |   979 |     1 | 11120 | P28 |  2780 |     2 | 10630 |
| P4  |  2000 |     4 | 10662 | P29 |  3313 |     2 | 12837 |
| P5  |  2821 |     2 | 12139 | P30 |  4001 |     6 | 10968 |
| P6  |  3622 |     6 | 10825 | P31 |  4357 |     7 | 10863 |
| P7  |  3705 |     6 | 11415 | P32 |  4951 |     5 | 10237 |
| P8  |  1993 |     5 | 10918 | P33 |  3334 |     5 | 10515 |
| P9  | 10147 |     6 | 10372 | P34 |  3800 |     4 | 10649 |
| P10 |  7950 |     6 | 10369 | P35 |   565 |     3 | 10163 |
| P11 |  4796 |     4 | 10192 | P36 |  3420 |     3 | 10216 |
| P12 |  3419 |     5 | 12037 | P37 |  3876 |     4 | 12224 |
| P13 |  8135 |     5 | 10345 | P38 |  4110 |     4 | 10046 |
| P14 |  3714 |     4 | 10113 | P39 |   924 |     1 | 11395 |
| P15 |  9328 |     6 | 11173 | P40 |  4028 |     4 | 11361 |
| P16 |  6071 |     6 | 10205 | P41 |  4930 |     6 | 10184 |
| P17 |  7437 |    16 | 10970 | P42 |   884 |     3 | 11601 |
| P18 |  6515 |     8 | 10440 | P43 |  3200 |     5 | 13086 |
| P19 |   997 |     3 | 11036 | P44 |  6051 |     6 | 10177 |
| P20 |  7232 |     5 | 10916 | P45 |  2558 |     4 | 10468 |
| P21 |   192 |     1 | 11798 | P46 |  5360 |     8 | 10184 |
| P22 |  2310 |     5 | 10352 | P47 |  5389 |     7 | 11146 |
| P23 |  3214 |     5 | 10308 | P48 |  1672 |     3 | 10623 |
| P24 |  5998 |     6 | 10588 | P49 |  7918 |     5 | 10470 |

- **Total: 196,600 nodes** across 50 positions
- **Average: 3,932 nodes/position** in 10 seconds (~393 NPS)
- Depth range: 1-16 (simple endgames reach d7-16, complex middlegames d1-3)

## 15s Search on eZ80 (50 positions)

Searched each of 50 benchmark positions for 15 seconds on the eZ80 (48 MHz, cycle-accurate emulator).

| Pos | Nodes | Depth |    ms | Pos | Nodes | Depth |    ms |
| --- | ----: | ----: | ----: | --- | ----: | ----: | ----: |
| P0  |  5344 |     5 | 18297 | P25 |  3706 |     2 | 16558 |
| P1  |  1492 |     2 | 16452 | P26 |  2109 |     3 | 15593 |
| P2  |  9549 |     6 | 16124 | P27 |  2699 |     3 | 16102 |
| P3  |  4339 |     3 | 18145 | P28 |  3612 |     3 | 17609 |
| P4  |  2114 |     4 | 17293 | P29 |  4099 |     3 | 16229 |
| P5  |  4059 |     3 | 16149 | P30 |  4613 |     6 | 15206 |
| P6  |  7630 |     7 | 15098 | P31 |  4304 |     7 | 16141 |
| P7  |  8430 |     7 | 15852 | P32 |  5729 |     5 | 16677 |
| P8  |  9014 |     7 | 15625 | P33 |  3397 |     5 | 15754 |
| P9  |  9736 |     6 | 15395 | P34 |  4252 |     4 | 15498 |
| P10 |  7489 |     6 | 15367 | P35 |  8150 |     4 | 16313 |
| P11 |  6684 |     5 | 15654 | P36 |  6597 |     4 | 17183 |
| P12 |  3268 |     5 | 16759 | P37 |  3791 |     4 | 16743 |
| P13 |  7821 |     5 | 15237 | P38 |  7131 |     5 | 16175 |
| P14 |  7835 |     5 | 15407 | P39 |  4313 |     3 | 18334 |
| P15 | 12388 |     7 | 15581 | P40 |   914 |     3 | 15831 |
| P16 |  9016 |     6 | 15086 | P41 |  6043 |     7 | 15242 |
| P17 | 11653 |    17 | 15209 | P42 |  5720 |     5 | 15878 |
| P18 |  9750 |     9 | 15321 | P43 |  3208 |     5 | 16275 |
| P19 |   988 |     3 | 16061 | P44 | 11703 |     7 | 15159 |
| P20 |  7338 |     5 | 15977 | P45 |  5376 |     5 | 15659 |
| P21 |   192 |     1 | 16086 | P46 |  5311 |     8 | 15733 |
| P22 |  2285 |     5 | 15390 | P47 | 10753 |     8 | 15356 |
| P23 | 10085 |     6 | 15220 | P48 |  5360 |     4 | 15917 |
| P24 |  7655 |     7 | 15268 | P49 | 10541 |     6 | 16236 |

- **Total: 299,585 nodes** across 50 positions
- **Average: 5,992 nodes/position** in 15 seconds (~399 NPS)
- Depth range: 1-17 (simple endgames reach d7-17, complex middlegames d1-3)

## Tournament vs Stockfish

the methodology here is to limit nodes to avg. nodes it got in the 50 benchmark positions and then have the node-limited engine play against stockfish at diff. ratings. obv. this is flawed since nodes/pos differs by a good margin, but this should still paint a decent picture.

### Node-limited (1800 nodes, 0.1s/move, XXL book)

Simulates eZ80 playing strength (~1800 nodes per move based on 5s search bench).

| SF Elo | W   | D   | L   | Score   | Pct | Elo diff |
| ------ | --- | --- | --- | ------- | --- | -------- |
| 1700   | 13  | 3   | 14  | 14.5/30 | 48% | -12      |
| 1800   | 7   | 8   | 15  | 11.0/30 | 37% | -95      |
| 1900   | 4   | 9   | 17  | 8.5/30  | 28% | -161     |
| 2000   | 5   | 10  | 15  | 10.0/30 | 33% | -120     |
| 2100   | 4   | 7   | 19  | 7.5/30  | 25% | -191     |

**Estimated eZ80 Elo: ~1700** (50% mark vs SF-1700)

### Node-limited (4000 nodes, 0.1s/move, no book)

PGN: [`chess/engine/tournament_4000_nobook.pgn`](../../chess/engine/tournament_4000_nobook.pgn)

Simulates ~10s search on eZ80 (~3,932 nodes per move based on 10s search bench).

| SF Elo | W   | D   | L   | Score   | Pct | Elo diff |
| ------ | --- | --- | --- | ------- | --- | -------- |
| 1700   | 19  | 4   | 7   | 21.0/30 | 70% | +147     |
| 1800   | 14  | 2   | 14  | 15.0/30 | 50% | +0       |
| 1900   | 12  | 3   | 15  | 13.5/30 | 45% | -35      |
| 2000   | 9   | 6   | 15  | 12.0/30 | 40% | -70      |
| 2100   | 8   | 9   | 13  | 12.5/30 | 42% | -58      |

**Estimated eZ80 Elo: ~1800** (50% mark vs SF-1800)

### Node-limited (6000 nodes, 0.1s/move, no book)

PGN: [`chess/engine/tournament_6000_nobook.pgn`](../../chess/engine/tournament_6000_nobook.pgn), [`chess/engine/tournament_6000_1950.pgn`](../../chess/engine/tournament_6000_1950.pgn)

Simulates ~15s search on eZ80 (~5,992 nodes per move based on 15s search bench).

| SF Elo | W   | D   | L   | Score   | Pct | Elo diff |
| ------ | --- | --- | --- | ------- | --- | -------- |
| 1700   | 22  | 1   | 7   | 22.5/30 | 75% | +191     |
| 1800   | 15  | 7   | 8   | 18.5/30 | 62% | +83      |
| 1900   | 13  | 6   | 11  | 16.0/30 | 53% | +23      |
| 1950   | 12  | 6   | 12  | 15.0/30 | 50% | +0       |
| 2000   | 9   | 7   | 14  | 12.5/30 | 42% | -58      |
| 2100   | 12  | 8   | 10  | 16.0/30 | 53% | +23      |

**Estimated eZ80 Elo: ~1950** (50% mark vs SF-1950)

### "Unleashed" (0.1s/move, no node limit, XXL book)

PGN: [`chess/engine/tournament.pgn`](../../chess/engine/tournament.pgn)

Desktop Arm64 search strength — shows the engine's algorithmic ceiling. (m5 macbook pro)

| SF Elo | W   | D   | L   | Score   | Pct | Elo diff |
| ------ | --- | --- | --- | ------- | --- | -------- |
| 1700   | 28  | 1   | 1   | 28.5/30 | 95% | +512     |
| 1800   | 27  | 2   | 1   | 28.0/30 | 93% | +458     |
| 1900   | 22  | 5   | 3   | 24.5/30 | 82% | +260     |
| 2000   | 26  | 3   | 1   | 27.5/30 | 92% | +417     |
| 2100   | 20  | 3   | 7   | 21.5/30 | 72% | +161     |
| 2200   | 19  | 8   | 3   | 23.0/30 | 77% | +207     |
| 2300   | 17  | 6   | 7   | 20.0/30 | 67% | +120     |
| 2400   | 16  | 7   | 7   | 19.5/30 | 65% | +108     |
| 2500   | 16  | 10  | 4   | 21.0/30 | 70% | +147     |
| 2600   | 12  | 11  | 7   | 17.5/30 | 58% | +58      |
| 2650   | 9   | 10  | 11  | 14.0/30 | 47% | -23      |
| 2700   | 5   | 13  | 12  | 11.5/30 | 38% | -83      |
| 2800   | 0   | 14  | 16  | 7.0/30  | 23% | -207     |
| 2900   | 1   | 8   | 21  | 5.0/30  | 17% | -280     |
| 3000   | 0   | 7   | 23  | 3.5/30  | 12% | -352     |

**Estimated desktop Elo: ~2650** (50% mark between SF-2600 and SF-2700)

## Notes

- **Staged movegen** shows no change in movegen/perft benchmarks because it only reorders moves (captures first, then quiets). The benefit is in alpha-beta search where it improves move ordering and pruning.
- **Castle skip** increases movegen cost by +6,192 cy/call (+6.7%) because the additional `in_check` test at the start of movegen adds overhead that exceeds the savings from skipping castling attack probes. These 5 test positions are mostly not in check, so the optimization rarely triggers.
- **Bishop count** saves 8,905 cy/eval (-5.9%) by tracking bishop counts incrementally in make/unmake instead of counting them during eval. The make/unmake cost increases slightly (+88 cy/pair).
- **Piece index** is the biggest perft win: -5.1% total cycles at depth 3, primarily through faster make/unmake operations.
- At 48 MHz, perft(3) from startpos takes ~5.4s on the baseline and ~5.4s on the final version. The net cycle savings from all optimizations is -3.7% for perft(3).
