#!/usr/bin/env python3
"""
Ablation tournament: each variant (with one eval feature removed) plays 20 games
against the full engine (all features enabled). Both sides use 2000-node limit.
"""

import os
import sys
import datetime
import threading
from concurrent.futures import ThreadPoolExecutor, as_completed
import chess
import chess.engine
import chess.pgn

BASE_DIR = os.path.dirname(os.path.abspath(__file__))
BUILD_DIR = os.path.join(BASE_DIR, "build")
FULL_ENGINE = os.path.join(BUILD_DIR, "uci_full")

VARIANTS = [
    ("no_tempo",      "NO_TEMPO",      "Tempo bonus S(24,11)"),
    ("no_pawns",      "NO_PAWNS",      "Pawn structure (doubled/isolated/connected)"),
    ("no_passed",     "NO_PASSED",     "Passed pawn bonus"),
    ("no_rook_files", "NO_ROOK_FILES", "Rook on open/semi-open files"),
    ("no_mobility",   "NO_MOBILITY",   "Knight & bishop mobility"),
    ("no_shield",     "NO_SHIELD",     "Pawn shield (king safety)"),
]

GAMES_PER_MATCH = 20  # 10 as white, 10 as black
MOVETIME = 0.1  # seconds per move
MAX_WORKERS = 6

print_lock = threading.Lock()
pgn_lock = threading.Lock()


def log(msg):
    with print_lock:
        print(msg, flush=True)


def play_game(white_path, black_path, white_name, black_name, event):
    """Play one game. Returns chess.pgn.Game."""
    board = chess.Board()
    game = chess.pgn.Game()
    game.headers["Event"] = event
    game.headers["Date"] = datetime.date.today().strftime("%Y.%m.%d")
    game.headers["White"] = white_name
    game.headers["Black"] = black_name

    w_eng = chess.engine.SimpleEngine.popen_uci(white_path)
    b_eng = chess.engine.SimpleEngine.popen_uci(black_path)

    limit = chess.engine.Limit(time=MOVETIME)
    node = game

    try:
        while not board.is_game_over(claim_draw=True) and board.fullmove_number <= 200:
            eng = w_eng if board.turn == chess.WHITE else b_eng
            try:
                result = eng.play(board, limit)
            except chess.engine.EngineTerminatedError:
                game.headers["Result"] = "0-1" if board.turn == chess.WHITE else "1-0"
                return game
            if result.move is None:
                break
            node = node.add_variation(result.move)
            board.push(result.move)

        outcome = board.outcome(claim_draw=True)
        if outcome:
            game.headers["Result"] = outcome.result()
        else:
            game.headers["Result"] = "1/2-1/2"
    finally:
        try:
            w_eng.quit()
        except Exception:
            pass
        try:
            b_eng.quit()
        except Exception:
            pass

    return game


def result_score(result_str, for_white):
    if result_str == "1-0":
        return 1.0 if for_white else 0.0
    elif result_str == "0-1":
        return 0.0 if for_white else 1.0
    elif result_str == "1/2-1/2":
        return 0.5
    return 0.0


def main():
    # Check all binaries exist
    if not os.path.isfile(FULL_ENGINE):
        print(f"Full engine not found: {FULL_ENGINE}")
        print("Build: cd chess/engine && make ablation")
        sys.exit(1)

    missing = []
    for suffix, _, desc in VARIANTS:
        path = os.path.join(BUILD_DIR, f"uci_{suffix}")
        if not os.path.isfile(path):
            missing.append(f"uci_{suffix}")
    if missing:
        print(f"Missing ablation binaries: {', '.join(missing)}")
        print("Build: cd chess/engine && make ablation")
        sys.exit(1)

    total_games = len(VARIANTS) * GAMES_PER_MATCH
    print(f"Ablation Tournament: {len(VARIANTS)} features, {GAMES_PER_MATCH} games each ({total_games} total)")
    print(f"Time control: {MOVETIME}s/move, max {MAX_WORKERS} concurrent matches")
    print(f"Both engines use 2000-node limit")
    print()

    pgn_path = os.path.join(BASE_DIR, "ablation.pgn")
    with open(pgn_path, "w"):
        pass

    results = {}
    results_lock = threading.Lock()

    def append_game(game):
        with pgn_lock:
            with open(pgn_path, "a") as f:
                print(game, file=f)
                print(file=f)

    def run_match(suffix, flag, desc):
        variant_path = os.path.join(BUILD_DIR, f"uci_{suffix}")
        variant_name = f"Full-{suffix}"
        full_name = "Full"
        half = GAMES_PER_MATCH // 2
        event = f"Ablation: Full vs {suffix}"

        log(f"  Starting: Full vs {suffix} ({desc})")

        games = []
        for i in range(GAMES_PER_MATCH):
            if i < half:
                # Full as white vs variant as black
                g = play_game(FULL_ENGINE, variant_path, full_name, variant_name, event)
                full_sc = result_score(g.headers["Result"], True)
            else:
                # Variant as white vs full as black
                g = play_game(variant_path, FULL_ENGINE, variant_name, full_name, event)
                full_sc = result_score(g.headers["Result"], False)
            games.append((g, full_sc))

        full_score = sum(sc for _, sc in games)
        full_wins = sum(1 for _, sc in games if sc == 1.0)
        full_draws = sum(1 for _, sc in games if sc == 0.5)
        full_losses = sum(1 for _, sc in games if sc == 0.0)

        log(f"  Done: Full vs {suffix}  +{full_wins}={full_draws}-{full_losses}  "
            f"Full {full_score:.1f}/{GAMES_PER_MATCH}")

        with results_lock:
            for g, _ in games:
                append_game(g)
            results[suffix] = {
                "desc": desc,
                "w": full_wins, "d": full_draws, "l": full_losses,
                "score": full_score
            }

    with ThreadPoolExecutor(max_workers=MAX_WORKERS) as pool:
        futures = {pool.submit(run_match, s, f, d): s for s, f, d in VARIANTS}
        done = 0
        for fut in as_completed(futures):
            done += 1
            try:
                fut.result()
            except Exception as e:
                name = futures[fut]
                log(f"  ERROR {name}: {e}")
            log(f"  Progress: {done}/{len(VARIANTS)} features tested")

    print(f"\nPGNs saved to: {pgn_path}")

    # Results table
    print(f"\n{'='*75}")
    print(f"{'ABLATION RESULTS — Full Engine vs Feature-Removed Variants':^75}")
    print(f"{'='*75}")
    print(f"  Full score > 10 means the removed feature was HELPING (feature is valuable)")
    print(f"  Full score < 10 means the removed feature was HURTING (feature is harmful)")
    print(f"{'='*75}")
    print(f"{'Removed Feature':<40} {'W':>3} {'D':>3} {'L':>3} {'Full':>6} {'/ '+str(GAMES_PER_MATCH):>5} {'Elo +/-':>8}")
    print(f"{'-'*75}")

    for suffix, _, _ in VARIANTS:
        if suffix in results:
            r = results[suffix]
            pct = r["score"] / GAMES_PER_MATCH
            # Approximate Elo difference from win percentage
            if pct >= 1.0:
                elo_diff = "+inf"
            elif pct <= 0.0:
                elo_diff = "-inf"
            else:
                import math
                elo_diff = f"{-400 * math.log10((1 - pct) / pct):+.0f}"
            print(f"  {r['desc']:<38} {r['w']:>3} {r['d']:>3} {r['l']:>3} "
                  f"{r['score']:>5.1f}  /{GAMES_PER_MATCH}  {elo_diff:>8}")

    print(f"{'='*75}")
    print()
    print("Interpretation:")
    print("  Positive Elo = feature HELPS (full engine beats the variant without it)")
    print("  Negative Elo = feature HURTS (should be removed)")


if __name__ == "__main__":
    main()
