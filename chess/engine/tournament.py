#!/usr/bin/env python3
"""
Tournament: our engine vs Stockfish at multiple Elo levels.
30 games per level, 0.1s/move, 5 concurrent matches.

Usage:
  python3 tournament.py                         # default (uci binary, SF 2600-3000)
  python3 tournament.py --engine uci_4000 --elos 1700-2300 --pgn tournament_4000.pgn
  python3 tournament.py --nodes 25000 --no-book --elos 1500-2000
"""

import os
import sys
import math
import datetime
import threading
import argparse
import subprocess
from concurrent.futures import ThreadPoolExecutor, as_completed
import chess
import chess.engine
import chess.pgn

BASE_DIR = os.path.dirname(os.path.abspath(__file__))
BUILD_DIR = os.path.join(BASE_DIR, "build")
STOCKFISH = "/opt/homebrew/bin/stockfish"

GAMES_PER_MATCH = 30  # overridden by --games
MOVETIME = 0.1
MAX_WORKERS = 5

print_lock = threading.Lock()
pgn_lock = threading.Lock()

# Set by main() based on --nodes flag
OUR_LIMIT = None
SF_LIMIT = None
LIMIT_DESC = None


def log(msg):
    with print_lock:
        print(msg, flush=True)


def play_game(our_path, our_args, sf_path, sf_elo, our_is_white):
    board = chess.Board()
    game = chess.pgn.Game()
    game.headers["Event"] = f"Tournament vs SF-{sf_elo}"
    game.headers["Date"] = datetime.date.today().strftime("%Y.%m.%d")

    our_eng = chess.engine.SimpleEngine.popen_uci([our_path] + our_args)
    sf_eng = chess.engine.SimpleEngine.popen_uci(sf_path)
    sf_eng.configure({"Threads": 1, "UCI_LimitStrength": True, "UCI_Elo": sf_elo})

    our_name = f"TI84Chess-{LIMIT_DESC}"
    if our_is_white:
        w_eng, b_eng = our_eng, sf_eng
        game.headers["White"] = our_name
        game.headers["Black"] = f"SF-{sf_elo}"
    else:
        w_eng, b_eng = sf_eng, our_eng
        game.headers["White"] = f"SF-{sf_elo}"
        game.headers["Black"] = our_name

    node = game

    try:
        while not board.is_game_over(claim_draw=True) and board.fullmove_number <= 200:
            is_our_turn = (board.turn == chess.WHITE) == our_is_white
            eng = our_eng if is_our_turn else sf_eng
            limit = OUR_LIMIT if is_our_turn else SF_LIMIT
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
    global GAMES_PER_MATCH, OUR_LIMIT, SF_LIMIT, LIMIT_DESC

    parser = argparse.ArgumentParser(description="TI84Chess vs Stockfish tournament")
    parser.add_argument("--engine", default="uci",
                        help="Engine binary name in build/ (default: uci)")
    parser.add_argument("--elos", default="2600-3000",
                        help="SF Elo range as MIN-MAX (default: 2600-3000)")
    parser.add_argument("--step", type=int, default=100,
                        help="Elo step size (default: 100)")
    parser.add_argument("--pgn", default=None,
                        help="PGN output filename (default: auto-generated)")
    parser.add_argument("--games", type=int, default=None,
                        help="Games per match (default: 30)")
    parser.add_argument("--nodes", type=int, default=None,
                        help="Node limit per move (overrides time limit)")
    parser.add_argument("--no-book", action="store_true",
                        help="Run without opening book")
    parser.add_argument("--book", default=None,
                        help="Path to opening book .bin file (default: books/book_xxl.bin)")
    args = parser.parse_args()

    # Build engine with correct NODE_LIMIT if --nodes specified
    if args.nodes:
        engine_name = f"uci_{args.nodes}n"
        OUR_ENGINE = os.path.join(BUILD_DIR, engine_name)
        print(f"Building {engine_name} with NODE_LIMIT={args.nodes}...")
        ret = subprocess.run(
            ["make", "uci", f"CFLAGS=-Wall -Wextra -O2 -std=c11 -DNODE_LIMIT={args.nodes}"],
            cwd=BASE_DIR, capture_output=True, text=True
        )
        if ret.returncode != 0:
            print(f"Build failed:\n{ret.stderr}")
            sys.exit(1)
        # Rename to avoid overwriting the default uci binary
        default_uci = os.path.join(BUILD_DIR, "uci")
        os.rename(default_uci, OUR_ENGINE)
        print(f"Built {OUR_ENGINE}")
    else:
        OUR_ENGINE = os.path.join(BUILD_DIR, args.engine)

    if args.no_book:
        OUR_ENGINE_ARGS = []
    else:
        book_path = args.book or os.path.join(BASE_DIR, "..", "books", "book_xxl.bin")
        OUR_ENGINE_ARGS = ["-book", book_path]
    elo_min, elo_max = map(int, args.elos.split("-"))
    if args.games:
        GAMES_PER_MATCH = args.games
    SF_ELOS = list(range(elo_min, elo_max + 1, args.step))

    # Set up search limits
    # Node limit is compiled into our engine; give it generous time to hit the limit
    # Stockfish always gets a normal time limit
    SF_LIMIT = chess.engine.Limit(time=MOVETIME)
    if args.nodes:
        OUR_LIMIT = chess.engine.Limit(time=10.0)  # generous; internal NODE_LIMIT stops search
        LIMIT_DESC = f"{args.nodes}n"
    else:
        OUR_LIMIT = chess.engine.Limit(time=MOVETIME)
        LIMIT_DESC = f"{MOVETIME}s"

    # PGN output path: pgn/<date>/<name>.pgn
    today = datetime.date.today().strftime("%Y-%m-%d")
    pgn_dir = os.path.join(BASE_DIR, "pgn", today)
    os.makedirs(pgn_dir, exist_ok=True)
    book_tag = "nobook" if args.no_book else "book"
    if args.pgn:
        pgn_name = args.pgn
    else:
        pgn_name = f"tournament_{args.engine}_{LIMIT_DESC}_{book_tag}_{elo_min}-{elo_max}_{GAMES_PER_MATCH}g.pgn"
    pgn_path = os.path.join(pgn_dir, pgn_name)

    if not os.path.isfile(OUR_ENGINE):
        print(f"Missing engine: {OUR_ENGINE}")
        sys.exit(1)
    if not os.path.isfile(STOCKFISH):
        print(f"Missing stockfish: {STOCKFISH}")
        sys.exit(1)

    total = len(SF_ELOS) * GAMES_PER_MATCH
    print(f"Tournament: TI84Chess ({args.engine}, {LIMIT_DESC}/move, {book_tag}) vs Stockfish")
    print(f"SF levels: {SF_ELOS}")
    print(f"{GAMES_PER_MATCH} games per level = {total} total games")
    print(f"Concurrency: {MAX_WORKERS}")
    print()

    with open(pgn_path, "w"):
        pass

    results = {}
    results_lock = threading.Lock()

    def append_game(game):
        with pgn_lock:
            with open(pgn_path, "a") as f:
                print(game, file=f)
                print(file=f)

    def run_match(sf_elo):
        half = GAMES_PER_MATCH // 2
        wins = draws = losses = 0

        for i in range(GAMES_PER_MATCH):
            our_is_white = i < half
            g = play_game(OUR_ENGINE, OUR_ENGINE_ARGS, STOCKFISH, sf_elo, our_is_white)
            sc = score_for_our(g.headers["Result"], our_is_white)
            if sc == 1.0:
                wins += 1
            elif sc == 0.5:
                draws += 1
            else:
                losses += 1
            append_game(g)
            if (i + 1) % 10 == 0:
                log(f"  SF-{sf_elo}: {i+1}/{GAMES_PER_MATCH} games done (+{wins}={draws}-{losses})")

        total_sc = wins + 0.5 * draws
        pct = total_sc / GAMES_PER_MATCH
        log(f"  SF-{sf_elo}: FINAL +{wins}={draws}-{losses}  {total_sc:.1f}/{GAMES_PER_MATCH} ({pct*100:.0f}%) Elo diff: {elo_diff(pct)}")

        with results_lock:
            results[sf_elo] = {"w": wins, "d": draws, "l": losses, "score": total_sc}

    with ThreadPoolExecutor(max_workers=MAX_WORKERS) as pool:
        futures = {pool.submit(run_match, elo): elo for elo in SF_ELOS}
        for fut in as_completed(futures):
            try:
                fut.result()
            except Exception as ex:
                elo = futures[fut]
                log(f"  ERROR SF-{elo}: {ex}")

    print(f"\nPGNs saved to: {pgn_path}")
    title = f"TI84Chess ({args.engine}, {LIMIT_DESC}, {book_tag}) vs Stockfish"
    print(f"\n{'='*60}")
    print(f"{title:^60}")
    print(f"{'='*60}")
    print(f"  {'SF Elo':<10} {'W':>4} {'D':>4} {'L':>4}  {'Score':>8}  {'Pct':>6}  {'Elo diff':>10}")
    print(f"  {'-'*52}")

    for elo in SF_ELOS:
        if elo in results:
            r = results[elo]
            sc = r["score"]
            pct = sc / GAMES_PER_MATCH
            print(f"  SF-{elo:<6} {r['w']:>4} {r['d']:>4} {r['l']:>4}  {sc:>5.1f}/{GAMES_PER_MATCH}  {pct*100:>5.1f}%  {elo_diff(pct):>10}")

    print(f"{'='*60}")


if __name__ == "__main__":
    main()
