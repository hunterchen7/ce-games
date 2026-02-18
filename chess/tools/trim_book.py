#!/usr/bin/env python3
"""
Trim a Polyglot opening book to various size tiers using BFS from the
starting position, so the resulting book contains connected, playable lines.

Polyglot .bin format: sorted array of 16-byte entries (big-endian)
  - uint64 key     (Zobrist hash of position)
  - uint16 move    (encoded move)
  - uint16 weight  (move quality: 2*wins + draws)
  - uint32 learn   (unused, set to 0)

Requires: python-chess (pip install chess)

Usage:
  python3 trim_book.py <source.bin> [--out-dir <dir>] [--tiers small,medium,large,xl,xxl]
  python3 trim_book.py <source.bin> --stats   # just print stats

Examples:
  python3 trim_book.py Cerebellum3Merge.bin
  python3 trim_book.py Cerebellum3Merge.bin --out-dir ../books --tiers small,large
"""

import argparse
import struct
import os
from collections import defaultdict, deque

import chess
import chess.polyglot

ENTRY_SIZE = 16
ENTRY_FMT = ">QHHl"  # key(u64), move(u16), weight(u16), learn(i32)

# Size tier targets (in bytes)
TIERS = {
    "small":  64  * 1024,         # 64 KB  ~4,000 entries
    "medium": 256 * 1024,         # 256 KB ~16,000 entries
    "large":  512 * 1024,         # 512 KB ~32,000 entries
    "xl":     1   * 1024 * 1024,  # 1 MB   ~65,000 entries
    "xxl":    2   * 1024 * 1024,  # 2 MB   ~130,000 entries
}


def read_book(path):
    """Read a Polyglot book into a dict: key -> [(move_int, weight), ...]"""
    book = defaultdict(list)
    with open(path, "rb") as f:
        while True:
            data = f.read(ENTRY_SIZE)
            if len(data) < ENTRY_SIZE:
                break
            key, move, weight, _learn = struct.unpack(ENTRY_FMT, data)
            book[key].append((move, weight))
    # Sort each position's moves by weight descending
    for key in book:
        book[key].sort(key=lambda m: m[1], reverse=True)
    return book


def write_book(entries, path):
    """Write entries to a Polyglot book file (sorted by key)."""
    entries.sort(key=lambda e: e[0])
    with open(path, "wb") as f:
        for key, move, weight in entries:
            f.write(struct.pack(ENTRY_FMT, key, move, weight, 0))


def polyglot_move_to_chess(board, poly_move_int):
    """Convert a Polyglot move integer to a python-chess Move.

    Polyglot encodes castling as king-to-rook, python-chess uses king-to-dest.
    """
    to_file   = (poly_move_int >> 0) & 0x7
    to_row    = (poly_move_int >> 3) & 0x7
    from_file = (poly_move_int >> 6) & 0x7
    from_row  = (poly_move_int >> 9) & 0x7
    promo     = (poly_move_int >> 12) & 0x7

    from_sq = chess.square(from_file, from_row)
    to_sq = chess.square(to_file, to_row)

    # Handle Polyglot castling encoding (king moves to rook square)
    if board.piece_type_at(from_sq) == chess.KING:
        if abs(from_file - to_file) > 1 or (from_row == to_row and
                board.piece_type_at(to_sq) == chess.ROOK and
                board.color_at(to_sq) == board.color_at(from_sq)):
            # Castling: convert rook-target to standard king destination
            if to_file > from_file:  # kingside
                to_sq = chess.square(6, from_row)
            else:  # queenside
                to_sq = chess.square(2, from_row)

    promotion = None
    if promo:
        promo_map = {1: chess.KNIGHT, 2: chess.BISHOP, 3: chess.ROOK, 4: chess.QUEEN}
        promotion = promo_map.get(promo)

    return chess.Move(from_sq, to_sq, promotion=promotion)


def move_to_uci_str(move_int):
    """Convert a Polyglot move integer to UCI string for display."""
    to_file   = (move_int >> 0) & 0x7
    to_row    = (move_int >> 3) & 0x7
    from_file = (move_int >> 6) & 0x7
    from_row  = (move_int >> 9) & 0x7
    promo     = (move_int >> 12) & 0x7
    uci = chr(ord('a') + from_file) + str(from_row + 1)
    uci += chr(ord('a') + to_file) + str(to_row + 1)
    if promo:
        uci += {1: 'n', 2: 'b', 3: 'r', 4: 'q'}.get(promo, '')
    return uci


def trim_hybrid(book, max_entries, max_moves_per_pos):
    """
    Hybrid trimming: depth-aware BFS from start + fill with highest-weight.

    Phase 1: BFS from the starting position.  Shallow positions (depth ≤ 8
             ply) get up to max_moves_per_pos alternatives for variety.
             Deeper positions get 1 move each for maximum depth coverage.
    Phase 2: Fill remaining space with the highest-weight positions from
             the full book (these can still be hit via transposition).

    Returns a list of (key, move_int, weight) tuples.
    """
    SHALLOW_DEPTH = 8  # ply — positions at depth ≤ this get full alternatives

    result = []
    used_keys_moves = set()  # (key, move_int) pairs already added

    # --- Phase 1: BFS from starting position ---
    # Track depth so we can give shallow positions more alternatives.
    visited = set()
    start = chess.Board()
    start_key = chess.polyglot.zobrist_hash(start)
    queue = deque()
    queue.append((start.copy(), 0))
    visited.add(start_key)
    shallow_keys = []   # positions at depth ≤ SHALLOW_DEPTH (BFS order)
    deep_keys = []      # positions at depth > SHALLOW_DEPTH (BFS order)

    while queue:
        board, depth = queue.popleft()
        key = chess.polyglot.zobrist_hash(board)

        if key not in book:
            continue

        if depth <= SHALLOW_DEPTH:
            shallow_keys.append(key)
        else:
            deep_keys.append(key)

        # Queue children from ALL source moves to discover the full tree
        for move_int, weight in book[key]:
            try:
                chess_move = polyglot_move_to_chess(board, move_int)
                if chess_move in board.legal_moves:
                    child = board.copy()
                    child.push(chess_move)
                    child_key = chess.polyglot.zobrist_hash(child)
                    if child_key not in visited and child_key in book:
                        visited.add(child_key)
                        queue.append((child, depth + 1))
            except Exception:
                pass

    # Add shallow positions with full alternatives (up to max_moves_per_pos)
    for key in shallow_keys:
        for move_int, weight in book[key][:max_moves_per_pos]:
            if len(result) >= max_entries:
                break
            if (key, move_int) not in used_keys_moves:
                result.append((key, move_int, weight))
                used_keys_moves.add((key, move_int))
        if len(result) >= max_entries:
            break

    # Add deep positions with 1 move each
    for key in deep_keys:
        if len(result) >= max_entries:
            break
        moves = book[key]
        if moves:
            move_int, weight = moves[0]
            if (key, move_int) not in used_keys_moves:
                result.append((key, move_int, weight))
                used_keys_moves.add((key, move_int))

    bfs_count = len(result)
    print(f"  Shallow (≤{SHALLOW_DEPTH} ply): {len(shallow_keys):,} positions"
          f"  Deep (>{SHALLOW_DEPTH} ply): {len(deep_keys):,} positions")

    # --- Phase 2: Fill with highest-weight positions ---
    # Collect all entries not yet added, sorted by weight descending
    remaining = []
    for key, moves in book.items():
        for move_int, weight in moves[:max_moves_per_pos]:
            if (key, move_int) not in used_keys_moves:
                remaining.append((weight, key, move_int))

    remaining.sort(reverse=True)  # highest weight first

    for weight, key, move_int in remaining:
        if len(result) >= max_entries:
            break
        result.append((key, move_int, weight))

    fill_count = len(result) - bfs_count
    total_discovered = len(shallow_keys) + len(deep_keys)
    print(f"  Phase 1 (BFS):  {bfs_count:,} entries ({total_discovered:,} positions discovered)")
    print(f"  Phase 2 (fill): {fill_count:,} entries")

    return result


def print_stats(book, label="Book", entries=None):
    """Print statistics about a book."""
    if entries is not None:
        # Stats from entry list
        positions = defaultdict(list)
        for key, move, weight in entries:
            positions[key].append((move, weight))
        total_entries = len(entries)
        weights = [e[2] for e in entries]
    else:
        # Stats from book dict
        positions = book
        total_entries = sum(len(m) for m in book.values())
        weights = [w for moves in book.values() for _, w in moves]

    total_positions = len(positions)
    size_bytes = total_entries * ENTRY_SIZE
    moves_per_pos = [len(m) for m in positions.values()]

    print(f"\n=== {label} ===")
    print(f"  Entries:    {total_entries:,}")
    print(f"  Positions:  {total_positions:,}")
    print(f"  Size:       {size_bytes:,} bytes ({size_bytes/1024:.1f} KB)")
    if moves_per_pos:
        print(f"  Moves/pos:  avg {sum(moves_per_pos)/len(moves_per_pos):.1f}, "
              f"max {max(moves_per_pos)}")
    if weights:
        print(f"  Weights:    min {min(weights)}, max {max(weights)}, "
              f"avg {sum(weights)/len(weights):.0f}")

    # Show starting position moves
    start_key = chess.polyglot.zobrist_hash(chess.Board())
    start_moves = positions.get(start_key, [])
    if start_moves:
        print(f"  Starting position moves:")
        if isinstance(start_moves[0], tuple) and len(start_moves[0]) == 3:
            # (key, move, weight) format
            for _, move, weight in sorted(start_moves, key=lambda x: x[2], reverse=True)[:5]:
                print(f"    {move_to_uci_str(move):6s} weight={weight}")
        else:
            # (move, weight) format
            for move, weight in sorted(start_moves, key=lambda x: x[1], reverse=True)[:5]:
                print(f"    {move_to_uci_str(move):6s} weight={weight}")


def compute_max_depth(entries, book):
    """Compute the max depth reached in the trimmed book via BFS."""
    visited = set()
    start = chess.Board()
    start_key = chess.polyglot.zobrist_hash(start)

    entry_keys = set(e[0] for e in entries)
    queue = deque()
    queue.append((start.copy(), 0))
    visited.add(start_key)
    max_depth = 0

    while queue:
        board, depth = queue.popleft()
        key = chess.polyglot.zobrist_hash(board)
        if key not in entry_keys:
            continue
        max_depth = max(max_depth, depth)

        # Get moves for this position from the trimmed entries
        pos_moves = [(m, w) for k, m, w in entries if k == key]
        if not pos_moves and key in book:
            pos_moves = book[key]

        for move_int, weight in pos_moves:
            try:
                chess_move = polyglot_move_to_chess(board, move_int)
                if chess_move in board.legal_moves:
                    child = board.copy()
                    child.push(chess_move)
                    child_key = chess.polyglot.zobrist_hash(child)
                    if child_key not in visited and child_key in entry_keys:
                        visited.add(child_key)
                        queue.append((child, depth + 1))
            except Exception:
                pass

    return max_depth


def main():
    parser = argparse.ArgumentParser(description="Trim a Polyglot opening book")
    parser.add_argument("source", help="Source .bin Polyglot book")
    parser.add_argument("--out-dir", default=".", help="Output directory (default: .)")
    parser.add_argument("--tiers", default="small,medium,large,xl,xxl",
                        help="Comma-separated list of tiers to generate")
    parser.add_argument("--stats", action="store_true",
                        help="Just print stats, don't generate tiers")
    parser.add_argument("--max-moves", type=int, default=None,
                        help="Max moves per position (default: auto per tier)")
    args = parser.parse_args()

    print(f"Reading {args.source}...")
    book = read_book(args.source)
    print_stats(book, f"Source: {os.path.basename(args.source)}")

    if args.stats:
        return

    os.makedirs(args.out_dir, exist_ok=True)

    requested_tiers = [t.strip() for t in args.tiers.split(",")]

    # Smaller tiers keep fewer alternatives to maximize depth coverage
    tier_max_moves = {
        "small":  1,
        "medium": 2,
        "large":  2,
        "xl":     3,
        "xxl":    3,
    }

    for tier_name in requested_tiers:
        if tier_name not in TIERS:
            print(f"Unknown tier: {tier_name}, skipping")
            continue

        target = TIERS[tier_name]
        max_entries = target // ENTRY_SIZE
        max_moves = args.max_moves or tier_max_moves.get(tier_name, 2)

        # Skip if source is already smaller than target
        source_entries = sum(len(m) for m in book.values())
        if source_entries * ENTRY_SIZE <= target:
            entries = [(k, m, w) for k, moves in book.items() for m, w in moves]
            out_path = os.path.join(args.out_dir, f"book_{tier_name}.bin")
            write_book(entries, out_path)
            print_stats(book, f"{tier_name} (source fits, copied as-is)")
            print(f"  -> {out_path}")
            continue

        print(f"\nTrimming {tier_name} (target: {target//1024} KB, "
              f"max {max_moves} moves/pos)...")
        entries = trim_hybrid(book, max_entries, max_moves)
        out_path = os.path.join(args.out_dir, f"book_{tier_name}.bin")
        write_book(entries, out_path)

        # Deduplicate for stats (BFS may visit same key from different paths)
        seen = set()
        unique_entries = []
        for e in entries:
            k = (e[0], e[1])
            if k not in seen:
                seen.add(k)
                unique_entries.append(e)

        print_stats(book, f"{tier_name}", entries=unique_entries)
        print(f"  -> {out_path}")

    print("\nDone!")


if __name__ == "__main__":
    main()
