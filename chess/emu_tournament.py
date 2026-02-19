#!/usr/bin/env python3
"""
Emulator-based chess tournament: eZ80 engine (via emulator) vs Stockfish.

The eZ80 engine runs inside a cycle-accurate TI-84 Plus CE emulator.
Each move is computed by patching a command into the binary, running the
emulator as a subprocess, and parsing the "MOVE xxxx" output.

Usage:
    python3 emu_tournament.py --games 30 --sf-elo 1500 --time-ms 4500 \\
        --max-nodes 30000 --variance 5 --book-ply 0 --concurrency 5
"""

import argparse
import chess
import chess.engine
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time
from concurrent.futures import ThreadPoolExecutor, as_completed
from pathlib import Path
from datetime import datetime

# Paths
SCRIPT_DIR = Path(__file__).parent.resolve()
EMU_UCI_8XP = SCRIPT_DIR / "emu_uci" / "bin" / "EMUUCI.8xp"
BOOK_DIR = SCRIPT_DIR / "books"
LIBS_DIR = SCRIPT_DIR.parent / "libs"
EMU_DIR = Path.home() / "Documents" / "GitHub" / "calc" / "core"
EMU_BIN = EMU_DIR / "target" / "release" / "examples" / "debug"

SENTINEL = b"@@CMDSLOT@@"
CMD_SLOT_SIZE = 512


def find_sentinel(data: bytes) -> int:
    """Find the offset of the @@CMDSLOT@@ sentinel in the binary."""
    idx = data.find(SENTINEL)
    if idx < 0:
        raise ValueError("Sentinel @@CMDSLOT@@ not found in binary!")
    # Verify uniqueness
    if data.find(SENTINEL, idx + 1) >= 0:
        raise ValueError("Multiple sentinels found in binary!")
    return idx


def recompute_8xp_checksum(data: bytes) -> bytes:
    """Recompute the .8xp file checksum (last 2 bytes = sum of data section)."""
    # Data section starts at offset 55, checksum is last 2 bytes
    data_section = data[55:-2]
    checksum = sum(data_section) & 0xFFFF
    return data[:-2] + checksum.to_bytes(2, "little")


def patch_binary(original: bytes, offset: int, command: str) -> bytes:
    """Patch the command into the binary at the sentinel offset."""
    cmd_bytes = command.encode("ascii")
    if len(cmd_bytes) >= CMD_SLOT_SIZE:
        raise ValueError(f"Command too long ({len(cmd_bytes)} >= {CMD_SLOT_SIZE})")
    # NUL-pad to fill the slot
    padded = cmd_bytes + b"\x00" * (CMD_SLOT_SIZE - len(cmd_bytes))
    patched = original[:offset] + padded + original[offset + CMD_SLOT_SIZE:]
    return recompute_8xp_checksum(patched)


def build_command(time_ms: int, max_nodes: int, variance: int, book_ply: int,
                  fen: str) -> str:
    """Build the command string for the eZ80 program."""
    return f"{time_ms} {max_nodes} {variance} {book_ply} {fen}"


def get_appvar_args(book_ply: int) -> list[str]:
    """Get the list of appvar files to pass to the emulator."""
    args = []
    # Always need libs
    for f in sorted(LIBS_DIR.glob("*.8xv")):
        args.append(str(f))
    # CHDATA (book randoms + sprites) — always needed
    chdata = SCRIPT_DIR / "assets" / "CHDATA.8xv"
    if chdata.exists():
        args.append(str(chdata))
    # Book files only if book is used
    if book_ply > 0:
        # Use Small tier (CHBS) for tournament
        for f in sorted(BOOK_DIR.glob("CHBS*.8xv")):
            args.append(str(f))
    return args


def run_emulator(patched_8xp: str, appvar_args: list[str],
                 timeout_s: int) -> str | None:
    """Run the emulator and return the engine's move (UCI string) or None."""
    cmd = [
        str(EMU_BIN), "run", patched_8xp
    ] + appvar_args + ["--timeout", str(timeout_s)]

    try:
        result = subprocess.run(
            cmd, capture_output=True, text=True,
            cwd=str(EMU_DIR),
            timeout=timeout_s + 30  # extra margin
        )
        output = result.stdout + result.stderr
        # Look for "MOVE xxxx"
        match = re.search(r"MOVE\s+(\S+)", output)
        if match:
            return match.group(1)
        else:
            print(f"  [EMU] No MOVE found in output. stdout={result.stdout[:200]}")
            print(f"  [EMU] stderr={result.stderr[:200]}")
            return None
    except subprocess.TimeoutExpired:
        print(f"  [EMU] Timeout after {timeout_s + 30}s")
        return None
    except Exception as e:
        print(f"  [EMU] Error: {e}")
        return None


class EmuEngine:
    """Wraps the eZ80 emulator-based engine for playing games."""

    def __init__(self, original_binary: bytes, sentinel_offset: int,
                 time_ms: int, max_nodes: int, variance: int, book_ply: int,
                 appvar_args: list[str], timeout_s: int, tmp_dir: str):
        self.original = original_binary
        self.offset = sentinel_offset
        self.time_ms = time_ms
        self.max_nodes = max_nodes
        self.variance = variance
        self.book_ply = book_ply
        self.appvar_args = appvar_args
        self.timeout_s = timeout_s
        self.tmp_dir = tmp_dir

    def get_move(self, fen: str) -> str | None:
        """Get the engine's move for the given position (FEN string)."""
        cmd = build_command(self.time_ms, self.max_nodes, self.variance,
                            self.book_ply, fen)
        patched = patch_binary(self.original, self.offset, cmd)

        # Write patched binary to temp file (filename must be EMUUCI.8xp
        # because the emulator uses the filename stem as the program name)
        tmp_path = os.path.join(self.tmp_dir, "EMUUCI.8xp")
        with open(tmp_path, "wb") as f:
            f.write(patched)

        move = run_emulator(tmp_path, self.appvar_args, self.timeout_s)
        return move


def play_game(game_id: int, emu_engine: EmuEngine, sf_path: str,
              sf_elo: int, emu_plays_white: bool) -> dict:
    """Play a single game between the emulator engine and Stockfish."""
    board = chess.Board()
    moves_uci = []
    game_moves = []

    # Start Stockfish
    sf = chess.engine.SimpleEngine.popen_uci(sf_path)
    sf.configure({"UCI_LimitStrength": True, "UCI_Elo": sf_elo, "Threads": 1})

    result_info = {
        "game_id": game_id,
        "emu_white": emu_plays_white,
        "result": "*",
        "moves": [],
        "termination": "",
        "move_count": 0,
    }

    max_moves = 300  # safety limit

    try:
        while not board.is_game_over() and len(moves_uci) < max_moves:
            emu_turn = (board.turn == chess.WHITE) == emu_plays_white

            if emu_turn:
                move_str = emu_engine.get_move(board.fen())
                if move_str is None or move_str == "none":
                    # Engine failed to produce a move — forfeit
                    result_info["result"] = "0-1" if emu_plays_white else "1-0"
                    result_info["termination"] = "emu_error"
                    break
                try:
                    move = chess.Move.from_uci(move_str)
                except ValueError:
                    print(f"  Game {game_id}: Invalid UCI move from emu: '{move_str}'")
                    result_info["result"] = "0-1" if emu_plays_white else "1-0"
                    result_info["termination"] = "emu_invalid_move"
                    break
                if move not in board.legal_moves:
                    print(f"  Game {game_id}: Illegal move from emu: {move_str} in {board.fen()}")
                    result_info["result"] = "0-1" if emu_plays_white else "1-0"
                    result_info["termination"] = "emu_illegal_move"
                    break
            else:
                sf_result = sf.play(board, chess.engine.Limit(time=1.0))
                move = sf_result.move

            board.push(move)
            moves_uci.append(move.uci())
            game_moves.append(move.uci())

        if board.is_game_over():
            outcome = board.outcome()
            if outcome.winner is None:
                result_info["result"] = "1/2-1/2"
            elif outcome.winner == chess.WHITE:
                result_info["result"] = "1-0"
            else:
                result_info["result"] = "0-1"
            result_info["termination"] = outcome.termination.name
        elif len(moves_uci) >= max_moves:
            result_info["result"] = "1/2-1/2"
            result_info["termination"] = "max_moves"

    finally:
        sf.quit()

    result_info["moves"] = game_moves
    result_info["move_count"] = len(game_moves)
    return result_info


def result_to_emu_score(result: str, emu_white: bool) -> float:
    """Convert game result to score from emu engine's perspective."""
    if result == "1/2-1/2":
        return 0.5
    if result == "1-0":
        return 1.0 if emu_white else 0.0
    if result == "0-1":
        return 0.0 if emu_white else 1.0
    return 0.0


def run_tournament(args):
    """Run a tournament of N games."""
    # Read original binary
    if not EMU_UCI_8XP.exists():
        print(f"Error: {EMU_UCI_8XP} not found. Build with: make -C chess/emu_uci")
        sys.exit(1)

    original = EMU_UCI_8XP.read_bytes()
    offset = find_sentinel(original)
    print(f"Sentinel found at offset {offset}")

    appvar_args = get_appvar_args(args.book_ply)
    # Timeout must cover both time-based and node-based search stopping.
    # If the emulator's hardware timer glitches, the search falls back to
    # the node limit.  At ~154K cycles/node and 48 MHz, worst case is:
    #   max_nodes * 200 / 48000  seconds  (with safety margin on cy/node)
    time_timeout = (args.time_ms // 1000) * 3 + 30
    node_timeout = (args.max_nodes * 200 // 48000 + 30) if args.max_nodes else 0
    timeout_s = max(60, time_timeout, node_timeout)

    sf_path = shutil.which("stockfish")
    if not sf_path:
        print("Error: stockfish not found in PATH")
        sys.exit(1)

    print(f"\n=== Tournament: {args.games} games, SF Elo {args.sf_elo} ===")
    print(f"    time={args.time_ms}ms nodes={args.max_nodes} var={args.variance} "
          f"book_ply={args.book_ply}")
    print(f"    timeout={timeout_s}s concurrency={args.concurrency}")
    print(f"    appvars: {len(appvar_args)} files")
    print()

    results = []
    wins = draws = losses = 0
    start_time = time.time()

    # PGN output
    date_str = datetime.now().strftime("%Y-%m-%d")
    pgn_dir = SCRIPT_DIR / "engine" / "pgn" / date_str
    pgn_dir.mkdir(parents=True, exist_ok=True)
    pgn_name = f"emu_{args.time_ms}ms_v{args.variance}_sf{args.sf_elo}.pgn"
    pgn_path = pgn_dir / pgn_name

    def run_single_game(game_id):
        with tempfile.TemporaryDirectory() as tmp_dir:
            engine = EmuEngine(
                original, offset, args.time_ms, args.max_nodes,
                args.variance, args.book_ply, appvar_args, timeout_s, tmp_dir
            )
            emu_white = (game_id % 2 == 0)  # alternate colors
            return play_game(game_id, engine, sf_path, args.sf_elo, emu_white)

    with ThreadPoolExecutor(max_workers=args.concurrency) as pool:
        futures = {pool.submit(run_single_game, i): i for i in range(args.games)}

        for future in as_completed(futures):
            info = future.result()
            results.append(info)

            emu_score = result_to_emu_score(info["result"], info["emu_white"])
            if emu_score == 1.0:
                wins += 1
            elif emu_score == 0.5:
                draws += 1
            else:
                losses += 1

            color = "W" if info["emu_white"] else "B"
            n = len(results)
            print(f"  Game {info['game_id']:2d} ({color}): {info['result']:7s} "
                  f"({info['termination']}, {info['move_count']} moves)  "
                  f"[+{wins}={draws}-{losses}] ({n}/{args.games})")

            # Write PGN
            with open(pgn_path, "a") as f:
                emu_name = f"CE-eZ80-{args.time_ms}ms"
                sf_name = f"Stockfish-{args.sf_elo}"
                white = emu_name if info["emu_white"] else sf_name
                black = sf_name if info["emu_white"] else emu_name
                f.write(f'[Event "Emu Tournament"]\n')
                f.write(f'[Site "Emulator"]\n')
                f.write(f'[Date "{date_str}"]\n')
                f.write(f'[Round "{info["game_id"] + 1}"]\n')
                f.write(f'[White "{white}"]\n')
                f.write(f'[Black "{black}"]\n')
                f.write(f'[Result "{info["result"]}"]\n')
                f.write(f'[Termination "{info["termination"]}"]\n\n')
                # Write moves
                board = chess.Board()
                move_text = []
                for i, m in enumerate(info["moves"]):
                    if i % 2 == 0:
                        move_text.append(f"{i // 2 + 1}.")
                    move_text.append(board.san(chess.Move.from_uci(m)))
                    board.push(chess.Move.from_uci(m))
                f.write(" ".join(move_text))
                f.write(f" {info['result']}\n\n")

            # Report every 5 games
            if n % 5 == 0 or n == args.games:
                elapsed = time.time() - start_time
                total = wins + draws + losses
                score_pct = (wins + 0.5 * draws) / total * 100 if total else 0
                print(f"\n  --- After {n} games: +{wins}={draws}-{losses} "
                      f"({score_pct:.0f}%) elapsed={elapsed:.0f}s ---\n")

    # Final summary
    elapsed = time.time() - start_time
    total = wins + draws + losses
    score_pct = (wins + 0.5 * draws) / total * 100 if total else 0
    print(f"\n{'='*60}")
    print(f"FINAL: +{wins}={draws}-{losses} ({score_pct:.1f}%)")
    print(f"SF Elo: {args.sf_elo}  Time: {args.time_ms}ms  Variance: {args.variance}")
    print(f"Elapsed: {elapsed:.0f}s  PGN: {pgn_path}")
    print(f"{'='*60}")

    return {"wins": wins, "draws": draws, "losses": losses, "pgn": str(pgn_path)}


def main():
    parser = argparse.ArgumentParser(description="Emulator-based chess tournament")
    parser.add_argument("--games", type=int, default=30, help="Number of games")
    parser.add_argument("--sf-elo", type=int, default=1500, help="Stockfish UCI_Elo")
    parser.add_argument("--time-ms", type=int, default=4500, help="Engine think time (ms)")
    parser.add_argument("--max-nodes", type=int, default=30000, help="Engine max nodes")
    parser.add_argument("--variance", type=int, default=0, help="Move variance (cp)")
    parser.add_argument("--book-ply", type=int, default=0, help="Book max ply (0=disabled)")
    parser.add_argument("--concurrency", type=int, default=5, help="Concurrent games")
    args = parser.parse_args()

    run_tournament(args)


if __name__ == "__main__":
    main()
