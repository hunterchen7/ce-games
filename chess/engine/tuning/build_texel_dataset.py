#!/usr/bin/env python3
"""Build a Texel tuning dataset from one or more PGN sources.

Output CSV columns:
  fen,label

label is the final game score from side-to-move perspective:
  win=1.0, draw=0.5, loss=0.0
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import io
import random
import subprocess
from pathlib import Path

import chess
import chess.pgn

RESULT_TO_WHITE_SCORE = {
    "1-0": 1.0,
    "0-1": 0.0,
    "1/2-1/2": 0.5,
}


def open_pgn_stream(path: Path):
    if path.suffix == ".zst":
        proc = subprocess.Popen(
            ["zstd", "-dc", str(path)],
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
        )
        if proc.stdout is None:
            raise RuntimeError("failed to open zstd stream")
        return io.TextIOWrapper(proc.stdout, encoding="utf-8", errors="replace"), proc

    return path.open("r", encoding="utf-8", errors="replace"), None


def should_keep_position(board: chess.Board, ply: int, min_ply: int, max_ply: int, was_capture: bool) -> bool:
    if ply < min_ply or ply > max_ply:
        return False
    if was_capture:
        return False
    if board.is_check():
        return False
    if board.is_game_over(claim_draw=True):
        return False
    return True


def fen_hash64(fen: str) -> int:
    return int.from_bytes(hashlib.blake2b(fen.encode("utf-8"), digest_size=8).digest(), "little")


def resolve_input_files(inputs: list[str]) -> list[Path]:
    files: list[Path] = []
    for item in inputs:
        p = Path(item)
        if p.is_dir():
            files.extend(sorted(p.glob("*.pgn")))
            files.extend(sorted(p.glob("*.pgn.zst")))
        elif p.is_file():
            files.append(p)
        else:
            raise FileNotFoundError(f"input path not found: {item}")

    if not files:
        raise FileNotFoundError("no PGN files resolved from --input")

    # Stable ordering (important for reproducibility).
    files = sorted(set(files), key=lambda x: str(x))
    return files


def main() -> None:
    parser = argparse.ArgumentParser(description="Build Texel tuning dataset from PGN")
    parser.add_argument(
        "--input",
        action="append",
        required=True,
        help="Input .pgn/.pgn.zst file or directory. Repeat for multiple sources.",
    )
    parser.add_argument("--output", required=True, help="Output CSV path")
    parser.add_argument(
        "--max-games",
        type=int,
        default=0,
        help="Global parsed game limit across all inputs (0 = no limit)",
    )
    parser.add_argument(
        "--max-games-per-input",
        type=int,
        default=0,
        help="Parsed game limit per input file (0 = no limit)",
    )
    parser.add_argument(
        "--target-positions",
        type=int,
        default=0,
        help="Stop once this many positions are written (0 = no target)",
    )
    parser.add_argument("--positions-per-game", type=int, default=2)
    parser.add_argument("--min-ply", type=int, default=12)
    parser.add_argument("--max-ply", type=int, default=100)
    parser.add_argument("--seed", type=int, default=42)
    parser.add_argument(
        "--dedupe",
        action=argparse.BooleanOptionalAction,
        default=True,
        help="Deduplicate FENs globally (default: enabled)",
    )
    args = parser.parse_args()

    out = Path(args.output)
    out.parent.mkdir(parents=True, exist_ok=True)

    src_files = resolve_input_files(args.input)

    rng = random.Random(args.seed)

    parsed_games = 0
    kept_games = 0
    kept_positions = 0
    seen_fens: set[int] | None = set() if args.dedupe else None

    with out.open("w", newline="", encoding="utf-8") as f_out:
        writer = csv.writer(f_out)
        writer.writerow(["fen", "label"])

        for src in src_files:
            print(f"source_start file={src}", flush=True)

            src_parsed = 0
            src_kept_games = 0
            src_kept_positions = 0

            text_stream, proc = open_pgn_stream(src)
            try:
                while True:
                    if args.max_games > 0 and parsed_games >= args.max_games:
                        break
                    if args.max_games_per_input > 0 and src_parsed >= args.max_games_per_input:
                        break
                    if args.target_positions > 0 and kept_positions >= args.target_positions:
                        break

                    game = chess.pgn.read_game(text_stream)
                    if game is None:
                        break

                    parsed_games += 1
                    src_parsed += 1

                    result = game.headers.get("Result", "*")
                    white_score = RESULT_TO_WHITE_SCORE.get(result)
                    if white_score is None:
                        continue

                    board = game.board()
                    candidates: list[tuple[str, float]] = []

                    ply = 0
                    for move in game.mainline_moves():
                        ply += 1
                        was_capture = board.is_capture(move)
                        board.push(move)

                        if not should_keep_position(board, ply, args.min_ply, args.max_ply, was_capture):
                            continue

                        side_score = white_score if board.turn == chess.WHITE else (1.0 - white_score)
                        fen = board.fen(en_passant="fen")
                        candidates.append((fen, side_score))

                    if not candidates:
                        continue

                    sample_n = min(args.positions_per_game, len(candidates))
                    wrote_this_game = 0

                    for fen, label in rng.sample(candidates, sample_n):
                        if seen_fens is not None:
                            h = fen_hash64(fen)
                            if h in seen_fens:
                                continue
                            seen_fens.add(h)

                        writer.writerow([fen, f"{label:.1f}"])
                        kept_positions += 1
                        src_kept_positions += 1
                        wrote_this_game += 1

                        if args.target_positions > 0 and kept_positions >= args.target_positions:
                            break

                    if wrote_this_game > 0:
                        kept_games += 1
                        src_kept_games += 1

                    if parsed_games % 2000 == 0:
                        print(
                            "progress "
                            f"parsed_games={parsed_games} kept_games={kept_games} kept_positions={kept_positions}",
                            flush=True,
                        )

                print(
                    "source_done "
                    f"file={src} parsed={src_parsed} kept_games={src_kept_games} kept_positions={src_kept_positions}",
                    flush=True,
                )

            finally:
                if proc is not None:
                    proc.stdout.close()  # type: ignore[union-attr]
                    proc.wait(timeout=30)
                else:
                    text_stream.close()

            if args.max_games > 0 and parsed_games >= args.max_games:
                break
            if args.target_positions > 0 and kept_positions >= args.target_positions:
                break

    print(
        "Done. "
        f"sources={len(src_files)} parsed_games={parsed_games} kept_games={kept_games} "
        f"kept_positions={kept_positions} output={out}",
        flush=True,
    )


if __name__ == "__main__":
    main()
