#!/usr/bin/env python3
"""
Ablation vs Stockfish: each engine variant plays 20 games against SF at
1400, 1500, 1600, 1700. Measures absolute strength of each variant.
"""

import os
import sys
import math
import datetime
import threading
from concurrent.futures import ThreadPoolExecutor, as_completed
import chess
import chess.engine
import chess.pgn

BASE_DIR = os.path.dirname(os.path.abspath(__file__))
BUILD_DIR = os.path.join(BASE_DIR, "build")
STOCKFISH = "/opt/homebrew/bin/stockfish"

ENGINES = [
    ("uci_full",          "Full"),
    ("uci_baseline",      "Baseline"),
    ("uci_no_tempo",      "-Tempo"),
    ("uci_no_pawns",      "-Pawns"),
    ("uci_no_passed",     "-Passed"),
    ("uci_no_rook_files", "-RookFiles"),
    ("uci_no_mobility",   "-Mobility"),
    ("uci_no_shield",     "-Shield"),
]

SF_ELOS = [1700]
GAMES_PER_MATCH = 20
MOVETIME = 0.1
MAX_WORKERS = 12

print_lock = threading.Lock()
pgn_lock = threading.Lock()


def log(msg):
    with print_lock:
        print(msg, flush=True)


def play_game(our_path, sf_path, sf_elo, our_name, our_is_white):
    board = chess.Board()
    game = chess.pgn.Game()
    game.headers["Event"] = f"Ablation vs SF-{sf_elo}"
    game.headers["Date"] = datetime.date.today().strftime("%Y.%m.%d")

    our_eng = chess.engine.SimpleEngine.popen_uci(our_path)
    sf_eng = chess.engine.SimpleEngine.popen_uci(sf_path)
    sf_eng.configure({"Threads": 1, "UCI_LimitStrength": True, "UCI_Elo": sf_elo})

    if our_is_white:
        w_eng, b_eng = our_eng, sf_eng
        game.headers["White"] = our_name
        game.headers["Black"] = f"SF-{sf_elo}"
    else:
        w_eng, b_eng = sf_eng, our_eng
        game.headers["White"] = f"SF-{sf_elo}"
        game.headers["Black"] = our_name

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
        game.headers["Result"] = outcome.result() if outcome else "1/2-1/2"
    finally:
        for e in [w_eng, b_eng]:
            try:
                e.quit()
            except Exception:
                pass

    return game


def score_for_our(result_str, our_is_white):
    if result_str == "1-0":
        return 1.0 if our_is_white else 0.0
    elif result_str == "0-1":
        return 0.0 if our_is_white else 1.0
    elif result_str == "1/2-1/2":
        return 0.5
    return 0.0


def elo_diff(pct):
    if pct >= 1.0:
        return "+inf"
    elif pct <= 0.0:
        return "-inf"
    return f"{-400 * math.log10((1 - pct) / pct):+.0f}"


def main():
    # Verify all engines exist
    for binary, name in ENGINES:
        path = os.path.join(BUILD_DIR, binary)
        if not os.path.isfile(path):
            print(f"Missing: {path}")
            sys.exit(1)

    total = len(ENGINES) * len(SF_ELOS) * GAMES_PER_MATCH
    print(f"Ablation vs Stockfish: {len(ENGINES)} engines x {len(SF_ELOS)} SF levels x {GAMES_PER_MATCH} games = {total} total")
    print(f"SF levels: {SF_ELOS}")
    print(f"Time control: {MOVETIME}s/move, max {MAX_WORKERS} concurrent")
    print()

    pgn_path = os.path.join(BASE_DIR, "ablation_vs_sf.pgn")
    with open(pgn_path, "w"):
        pass

    # results[engine_name][sf_elo] = {w, d, l, score}
    results = {}
    results_lock = threading.Lock()

    def append_game(game):
        with pgn_lock:
            with open(pgn_path, "a") as f:
                print(game, file=f)
                print(file=f)

    def run_match(binary, name, sf_elo):
        our_path = os.path.join(BUILD_DIR, binary)
        half = GAMES_PER_MATCH // 2
        wins = draws = losses = 0

        for i in range(GAMES_PER_MATCH):
            our_is_white = i < half
            g = play_game(our_path, STOCKFISH, sf_elo, name, our_is_white)
            sc = score_for_our(g.headers["Result"], our_is_white)
            if sc == 1.0:
                wins += 1
            elif sc == 0.5:
                draws += 1
            else:
                losses += 1
            append_game(g)

        total_sc = wins + 0.5 * draws
        log(f"  {name:<12} vs SF-{sf_elo}: +{wins}={draws}-{losses}  {total_sc:.1f}/{GAMES_PER_MATCH}")

        with results_lock:
            if name not in results:
                results[name] = {}
            results[name][sf_elo] = {
                "w": wins, "d": draws, "l": losses, "score": total_sc
            }

    # Build all (engine, sf_elo) pairs
    tasks = [(b, n, elo) for b, n in ENGINES for elo in SF_ELOS]

    with ThreadPoolExecutor(max_workers=MAX_WORKERS) as pool:
        futures = {pool.submit(run_match, b, n, e): (n, e) for b, n, e in tasks}
        done = 0
        for fut in as_completed(futures):
            done += 1
            try:
                fut.result()
            except Exception as ex:
                n, e = futures[fut]
                log(f"  ERROR {n} vs SF-{e}: {ex}")
            if done % 8 == 0 or done == len(tasks):
                log(f"  Progress: {done}/{len(tasks)} matchups complete")

    print(f"\nPGNs saved to: {pgn_path}")

    # Results table
    print(f"\n{'='*78}")
    print(f"{'ABLATION vs STOCKFISH — Score out of ' + str(GAMES_PER_MATCH):^78}")
    print(f"{'='*78}")
    header = f"{'Engine':<14}"
    for elo in SF_ELOS:
        header += f"  {'SF-'+str(elo):>12}"
    header += f"  {'Total':>8}  {'Avg%':>6}"
    print(header)
    print(f"{'-'*78}")

    for _, name in ENGINES:
        if name not in results:
            continue
        line = f"  {name:<12}"
        total_sc = 0
        total_g = 0
        for elo in SF_ELOS:
            if elo in results[name]:
                r = results[name][elo]
                sc = r["score"]
                total_sc += sc
                total_g += GAMES_PER_MATCH
                pct = sc / GAMES_PER_MATCH * 100
                line += f"  {sc:>5.1f} ({pct:>4.0f}%)"
            else:
                line += f"  {'N/A':>12}"
        if total_g > 0:
            avg_pct = total_sc / total_g * 100
            line += f"  {total_sc:>6.1f}  {avg_pct:>5.1f}%"
        print(line)

    print(f"{'='*78}")


if __name__ == "__main__":
    main()
