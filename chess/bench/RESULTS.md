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

## Texel Tuning Elo (Desktop Paired H2H)

Paired match setup:

- Start positions sampled from `chess/engine/tuning/data/texel_positions_1m.csv`
- 2000 FEN pairs (4000 games total), colors swapped per pair
- `movetime = 0.03s`, `max_fullmoves = 180`

| Comparison                             | Score       | Pct    | Elo Diff | 95% CI (Elo)     | Result JSON                                                    |
| -------------------------------------- | ----------- | ------ | -------- | ---------------- | -------------------------------------------------------------- |
| Round1 1M Texel vs Pre-tuning (`HEAD`) | 2296.5/4000 | 57.41% | +51.89   | [+41.97, +61.89] | `chess/engine/tuning/results/h2h_round1_vs_pretune_4000g.json` |
| Round2 1M Texel vs Round1 1M Texel     | 1991.0/4000 | 49.78% | -1.56    | [-11.29, +8.16]  | `chess/engine/tuning/results/h2h_round2_vs_round1_4000g.json`  |
| Round3 1M Texel vs Round1 1M Texel     | 1970.0/4000 | 49.25% | -5.21    | [-14.92, +4.48]  | `chess/engine/tuning/results/h2h_round3_vs_round1_4000g.json`  |

Current kept eval parameters: **Round1 1M Texel**.

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
Pre-Texel HCE, pre eval-optimization.

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
Pre-Texel HCE, pre eval-optimization.

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

## 5s Search on eZ80 — post-Texel (50 positions, 2026-02-16)

Searched each of 50 benchmark positions for 5 seconds on the eZ80 (48 MHz, cycle-accurate emulator).
Post eval-optimization + Texel tuning.

| Pos | Nodes | Depth |   ms | Pos | Nodes | Depth |   ms |
| --- | ----: | ----: | ---: | --- | ----: | ----: | ---: |
| P0  |   566 |     3 | 5176 | P25 |  1339 |     1 | 5118 |
| P1  |   286 |     1 | 5029 | P26 |  1767 |     2 | 5079 |
| P2  |  2152 |     5 | 5102 | P27 |  1025 |     1 | 5242 |
| P3  |  1195 |     1 | 5553 | P28 |   149 |     1 | 5554 |
| P4  |   770 |     3 | 5330 | P29 |   425 |     1 | 5742 |
| P5  |  1065 |     1 | 5199 | P30 |  1664 |     5 | 5005 |
| P6  |  3640 |     6 | 5108 | P31 |  1345 |     5 | 5150 |
| P7  |  3441 |     6 | 5021 | P32 |  2455 |     5 | 5316 |
| P8  |  1978 |     5 | 5228 | P33 |  1696 |     4 | 5200 |
| P9  |  3504 |     5 | 5245 | P34 |  2967 |     4 | 5137 |
| P10 |  3895 |     5 | 5113 | P35 |   453 |     3 | 5285 |
| P11 |   524 |     3 | 5058 | P36 |   602 |     2 | 5068 |
| P12 |   418 |     3 | 5264 | P37 |   364 |     2 | 5170 |
| P13 |  2250 |     3 | 5063 | P38 |  1332 |     3 | 5255 |
| P14 |  1209 |     3 | 5051 | P39 |  1524 |     1 | 5595 |
| P15 |  1264 |     5 | 5191 | P40 |   854 |     3 | 5243 |
| P16 |  1048 |     5 | 5177 | P41 |  1637 |     5 | 5258 |
| P17 |  3662 |    13 | 5164 | P42 |   868 |     3 | 5452 |
| P18 |  3543 |     7 | 5128 | P43 |  2086 |     4 | 5110 |
| P19 |   888 |     3 | 5152 | P44 |  3405 |     6 | 5123 |
| P20 |   388 |     3 | 5208 | P45 |  1793 |     4 | 5330 |
| P21 |   354 |     1 | 5752 | P46 |  2124 |     7 | 5019 |
| P22 |  2260 |     5 | 5041 | P47 |  2122 |     6 | 5304 |
| P23 |  2307 |     5 | 5195 | P48 |  1856 |     3 | 5050 |
| P24 |  1202 |     5 | 5032 | P49 |  1532 |     3 | 5014 |

- **Total: 81,193 nodes** across 50 positions
- **Average: 1,624 nodes/position** in 5 seconds (~325 NPS)
- Depth range: 1-13 (simple endgames reach d5-13, complex middlegames d1-3)
- Time overshoot much improved vs previous (~5.2s avg vs ~6.1s before)

## 10s Search on eZ80 — post-Texel (50 positions, 2026-02-16)

Searched each of 50 benchmark positions for 10 seconds on the eZ80 (48 MHz, cycle-accurate emulator).
Post eval-optimization + Texel tuning.

| Pos | Nodes | Depth |    ms | Pos | Nodes | Depth |    ms |
| --- | ----: | ----: | ----: | --- | ----: | ----: | ----: |
| P0  |   695 |     3 | 10628 | P25 |  3416 |     2 | 10622 |
| P1  |  1798 |     2 | 10374 | P26 |  3029 |     3 | 10490 |
| P2  |  2540 |     5 | 10393 | P27 |  3708 |     3 | 10568 |
| P3  |  2298 |     1 | 10240 | P28 |  2925 |     3 | 10069 |
| P4  |  2166 |     4 | 10524 | P29 |   838 |     1 | 10216 |
| P5  |  2525 |     2 | 10361 | P30 |  3317 |     6 | 10477 |
| P6  |  3377 |     6 | 10222 | P31 |  4493 |     6 | 10249 |
| P7  |  3479 |     6 | 10222 | P32 |  3073 |     5 | 10435 |
| P8  |  7737 |     6 | 10067 | P33 |  3663 |     5 | 10476 |
| P9  |  6758 |     5 | 10107 | P34 |  3867 |     4 | 10049 |
| P10 |  8227 |     6 | 10180 | P35 |   392 |     3 | 10273 |
| P11 |  5306 |     4 | 10423 | P36 |  3374 |     3 | 10297 |
| P12 |  3274 |     5 | 10321 | P37 |  3543 |     4 | 10258 |
| P13 |  1270 |     3 | 10153 | P38 |  4582 |     4 | 10254 |
| P14 |  3716 |     4 | 10081 | P39 |  2092 |     1 | 10105 |
| P15 |  8730 |     6 | 10122 | P40 |  1437 |     3 | 10097 |
| P16 |   978 |     5 | 10003 | P41 |  5526 |     6 | 10417 |
| P17 |  6588 |    14 | 10222 | P42 |   894 |     3 | 10168 |
| P18 |  6453 |     8 | 10074 | P43 |  3227 |     5 | 10312 |
| P19 |   966 |     3 | 10089 | P44 |  4969 |     6 | 10234 |
| P20 |  5481 |     5 | 10033 | P45 |  5789 |     5 | 10415 |
| P21 |   304 |     1 | 10373 | P46 |  4642 |     8 | 10104 |
| P22 |  2369 |     5 | 10195 | P47 |  4683 |     7 | 10171 |
| P23 |  3320 |     5 | 10119 | P48 |  2082 |     3 | 10360 |
| P24 |  8319 |     7 | 10094 | P49 |  7193 |     5 | 10365 |

- **Total: 185,428 nodes** across 50 positions
- **Average: 3,709 nodes/position** in 10 seconds (~371 NPS)
- Depth range: 1-14 (simple endgames reach d5-14, complex middlegames d1-3)

## 5s Search on eZ80 — pre-Texel HCE (50 positions, 2026-02-16)

Searched each of 50 benchmark positions for 5 seconds on the eZ80 (48 MHz, cycle-accurate emulator).
Hand-crafted eval weights before Texel tuning, no code optimizations.

| Pos | Nodes | Depth |   ms | Pos | Nodes | Depth |   ms |
| --- | ----: | ----: | ---: | --- | ----: | ----: | ---: |
| P0  |   638 |     3 | 5251 | P25 |  1536 |     0 | 5199 |
| P1  |   435 |     1 | 5131 | P26 |  1238 |     2 | 5810 |
| P2  |  2766 |     5 | 5065 | P27 |   881 |     1 | 5438 |
| P3  |   979 |     1 | 5938 | P28 |   162 |     1 | 5394 |
| P4  |   482 |     3 | 5270 | P29 |   387 |     1 | 5502 |
| P5  |   512 |     1 | 6047 | P30 |   771 |     4 | 5003 |
| P6  |  3622 |     6 | 5282 | P31 |  2301 |     6 | 5060 |
| P7  |  3568 |     6 | 5331 | P32 |  1479 |     4 | 5330 |
| P8  |  1895 |     5 | 5121 | P33 |  1599 |     4 | 5280 |
| P9  |  4235 |     5 | 5234 | P34 |  2773 |     3 | 5218 |
| P10 |  3897 |     5 | 5149 | P35 |   593 |     3 | 5101 |
| P11 |   483 |     3 | 5460 | P36 |   480 |     2 | 5182 |
| P12 |   484 |     3 | 5338 | P37 |  1726 |     3 | 5026 |
| P13 |   860 |     3 | 5223 | P38 |  1781 |     3 | 5176 |
| P14 |  1385 |     3 | 5325 | P39 |  1536 |     0 | 6006 |
| P15 |  1278 |     5 | 5081 | P40 |   964 |     3 | 5387 |
| P16 |  1207 |     5 | 5254 | P41 |  1797 |     5 | 5441 |
| P17 |  3541 |    13 | 5082 | P42 |   886 |     3 | 5021 |
| P18 |  3616 |     7 | 5198 | P43 |  2149 |     4 | 5571 |
| P19 |   991 |     3 | 5455 | P44 |  2234 |     5 | 5250 |
| P20 |   585 |     3 | 5247 | P45 |  2529 |     4 | 5288 |
| P21 |   192 |     1 | 5847 | P46 |  2145 |     7 | 5132 |
| P22 |  2317 |     5 | 5215 | P47 |  2426 |     6 | 5293 |
| P23 |  3262 |     5 | 5208 | P48 |  1761 |     3 | 5135 |
| P24 |  1544 |     5 | 5207 | P49 |  1576 |     3 | 5135 |

- **Total: 82,484 nodes** across 50 positions
- **Average: 1,650 nodes/position** in 5 seconds (~330 NPS)
- Depth range: 0-13 (simple endgames reach d5-13, complex middlegames d1-3)

## 10s Search on eZ80 — pre-Texel HCE (50 positions, 2026-02-16)

Searched each of 50 benchmark positions for 10 seconds on the eZ80 (48 MHz, cycle-accurate emulator).
Hand-crafted eval weights before Texel tuning, no code optimizations.

| Pos | Nodes | Depth |    ms | Pos | Nodes | Depth |    ms |
| --- | ----: | ----: | ----: | --- | ----: | ----: | ----: |
| P0  |  2379 |     4 | 10312 | P25 |  3281 |     2 | 10612 |
| P1  |  1522 |     2 | 10019 | P26 |  1957 |     3 | 10839 |
| P2  |  2733 |     5 | 10117 | P27 |   877 |     1 | 10251 |
| P3  |   979 |     1 | 10080 | P28 |  2625 |     2 | 10643 |
| P4  |  2000 |     4 | 10637 | P29 |  3314 |     2 | 10699 |
| P5  |   629 |     1 | 10299 | P30 |  3970 |     6 | 10409 |
| P6  |  3622 |     6 | 10158 | P31 |  4202 |     7 | 10101 |
| P7  |  3705 |     6 | 10214 | P32 |  5054 |     5 | 10170 |
| P8  |  1993 |     5 | 10285 | P33 |  3348 |     5 | 10462 |
| P9  | 10107 |     6 | 10047 | P34 |  4596 |     4 | 10158 |
| P10 |  7841 |     6 | 10016 | P35 |   564 |     3 | 10042 |
| P11 |  4794 |     4 | 10129 | P36 |  3407 |     3 | 10174 |
| P12 |  3493 |     5 | 10275 | P37 |  3727 |     4 | 10131 |
| P13 |  8632 |     5 | 10015 | P38 |  4121 |     4 | 10454 |
| P14 |  3367 |     4 | 10203 | P39 |   924 |     1 | 10354 |
| P15 |  1078 |     5 | 10168 | P40 |  1008 |     3 | 10211 |
| P16 |  5986 |     6 | 10060 | P41 |  5035 |     6 | 10150 |
| P17 |  5859 |    15 |  7374 | P42 |   884 |     3 | 10430 |
| P18 |  5984 |     8 | 10050 | P43 |  3225 |     5 | 10194 |
| P19 |   985 |     3 | 10075 | P44 |  6233 |     6 | 10159 |
| P20 |  7123 |     5 | 10188 | P45 |  2488 |     4 | 10427 |
| P21 |   192 |     1 | 10739 | P46 |  5214 |     8 | 10199 |
| P22 |  2318 |     5 | 10272 | P47 |  4888 |     7 | 10047 |
| P23 |  3024 |     5 | 10134 | P48 |  1717 |     3 | 10024 |
| P24 |  5994 |     6 | 10142 | P49 |  8079 |     5 | 10076 |

- **Total: 181,077 nodes** across 50 positions
- **Average: 3,622 nodes/position** in 10 seconds (~362 NPS)
- Depth range: 1-15 (simple endgames reach d5-15, complex middlegames d1-3)

## 15s Search on eZ80 (50 positions)

Searched each of 50 benchmark positions for 15 seconds on the eZ80 (48 MHz, cycle-accurate emulator).
Pre-Texel HCE, pre eval-optimization.

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

## 30s Search on eZ80 (50 positions)

Searched each of 50 benchmark positions for 30 seconds on the eZ80 (48 MHz, cycle-accurate emulator).
Pre-Texel HCE, pre eval-optimization. Depth capped at 15 to prevent timer overflow aliasing on trivial endgames (P17 exhausts d15 in 8.7s).

| Pos | Nodes | Depth |    ms | Pos | Nodes | Depth |    ms |
| --- | ----: | ----: | ----: | --- | ----: | ----: | ----: |
| P0  |  5467 |     5 | 30744 | P25 |  8643 |     4 | 30713 |
| P1  |  7190 |     3 | 30910 | P26 |  6807 |     4 | 30077 |
| P2  | 11677 |     7 | 30353 | P27 |  3334 |     3 | 30334 |
| P3  |  4706 |     3 | 30600 | P28 |  3653 |     3 | 30475 |
| P4  |  6453 |     5 | 30278 | P29 |  4064 |     3 | 30145 |
| P5  |  4390 |     3 | 30312 | P30 |  9243 |     7 | 30028 |
| P6  | 15220 |     8 | 30083 | P31 | 12547 |     8 | 30076 |
| P7  | 16109 |     8 | 30196 | P32 | 10450 |     6 | 30403 |
| P8  | 11646 |     7 | 30096 | P33 |  8552 |     6 | 30184 |
| P9  | 26462 |     7 | 30008 | P34 | 15688 |     5 | 30027 |
| P10 | 23270 |     7 | 30115 | P35 |  9591 |     5 | 30453 |
| P11 |  6986 |     5 | 30306 | P36 |  6410 |     4 | 30694 |
| P12 |  9372 |     6 | 30283 | P37 |  3495 |     4 | 30255 |
| P13 |  8398 |     5 | 30029 | P38 |  7397 |     5 | 30557 |
| P14 |  9770 |     5 | 30043 | P39 |  4362 |     3 | 30209 |
| P15 | 12613 |     7 | 30095 | P40 |  5008 |     4 | 30234 |
| P16 | 19757 |     7 | 30147 | P41 |  6108 |     7 | 30491 |
| P17 |  7115 |    15 |  8745 | P42 |  5753 |     5 | 30362 |
| P18 | 12543 |     9 | 30120 | P43 | 11440 |     6 | 30645 |
| P19 |  8586 |     5 | 30214 | P44 | 21818 |     8 | 30205 |
| P20 | 15211 |     7 | 30069 | P45 |  6053 |     5 | 30022 |
| P21 |  4660 |     3 | 30740 | P46 | 13535 |     9 | 30213 |
| P22 | 22274 |     7 | 30257 | P47 | 20410 |     9 | 30006 |
| P23 | 21461 |     7 | 30186 | P48 |  5267 |     4 | 30708 |
| P24 |  8597 |     7 | 30302 | P49 | 11688 |     6 | 30064 |

- **Total: 521,249 nodes** across 50 positions
- **Average: 10,425 nodes/position** in 30 seconds (~347 NPS)
- Depth range: 3-15 (P17 exhausts d15 search tree; most positions reach d3-9)

## Tournament vs Stockfish

the methodology here is to limit nodes to avg. nodes it got in the 50 benchmark positions and then have the node-limited engine play against stockfish at diff. ratings. obv. this is flawed since nodes/pos differs by a good margin, but this should still paint a decent picture.

### Node-limited (1800 nodes, 0.1s/move, XXL book)

Simulates eZ80 playing strength (~1800 nodes per move based on 5s search bench). forgot to save the pgn for this.

| SF Elo | W   | D   | L   | Score   | Pct | Elo diff |
| ------ | --- | --- | --- | ------- | --- | -------- |
| 1700   | 13  | 3   | 14  | 14.5/30 | 48% | -12      |
| 1800   | 7   | 8   | 15  | 11.0/30 | 37% | -95      |
| 1900   | 4   | 9   | 17  | 8.5/30  | 28% | -161     |
| 2000   | 5   | 10  | 15  | 10.0/30 | 33% | -120     |
| 2100   | 4   | 7   | 19  | 7.5/30  | 25% | -191     |

**Estimated eZ80 Elo: ~1700** (50% mark vs SF-1700)

### Node-limited (4000 nodes, 0.1s/move, no book)

PGN: [`tournament_4000_nobook.pgn`](../engine/pgn/2026-02-15/tournament_4000_nobook.pgn)

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

PGN: [`tournament_6000_nobook.pgn`](../engine/pgn/2026-02-15/tournament_6000_nobook.pgn), [`tournament_6000_1950.pgn`](../engine/pgn/2026-02-15/tournament_6000_1950.pgn)

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

### Node-limited (12000 nodes, 0.1s/move, no book)

PGN: [`tournament_12000_nobook.pgn`](../engine/pgn/2026-02-15/tournament_12000_nobook.pgn), [`tournament_12000_2150.pgn`](../engine/pgn/2026-02-15/tournament_12000_2150.pgn), [`tournament_12000_1900.pgn`](../engine/pgn/2026-02-15/tournament_12000_1900.pgn)

Simulates ~30s search on eZ80 (~10,425 nodes per move based on 30s search bench).

| SF Elo | W   | D   | L   | Score   | Pct | Elo diff |
| ------ | --- | --- | --- | ------- | --- | -------- |
| 1900   | 24  | 10  | 26  | 29.0/60 | 48% | -12      |
| 2000   | 15  | 7   | 8   | 18.5/30 | 62% | +83      |
| 2100   | 13  | 7   | 10  | 16.5/30 | 55% | +35      |
| 2150   | 10  | 4   | 16  | 12.0/30 | 40% | -70      |
| 2200   | 8   | 8   | 14  | 12.0/30 | 40% | -70      |
| 2300   | 7   | 6   | 17  | 10.0/30 | 33% | -120     |

**Estimated eZ80 Elo: ~2100** (55% vs SF-2100, 40% vs SF-2200)

### "Unleashed" (0.1s/move, no node limit, XXL book) (2025-02-15)

PGN: [`tournament.pgn`](../engine/pgn/2026-02-15/tournament.pgn)

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

### with texel tuning (0.1s/move, no node limit, XXL book) (2026-02-16)

Desktop Arm64, post-Texel tuning, before int-type optimizations (`e50c681`). (M5 MacBook Pro)

| SF Elo | W   | D   | L   | Score   | Pct | Elo diff |
| ------ | --- | --- | --- | ------- | --- | -------- |
| 2600   | 24  | 14  | 12  | 31.0/50 | 62% | +85      |

**Estimated desktop Elo: ~2685** (62% vs SF-2600)

### with int-type optimizations (after texel tuning) (0.1s/move, no node limit, XXL book) (2026-02-16)

Desktop Arm64, post-Texel tuning + int-type optimizations (widened search locals/params to native `int`, narrowed `phase`/`phase_weight` to `uint8_t`). (M5 MacBook Pro)

| SF Elo | W   | D   | L   | Score    | Pct | Elo diff |
| ------ | --- | --- | --- | -------- | --- | -------- |
| 2600   | 48  | 20  | 21  | 58.0/89  | 65% | +109     |
| 2700   | 28  | 42  | 30  | 49.0/100 | 49% | -7       |

SF-2600: 89/100 games completed (11 lost to binary deletion mid-run).
PGN: [`tournament_post_opt.pgn`](../engine/pgn/2026-02-16/tournament_post_opt.pgn), [`tournament_post_opt_2700.pgn`](../engine/pgn/2026-02-16/tournament_post_opt_2700.pgn)

**Estimated desktop Elo: ~2700** (65% vs SF-2600, 49% vs SF-2700)

## Notes

- **Staged movegen** shows no change in movegen/perft benchmarks because it only reorders moves (captures first, then quiets). The benefit is in alpha-beta search where it improves move ordering and pruning.
- **Castle skip** increases movegen cost by +6,192 cy/call (+6.7%) because the additional `in_check` test at the start of movegen adds overhead that exceeds the savings from skipping castling attack probes. These 5 test positions are mostly not in check, so the optimization rarely triggers.
- **Bishop count** saves 8,905 cy/eval (-5.9%) by tracking bishop counts incrementally in make/unmake instead of counting them during eval. The make/unmake cost increases slightly (+88 cy/pair).
- **Piece index** is the biggest perft win: -5.1% total cycles at depth 3, primarily through faster make/unmake operations.
- At 48 MHz, perft(3) from startpos takes ~5.4s on the baseline and ~5.4s on the final version. The net cycle savings from all optimizations is -3.7% for perft(3).

## Eval Optimization Pass (2026-02-16)

Search profiling (50 positions x 1000 nodes) identified eval as the dominant bottleneck at **53% of search time** (~110K cy/call, ~157K cy/node total). Sub-profiling broke down eval into four sections:

### Eval Sub-Profile (before optimization)

| Section     |     cy/call | % of eval |
| ----------- | ----------: | --------: |
| pieces      |      49,127 |       42% |
| mobility    |      38,655 |       33% |
| build_pawns |      15,369 |       13% |
| shield      |       2,392 |        2% |
| other       |      10,642 |        9% |
| **total**   | **116,185** |  **100%** |

### Optimizations Applied

1. **Combined `build_pawn_info`** — merged two separate pawn file array passes (`build_pawn_files` for white/black) with pawn attack bitmap generation into a single function. One loop per color builds file bitmasks and writes attack squares simultaneously.

2. **Pawn attack bitmap** — static 128-byte array indexed by 0x88 square, with bit 0 = attacked by white pawn, bit 1 = attacked by black pawn. Mobility evaluation uses O(1) bitmap lookups (`pawn_atk[dest] & 2`) instead of per-square `pawn_attacks_sq()` function calls.

3. **Bitmask passed pawn detection** — replaced inner file-scanning loop with a bitmask approach. Each file stores a rank occupancy bitmask (bit per row); passed pawn test becomes `b_pawns[f] & ahead_mask` instead of iterating rows.

### Eval Sub-Profile (after optimization)

| Section     |    cy/call | % of eval |      Delta |
| ----------- | ---------: | --------: | ---------: |
| pieces      |     30,231 |       36% |       -38% |
| mobility    |     22,363 |       27% |       -42% |
| build_pawns |     17,694 |       21% |       +15% |
| shield      |      2,441 |        3% |          — |
| other       |     10,285 |       12% |          — |
| **total**   | **83,014** |  **100%** | **-28.5%** |

`build_pawns` increased slightly because it now also does the 128-byte `memset` + attack bitmap population, but this is more than offset by the gains in `pieces` and `mobility`.

### Overall Impact

| Metric  |  Before |   After |  Delta |
| ------- | ------: | ------: | -----: |
| cy/eval | 116,185 |  83,014 | -28.5% |
| cy/node | 161,271 | 135,855 | -15.7% |

### eZ80 Lesson: Static vs Stack Allocation

Initial implementation allocated `pawn_atk[128]` on the stack. This caused a **14% performance regression** across all eval sections because the 128 extra bytes pushed local variable offsets beyond the eZ80's efficient IX+displacement range (signed 8-bit: -128 to +127). Making the array `static` fixed this entirely. This is safe because `evaluate()` is never called recursively.

## 5s Search on eZ80 — pre-Texel HCE + code opts (50 positions, 2026-02-16)

Searched each of 50 benchmark positions for 5 seconds on the eZ80 (48 MHz, cycle-accurate emulator).
Hand-crafted eval weights before Texel tuning, WITH eval code optimizations (pawn attack bitmap, combined build_pawn_info, bitmask passed pawns).

- **Total: 85,965 nodes** across 50 positions
- **Average: 1,719 nodes/position** in 5 seconds (~344 NPS)
- vs pre-Texel HCE without code opts (82,484): **+4.2%**

## 10s Search on eZ80 — pre-Texel HCE + code opts (50 positions, 2026-02-16)

Searched each of 50 benchmark positions for 10 seconds on the eZ80 (48 MHz, cycle-accurate emulator).
Hand-crafted eval weights before Texel tuning, WITH eval code optimizations (pawn attack bitmap, combined build_pawn_info, bitmask passed pawns).

- **Total: 197,911 nodes** across 50 positions
- **Average: 3,958 nodes/position** in 10 seconds (~396 NPS)
- vs pre-Texel HCE without code opts (181,077): **+9.3%**

### Code Optimization Impact Summary (pre-Texel weights held constant)

| Time Limit | No Code Opts | With Code Opts | Delta |
| ---------- | -----------: | -------------: | ----: |
| 5s         |       82,484 |         85,965 | +4.2% |
| 10s        |      181,077 |        197,911 | +9.3% |

The larger improvement at 10s is likely due to: (a) faster eval allowing deeper searches within the time window, and (b) reduced time overshoot with faster eval (time checks occur every 1024 nodes).

## Node-Time Benchmark (50 positions, 2026-02-16)

Time (ms) to reach N nodes per position on the eZ80 (48 MHz, cycle-accurate emulator).
Post eval-optimization + Texel tuning. `max_depth = 15`, node-limited search, TT cleared between each run.

| Pos | 2000n ms | 4000n ms | 6000n ms | 8000n ms | 10000n ms |
| --- | -------: | -------: | -------: | -------: | --------: |
| P0  |     6055 |    11871 |    16163 |    20886 |     26829 |
| P1  |     7279 |    14415 |    21582 |    28847 |     35901 |
| P2  |     3229 |     6462 |     9397 |    12762 |     15903 |
| P3  |     6498 |    12724 |    18823 |    25082 |     31010 |
| P4  |     6037 |    11776 |    18663 |    25086 |     32010 |
| P5  |     7355 |    14165 |    21183 |    28197 |     35092 |
| P6  |     2666 |     5505 |     8614 |    11779 |     14023 |
| P7  |     2663 |     5434 |     8737 |    11902 |     15126 |
| P8  |     2852 |     5648 |     8785 |    12270 |     15391 |
| P9  |     1981 |     4114 |     6138 |     8188 |     10176 |
| P10 |     2095 |     4176 |     6341 |     8343 |     10485 |
| P11 |     4160 |     8249 |    11307 |    15002 |     19206 |
| P12 |     5064 |    11020 |    15557 |    20884 |     26458 |
| P13 |     2237 |     4401 |     6494 |     8773 |     11054 |
| P14 |     2513 |     5338 |     8983 |    12672 |     15687 |
| P15 |     2540 |     4766 |     6769 |     9922 |     12188 |
| P16 |     2534 |     4826 |     7286 |     9985 |     12348 |
| P17 |      n/a |      n/a |      n/a |      n/a |       n/a |
| P18 |     2725 |     6245 |     9861 |    13427 |     17106 |
| P19 |     4021 |     8159 |    12574 |    16979 |     20755 |
| P20 |     2438 |     5820 |     8656 |    11429 |     14344 |
| P21 |     6870 |    13896 |    20221 |    26663 |     33648 |
| P22 |     2638 |     5487 |     8268 |    10980 |     13800 |
| P23 |     2334 |     4760 |     7348 |     9675 |     12583 |
| P24 |     2675 |     5184 |     8205 |    10565 |     13190 |
| P25 |     6000 |    11791 |    17714 |    23654 |     29200 |
| P26 |     5807 |    11168 |    14896 |    19074 |     24634 |
| P27 |     6255 |    11260 |    16781 |    22453 |     28258 |
| P28 |     6670 |    12062 |    18933 |    23619 |     28686 |
| P29 |     6024 |    11794 |    17750 |    23700 |     29672 |
| P30 |     4277 |     8287 |    12598 |    17037 |     21300 |
| P31 |     3501 |     6930 |    10348 |    13727 |     17135 |
| P32 |     4095 |     7562 |    11500 |    15194 |     18918 |
| P33 |     4863 |     9601 |    14151 |    18925 |     23712 |
| P34 |     3314 |     6755 |    10027 |    12995 |     16271 |
| P35 |     3918 |     7761 |    11772 |    15610 |     19083 |
| P36 |     4882 |     9563 |    13813 |    17963 |     23328 |
| P37 |     4522 |     9203 |    13301 |    17098 |     22044 |
| P38 |     4161 |     8171 |    12171 |    16317 |     20421 |
| P39 |     6688 |    12838 |    19011 |    25419 |     31596 |
| P40 |     5369 |    10428 |    15607 |    21892 |     27716 |
| P41 |     4023 |     7673 |    11994 |    15565 |     18466 |
| P42 |     4262 |     8442 |    12503 |    16351 |     19936 |
| P43 |     4090 |    10750 |    17180 |    22600 |     27048 |
| P44 |     3135 |     5969 |     8662 |    11643 |     13925 |
| P45 |     4060 |     7685 |    11972 |    16085 |     19583 |
| P46 |     3128 |     6339 |     9561 |    12779 |     16005 |
| P47 |     2884 |     5822 |     8955 |    12080 |     15138 |
| P48 |     5129 |     9337 |    14832 |    19199 |     24097 |
| P49 |     2154 |     4489 |     6665 |    11921 |     16091 |

- Growth is roughly linear — per-node cost ranges from ~1.0 ms (simple endgames) to ~3.6 ms (complex middlegames)
- P17 ("Self stalemate") hangs with node-limit-only search (no time function) — suspected search edge case
- Average ms/node across 49 positions: ~2.7 ms/node

---

## Desktop Time-Limited Search (50 positions x 3 time limits)

**Platform**: macOS, Apple Silicon (M-series), gcc -O2, wall-clock time via `mach_absolute_time()`
**Search**: max_depth=15, time_fn=bench_time_ms, TT cleared per search

| Pos     | 10ms nodes  | 50ms nodes   | 100ms nodes  |
| ------- | ----------- | ------------ | ------------ |
| P0      | 29348       | 219721       | 501779       |
| P1      | 16647       | 175127       | 173810       |
| P2      | 78614       | 205433       | 838151       |
| P3      | 23819       | 165027       | 175992       |
| P4      | 13876       | 94856        | 344800       |
| P5      | 36086       | 147382       | 151346       |
| P6      | 87840       | 437633       | 419601       |
| P7      | 37610       | 237112       | 661546       |
| P8      | 88673       | 188036       | 1145712      |
| P9      | 180086      | 448325       | 1573819      |
| P10     | 152063      | 348082       | 1353402      |
| P11     | 20205       | 236559       | 245281       |
| P12     | 30634       | 113158       | 324103       |
| P13     | 70087       | 405067       | 337258       |
| P14     | 60442       | 140319       | 938163       |
| P15     | 94305       | 88081        | 895631       |
| P16     | 103730      | 703866       | 1574547      |
| P17     | 6055        | 3988         | 3984         |
| P18     | 54402       | 401804       | 419943       |
| P19     | 48509       | 307888       | 324613       |
| P20     | 50883       | 358857       | 431754       |
| P21     | 35197       | 33610        | 34958        |
| P22     | 39273       | 264552       | 937526       |
| P23     | 67296       | 366229       | 556650       |
| P24     | 39553       | 392139       | 575837       |
| P25     | 19467       | 133641       | 134950       |
| P26     | 12423       | 125374       | 128763       |
| P27     | 17989       | 68678        | 275845       |
| P28     | 37681       | 99262        | 245364       |
| P29     | 34281       | 86720        | 262813       |
| P30     | 72667       | 275784       | 426802       |
| P31     | 93970       | 302690       | 896301       |
| P32     | 66893       | 230072       | 580102       |
| P33     | 43959       | 312606       | 665539       |
| P34     | 69851       | 162415       | 195255       |
| P35     | 11923       | 146910       | 419604       |
| P36     | 41147       | 98066        | 482885       |
| P37     | 35643       | 275752       | 518246       |
| P38     | 62631       | 108627       | 108732       |
| P39     | 24563       | 167771       | 167834       |
| P40     | 43157       | 193177       | 166736       |
| P41     | 37557       | 151176       | 175038       |
| P42     | 22510       | 96123        | 355423       |
| P43     | 30048       | 197225       | 175423       |
| P44     | 61307       | 297136       | 307246       |
| P45     | 50563       | 301659       | 220412       |
| P46     | 86373       | 218999       | 241556       |
| P47     | 62397       | 273014       | 309885       |
| P48     | 13858       | 159279       | 239477       |
| P49     | 28319       | 326486       | 316410       |
| **Avg** | **50928**   | **225829**   | **459136**   |
| **Tot** | **2546410** | **11291493** | **22956847** |

- Average throughput: ~5.1M nodes/sec @ 10ms, ~4.5M nodes/sec @ 50ms, ~4.6M nodes/sec @ 100ms
- P17 ("Self stalemate") converges quickly (~4k nodes) regardless of time limit — fully solved early
- Some positions show non-monotonic 50ms→100ms growth (e.g. P1, P6, P13) — due to TT being cleared between each run and iterative deepening completing different iterations
