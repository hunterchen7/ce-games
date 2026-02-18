# CE Games

A collection of games for the TI-84 Plus CE, built with the [CE C/C++ Toolchain](https://ce-programming.github.io/toolchain/).

## Games

| Game   | Description                                               | Demo                                                                                               |
| ------ | --------------------------------------------------------- | -------------------------------------------------------------------------------------------------- |
| Chess  | chess GUI & engine with opening book, 3 difficulty levels | ![chess](https://github.com/user-attachments/assets/5d1e533f-046a-443c-8e35-a56e92c74ca3)          |
| Pong   | Pong vs AI with 5 levels, and an "infinite" level         | ![pong](https://github.com/user-attachments/assets/0fdd6ba5-6432-4df9-9a56-f2642f49f33e)           |
| Sudoku | Easy, medium and hard mode with timer, pencil and more    | ![sudoku_full_3x](https://github.com/user-attachments/assets/5406b210-ce41-425a-bb80-80a494b6996d) |

## Prerequisites

- [CE C/C++ Toolchain v12+](https://github.com/CE-Programming/toolchain/releases) installed to `~/CEdev`
- `CEDEV` and toolchain bins on your PATH:
  ```sh
  export CEDEV="$HOME/CEdev"
  export PATH="$CEDEV/bin:$PATH"
  ```

## Building

Build all games:

```sh
make
```

Build a single game:

```sh
make pong
```

Clean all build artifacts:

```sh
make clean
```

Rebuild from scratch:

```sh
make clean && make
```

## Testing with the emulator

Using [hunterchen7/ti84ce](https://github.com/hunterchen7/ti84ce):

```sh
# Send a single program
cargo run --release --example debug -- sendfile ../games/pong/bin/PONG.8xp

# Bake all games + libs into a ROM
cargo run --release --example debug -- bakerom games.rom ../games/*/bin/*.8xp ../games/libs/*.8xv
```

## Project structure

```
ce-games/
  Makefile        # Top-level build (builds all games)
  shared/         # Common utilities (input, rendering helpers)
  libs/           # CE C library .8xv files
  pong/
    Makefile      # Game-specific build config
    icon.png      # 16x16 program icon
    src/main.c    # Game source
    bin/          # Build output (.8xp) — gitignored
    obj/          # Build intermediates — gitignored
```

Each game is a standalone CE C project with its own Makefile that includes the toolchain via `cedev-config --makefile`.

## Chess

A chess engine written in C for the TI-84 Plus CE's eZ80 processor (48 MHz, 256 KB RAM). Plays a full game of chess with an opening book, animated piece movement, and legal move highlighting.

### Estimated Elo Ratings

| Platform         | Time/Move | Estimated Elo | Method                    |
| ---------------- | --------- | ------------- | ------------------------- |
| eZ80 (48 MHz)    | 2s        | ~1355         | 50 games vs SF-1320       |
| eZ80 (48 MHz)    | 5s        | ~1700         | Node-limited vs Stockfish |
| eZ80 (48 MHz)    | 10s       | ~1800         | Node-limited vs Stockfish |
| eZ80 (48 MHz)    | 15s       | ~1950         | Node-limited vs Stockfish |
| eZ80 (48 MHz)    | 30s       | ~2100         | Node-limited vs Stockfish |
| Desktop (M5 MBP) | 0.01s     | ~2583         | 100 games vs Stockfish    |
| Desktop (M5 MBP) | 0.1s      | ~2700         | 189 games vs Stockfish    |

Ratings are estimated by playing against Stockfish's `UCI_LimitStrength` mode (single-threaded) at various UCI Elo levels. On-calculator ratings use node-limited search to simulate the eZ80's throughput (~330-400 NPS at 48 MHz). Desktop ratings are from direct time-controlled matches. Full results in [chess/bench/RESULTS.md](chess/bench/RESULTS.md).

### Engine Features

**Search:**

- Alpha-beta negamax with iterative deepening
- Principal variation search (PVS) with aspiration windows
- Null move pruning (R=2)
- Late move reductions (LMR)
- Futility pruning (depth <= 2)
- Quiescence search with delta pruning
- Check extensions (up to 2 per path)
- Transposition table (4096 entries, always-replace)

**Move Ordering:**

- TT best move, MVV-LVA for captures, killer heuristic (2 per ply), history heuristic
- Staged generation (captures first, then quiets)

**Evaluation:**

- Texel-tuned piece-square tables (PeSTO-based) with tapered middlegame/endgame scoring
- Pawn structure: doubled, isolated, connected, and passed pawn evaluation
- Piece mobility (knight, bishop) with pawn attack awareness
- Bishop pair bonus, rook on open/semi-open files, king pawn shield
- Incremental material + PST updates in make/unmake

**Opening Book:**

- Polyglot format split across multiple AppVars (TI-OS file size limit workaround)
- 5 tiers (Small to XXL), up to 131K entries
- Probed from Flash with zero RAM cost

**Difficulty & Move Variance:**

- Easy mode uses `move_variance` to randomly select among root moves within N centipawns of the best
- Wider PVS window at root ensures accurate candidate scoring without picking blunders
- variance=5 gives natural-looking move variety with no measurable Elo loss
- Higher variance values (10-15) trade more Elo for greater unpredictability

**Board Representation:**

- 0x88 board with piece lists and incremental Zobrist hashing
- 32-bit hash + 16-bit lock for TT collision detection

### UI

- 60 FPS double-buffered rendering (320x240, 8-bit palette)
- Animated piece movement with smooth interpolation
- Legal move indicators (dots for quiet moves, corner marks for captures)
- Last move highlighting, check indicator
- Board flips when playing Black
- 4 difficulty levels: Easy (2s), Medium (5s), Hard (10s), Expert (15s)
- Play as White, Black, or Random

### Controls

| Action           | Key              |
| ---------------- | ---------------- |
| Move cursor      | Arrow keys       |
| Select / confirm | `Enter` or `2nd` |
| Deselect         | `Clear`          |
| Resign           | `Mode`           |

### Building

The chess game requires the opening book AppVars alongside the main program:

```sh
make chess
```

This produces `chess/bin/CHESS.8xp` and the book AppVars in `chess/books/`. Transfer both the `.8xp` and the book `.8xv` files to your calculator.

## Controls

### Pong

**Menu / Level Select:**

| Action      | Key              |
| ----------- | ---------------- |
| Navigate    | `↑` / `↓`        |
| Select      | `Enter` or `2nd` |
| Quit / Back | `Clear`          |

**Gameplay:**

| Action           | Key              |
| ---------------- | ---------------- |
| Move paddle up   | `↑`              |
| Move paddle down | `↓`              |
| Return to menu   | `Clear`          |
| Skip transition  | `Enter` or `2nd` |

You control the right paddle. The left paddle is AI-controlled. You have 3 lives — lose one each time the AI scores. Lose all lives and it's game over.

**Game Modes:**

- **Play** — Campaign through 5 levels of increasing difficulty. Score enough points to advance.
- **Infinite** — Endless mode that gets harder every point you score. Color theme cycles every 5 points. How far can you get?
- **Level Select** — Jump to any level directly.
