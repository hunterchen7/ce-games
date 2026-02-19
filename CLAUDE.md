# CE Games

A collection of games for the **TI-84 Plus CE** graphing calculator.

## Toolchain

- **CE C/C++ Toolchain v12+** — cross-compiler and libraries for TI-84 Plus CE
  - Install location: `~/CEdev`
  - Env vars required: `CEDEV="$HOME/CEdev"`, `PATH="$CEDEV/bin:$PATH"`
  - Each game Makefile includes the toolchain via `include $(shell cedev-config --makefile)`
  - Docs: https://ce-programming.github.io/toolchain/

- **Target hardware**: TI-84 Plus CE (eZ80 CPU, 320x240 screen, 8-bit palette-indexed graphics)
- **Output format**: `.8xp` files (calculator program binaries)

## Build

```sh
make          # build all games
make pong     # build a single game
make clean    # clean all build artifacts
```

Each game has its own `Makefile` with `NAME`, `ICON`, `DESCRIPTION`, `CFLAGS` etc. The top-level `Makefile` delegates to each game directory.

## Testing / Emulation

Using the custom emulator at https://github.com/hunterchen7/ti84ce:

```sh
# Send a single program
cargo run --release --example debug -- sendfile ../games/pong/bin/PONG.8xp

# Bake all games + libs into a ROM
cargo run --release --example debug -- bakerom games.rom ../games/*/bin/*.8xp ../games/libs/*.8xv
```

## Project Structure

```
ce-games/
  CLAUDE.md
  Makefile          # Top-level build (iterates GAMES list)
  README.md
  shared/           # Common utilities (currently empty, for future use)
  libs/             # CE C library .8xv files (graphx, keypadc, fontlibc, fileioc, libload)
  <game>/
    Makefile         # Game-specific build config (NAME, ICON, CFLAGS, includes toolchain)
    icon.png         # 16x16 program icon
    src/main.c       # Game source (single-file C)
    bin/             # Build output (.8xp)
    obj/             # Build intermediates
```

## Adding a New Game

1. Create `<game>/` directory with `Makefile`, `icon.png`, and `src/main.c`
2. Game Makefile template:
   ```makefile
   NAME = GAMENAME
   ICON = icon.png
   DESCRIPTION = "Description"
   COMPRESSED = NO
   CFLAGS = -Wall -Wextra -Oz
   CXXFLAGS = -Wall -Wextra -Oz
   include $(shell cedev-config --makefile)
   ```
3. Add the game directory name to the `GAMES` list in the top-level `Makefile`

## Chess Engine

The chess game (`chess/`) has a standalone engine library (`chess/engine/`) shared across
multiple build targets, plus tooling for automated Elo testing.

### Engine Source (`chess/engine/src/`)

Core engine files: `board.c`, `movegen.c`, `eval.c`, `search.c`, `tt.c`, `zobrist.c`,
`engine.c`, `book.c`. Shared by all chess targets (game, bench, emu_uci).

### Build Targets

```sh
make -C chess              # Main game (CHESS.8xp)
make -C chess/bench        # Benchmark harness (BENCH.8xp)
make -C chess/emu_uci      # Emulator UCI bridge (EMUUCI.8xp)
```

### Emu UCI (`chess/emu_uci/`)

Headless eZ80 program for emulator-based automated play. Takes a single position
via a binary-patched command slot (`@@CMDSLOT@@` sentinel, 512 bytes).

**Command format**: `"<time_ms> <max_nodes> <variance> <book_ply> <fen>"`

The controller patches this into the `.8xp` binary before each emulator run.
The program parses the FEN, runs `engine_think()`, and outputs `MOVE <uci>\n`
via `dbg_printf`, then terminates via `*(volatile uint8_t *)0xFB0000 = 0`.

**Important**: The emulator derives the program name from the `.8xp` filename,
so patched temp files must be named `EMUUCI.8xp` (not a random temp name).

### Tournament Script (`chess/emu_tournament.py`)

Plays the eZ80 engine against Stockfish at configurable Elo via the emulator.
Requires `python-chess` and `stockfish` in PATH.

```sh
# Run 30 games vs SF-1700 with 9s think time
python3 chess/emu_tournament.py --games 30 --sf-elo 1700 --time-ms 9000

# All options:
#   --games N          Number of games (default 30)
#   --sf-elo N         Stockfish UCI_Elo (default 1500)
#   --time-ms N        Engine think time in ms (default 4500)
#   --max-nodes N      Engine node limit (default 30000)
#   --variance N       Move variance in centipawns (default 0)
#   --book-ply N       Opening book max ply, 0=disabled (default 0)
#   --concurrency N    Concurrent games (default 5)
```

Output PGN goes to `chess/engine/pgn/<date>/emu_<time>ms_v<var>_sf<elo>.pgn`.
Each game is a separate emulator process (fresh state). Games with emulator
timeouts are recorded as losses with `[Termination "emu_error"]`.

### Benchmark Results (`chess/bench/RESULTS.md`)

Contains cycle-accurate profiling data, optimization history (commits with
cy/node deltas), node-count benchmarks, and **emulator tournament Elo estimates**
at different time controls (Easy 900ms through Master 27s).

## CE C Programming Notes

- **Screen**: 320x240, 8-bit palette-indexed color (1555 RGB format via `gfx_RGBTo1555`)
- **Graphics library**: `graphx.h` — double-buffered rendering with `gfx_SetDrawBuffer()` / `gfx_SwapDraw()`
- **Input**: `keypadc.h` — call `kb_Scan()` once per frame, read `kb_Data[group]` for key states
  - Group 1: `kb_2nd`, `kb_Mode`, etc.
  - Group 6: `kb_Enter`, `kb_Clear`, etc.
  - Group 7: `kb_Up`, `kb_Down`, `kb_Left`, `kb_Right`
  - Edge detection pattern: `new_keys = cur_keys & ~prev_keys`
- **Timing**: `clock()` from `time.h`, `CLOCKS_PER_SEC` for frame timing
- **Compiler flags**: `-Wall -Wextra -Oz` (optimize for size — calculator has limited memory)
- **Program names**: Must be uppercase, max 8 characters (TI-OS limitation)
- **All games are single-file C** — keep source in `src/main.c`
