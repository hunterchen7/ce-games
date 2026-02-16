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

## Notes

- **Staged movegen** shows no change in movegen/perft benchmarks because it only reorders moves (captures first, then quiets). The benefit is in alpha-beta search where it improves move ordering and pruning.
- **Castle skip** increases movegen cost by +6,192 cy/call (+6.7%) because the additional `in_check` test at the start of movegen adds overhead that exceeds the savings from skipping castling attack probes. These 5 test positions are mostly not in check, so the optimization rarely triggers.
- **Bishop count** saves 8,905 cy/eval (-5.9%) by tracking bishop counts incrementally in make/unmake instead of counting them during eval. The make/unmake cost increases slightly (+88 cy/pair).
- **Piece index** is the biggest perft win: -5.1% total cycles at depth 3, primarily through faster make/unmake operations.
- At 48 MHz, perft(3) from startpos takes ~5.4s on the baseline and ~5.4s on the final version. The net cycle savings from all optimizations is -3.7% for perft(3).
