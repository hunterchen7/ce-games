# Texel Tuning Pipeline

This folder contains an end-to-end pipeline to tune `chess/engine/src/eval.c` using Texel tuning.

## What gets tuned

The tuner now learns a larger mg/eg parameter set:

- material scales per piece type (pawn/knight/bishop/rook/queen/king) for mg and eg
- PST scales per piece type (pawn/knight/bishop/rook/queen/king) for mg and eg
- bishop pair, tempo, doubled/isolated pawns, rook file bonuses, pawn shield (all mg/eg where applicable)
- connected pawn bonuses split by rank and phase
- passed pawn bonuses split by rank and phase
- knight/bishop mobility split by bucket and phase

This is a scale-based Texel tuning pass (`scale = 1.0` reproduces the baseline).

## 1) Fetch online data

Single month:

```bash
./chess/engine/tuning/fetch_data.sh
```

Range of months:

```bash
./chess/engine/tuning/fetch_broadcast_range.sh chess/engine/tuning/data 2024-01 2026-01
```

Data source:

- `https://database.lichess.org/broadcast/`

## 2) Build training dataset

Build from one file:

```bash
python3 chess/engine/tuning/build_texel_dataset.py \
  --input chess/engine/tuning/data/lichess_db_broadcast_2026-01.pgn.zst \
  --output chess/engine/tuning/data/texel_positions.csv \
  --positions-per-game 3 \
  --max-games 80000 \
  --min-ply 12 \
  --max-ply 100
```

Build from a directory (multi-file stream) with target size:

```bash
python3 chess/engine/tuning/build_texel_dataset.py \
  --input chess/engine/tuning/data \
  --output chess/engine/tuning/data/texel_positions_1m.csv \
  --positions-per-game 2 \
  --target-positions 1000000 \
  --min-ply 12 \
  --max-ply 100
```

Output columns:

- `fen`
- `label` (0.0/0.5/1.0 from side-to-move perspective)

## 3) Run Texel tuning

Build the exact C-side feature extractor first:

```bash
make -C chess/engine eval-terms
```

The tuner follows classic Texel flow:

1. Find best sigmoid slope `K` for current static eval.
2. Keep `K` fixed while tuning eval feature scales.

Pilot:

```bash
python3 chess/engine/tuning/texel_tune.py \
  --dataset chess/engine/tuning/data/texel_positions.csv \
  --feature-cache chess/engine/tuning/results/features.npz \
  --out chess/engine/tuning/results/texel_pilot.json \
  --feature-backend engine \
  --terms-bin chess/engine/build/eval_terms \
  --max-positions 60000 \
  --iters 60
```

Full run (1M example):

```bash
python3 chess/engine/tuning/texel_tune.py \
  --dataset chess/engine/tuning/data/texel_positions_1m.csv \
  --feature-cache chess/engine/tuning/results/features_1m_r4_expanded.npz \
  --out chess/engine/tuning/results/texel_1m_round4_expanded.json \
  --feature-backend engine \
  --terms-bin chess/engine/build/eval_terms \
  --max-positions 1000000 \
  --iters 400
```

Constrained run (recommended for stability):

```bash
python3 chess/engine/tuning/texel_tune.py \
  --dataset chess/engine/tuning/data/texel_positions_1m.csv \
  --feature-cache chess/engine/tuning/results/features_1m_r4_expanded.npz \
  --out chess/engine/tuning/results/texel_1m_round4b_constrained.json \
  --feature-backend engine \
  --terms-bin chess/engine/build/eval_terms \
  --max-positions 1000000 \
  --iters 150 \
  --l2 0.003 \
  --scale-min 0.75 \
  --scale-max 1.30
```

## 4) Apply tuned values to eval.c

```bash
python3 chess/engine/tuning/apply_texel_params.py \
  --params chess/engine/tuning/results/texel_full.json \
  --eval-c chess/engine/src/eval.c
```

`texel_tune.py` writes `base_values`, `base_tables`, and `table_decomp` to the
JSON so apply is absolute against the training baseline and does not stack if
re-run.

## 5) Verify build/tests

```bash
make -C chess/engine uci test-search test-integration eval-fen
./chess/engine/build/test_search
./chess/engine/build/test_integration
```

`eval-fen` utility:

```bash
echo "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1" | ./chess/engine/build/eval_fen
```

`eval-terms` utility:

```bash
echo "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1" | ./chess/engine/build/eval_terms
```
