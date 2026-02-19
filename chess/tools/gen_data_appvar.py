#!/usr/bin/env python3
"""
Generate CHDATA.8xv — combined chess data appvar.

Layout:
  Offset 0:     piece sprites (2,400 bytes)
  Offset 2400:  Polyglot random numbers (6,248 bytes)
  Total: 8,648 bytes

Usage:
  python3 tools/gen_data_appvar.py
"""

import os
import re
import struct
import subprocess
import sys
import tempfile

import chess.polyglot

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
CHESS_DIR = os.path.dirname(SCRIPT_DIR)
HEADER_PATH = os.path.join(CHESS_DIR, "src", "piece_sprites.h")
OUTPUT_DIR = os.path.join(CHESS_DIR, "assets")
APPVAR_NAME = "CHDATA"

EXPECTED_SPRITE_SIZE = 2400   # 6 × 20 × 20
EXPECTED_RANDOM_COUNT = 781
EXPECTED_RANDOM_SIZE = EXPECTED_RANDOM_COUNT * 8  # 6,248
EXPECTED_TOTAL = EXPECTED_SPRITE_SIZE + EXPECTED_RANDOM_SIZE  # 8,648


def find_convbin():
    """Find the convbin executable."""
    for path_dir in os.environ.get("PATH", "").split(os.pathsep):
        candidate = os.path.join(path_dir, "convbin")
        if os.path.isfile(candidate) and os.access(candidate, os.X_OK):
            return candidate
    cedev = os.environ.get("CEDEV", os.path.expanduser("~/CEdev"))
    candidate = os.path.join(cedev, "bin", "convbin")
    if os.path.isfile(candidate):
        return candidate
    return None


def parse_sprites(header_path):
    """Parse piece_sprites.h and return flat bytes."""
    with open(header_path) as f:
        text = f.read()

    match = re.search(r'piece_sprites\[.*?\]\s*=\s*(\{.*\});', text, re.DOTALL)
    if not match:
        raise ValueError("Could not find piece_sprites array in header")

    numbers = [int(x) for x in re.findall(r'\d+', match.group(1))]
    if len(numbers) != EXPECTED_SPRITE_SIZE:
        raise ValueError(f"Expected {EXPECTED_SPRITE_SIZE} sprite values, got {len(numbers)}")

    return bytes(numbers)


def get_polyglot_randoms():
    """Return the 781 Polyglot random numbers as little-endian bytes."""
    randoms = chess.polyglot.POLYGLOT_RANDOM_ARRAY
    assert len(randoms) == EXPECTED_RANDOM_COUNT
    return struct.pack(f"<{EXPECTED_RANDOM_COUNT}Q", *randoms)


def main():
    convbin = find_convbin()
    if not convbin:
        print("Error: convbin not found.", file=sys.stderr)
        sys.exit(1)

    # Build payload
    sprites = parse_sprites(HEADER_PATH)
    randoms = get_polyglot_randoms()
    payload = sprites + randoms

    assert len(payload) == EXPECTED_TOTAL, f"Payload size {len(payload)} != {EXPECTED_TOTAL}"
    print(f"Sprites: {len(sprites)} bytes")
    print(f"Randoms: {len(randoms)} bytes")
    print(f"Total:   {len(payload)} bytes")

    # Convert to .8xv
    os.makedirs(OUTPUT_DIR, exist_ok=True)
    output_path = os.path.join(OUTPUT_DIR, f"{APPVAR_NAME}.8xv")

    with tempfile.NamedTemporaryFile(suffix=".bin", delete=False) as tmp:
        tmp.write(payload)
        tmp_path = tmp.name

    try:
        result = subprocess.run(
            [convbin, "-j", "bin", "-k", "8xv",
             "-i", tmp_path, "-o", output_path,
             "-n", APPVAR_NAME, "-r"],
            capture_output=True, text=True
        )
        if result.returncode != 0:
            err = result.stdout.strip() or result.stderr.strip()
            print(f"Error: convbin failed: {err}", file=sys.stderr)
            sys.exit(1)
    finally:
        os.unlink(tmp_path)

    size = os.path.getsize(output_path)
    print(f"Generated {output_path} ({size} bytes)")


if __name__ == "__main__":
    main()
