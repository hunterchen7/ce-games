# CE Games

A collection of games for the TI-84 Plus CE, built with the [CE C/C++ Toolchain](https://ce-programming.github.io/toolchain/).

## Games

| Game | Description              | Demo      |
| ---- | ------------------------ | ----------- |
| Pong | Pong vs AI with 5 levels, and an "infinite" level | ![pong](https://github.com/user-attachments/assets/0fdd6ba5-6432-4df9-9a56-f2642f49f33e) |

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
