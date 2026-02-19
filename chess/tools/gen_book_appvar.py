#!/usr/bin/env python3
"""
Convert a Polyglot .bin opening book into TI-84 Plus CE AppVars (.8xv).

TI-OS limits each AppVar to ~65KB (one flash sector), so large books are
split across multiple numbered AppVars. A separate shared AppVar holds
the 781 Polyglot random numbers used for hash computation.

Output files:
  CHBKRN.8xv  — Polyglot randoms (6,248 bytes, shared across all tiers)
  <prefix>01.8xv, <prefix>02.8xv, ... — Book data chunks

Tier prefixes: CHBS (small), CHBM (medium), CHBL (large), CHBX (xl), CHBY (xxl)

Usage:
  python3 gen_book_appvar.py <input.bin> <output_dir> <tier>

Example:
  python3 gen_book_appvar.py books/book_medium.bin books/ medium

Requires: python-chess (pip install chess), convbin (CE C toolchain)
"""

import struct
import sys
import os
import subprocess
import tempfile
import math

import chess.polyglot

# TI-OS limits each AppVar to one 64KB flash sector.
# convbin enforces ~65,232 bytes max payload.
MAX_APPVAR_PAYLOAD = 65232

# Each book chunk: 4-byte header + N * 16-byte entries
ENTRY_SIZE = 16
CHUNK_HEADER = 4
MAX_ENTRIES_PER_CHUNK = (MAX_APPVAR_PAYLOAD - CHUNK_HEADER) // ENTRY_SIZE  # 4076

TIER_PREFIXES = {
    "small":  "CHBS",
    "medium": "CHBM",
    "large":  "CHBL",
    "xl":     "CHBX",
    "xxl":    "CHBY",
}

RANDOMS_APPVAR = "CHBKRN"


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


def run_convbin(convbin, raw_bin_path, output_8xv, appvar_name):
    """Convert a raw binary to an AppVar using convbin."""
    result = subprocess.run(
        [convbin,
         "-j", "bin", "-k", "8xv",
         "-i", raw_bin_path,
         "-o", output_8xv,
         "-n", appvar_name,
         "-r"],
        capture_output=True, text=True
    )
    if result.returncode != 0:
        err = result.stdout.strip() or result.stderr.strip()
        raise RuntimeError(f"convbin failed for {appvar_name}: {err}")


def generate_randoms_appvar(convbin, output_dir):
    """Generate CHBKRN.8xv containing the 781 Polyglot random uint64s."""
    output_path = os.path.join(output_dir, f"{RANDOMS_APPVAR}.8xv")

    randoms = chess.polyglot.POLYGLOT_RANDOM_ARRAY
    assert len(randoms) == 781
    payload = struct.pack("<781Q", *randoms)
    assert len(payload) == 781 * 8  # 6,248 bytes

    with tempfile.NamedTemporaryFile(suffix=".bin", delete=False) as tmp:
        tmp.write(payload)
        tmp_path = tmp.name

    try:
        run_convbin(convbin, tmp_path, output_path, RANDOMS_APPVAR)
    finally:
        os.unlink(tmp_path)

    print(f"  {RANDOMS_APPVAR}.8xv — {len(payload):,} bytes (Polyglot randoms)")
    return output_path


def generate_book_appvars(convbin, input_bin, output_dir, tier):
    """Split a Polyglot .bin book into chunked AppVars."""
    prefix = TIER_PREFIXES[tier]

    with open(input_bin, "rb") as f:
        book_data = f.read()

    if len(book_data) % ENTRY_SIZE != 0:
        print(f"WARNING: Book file size ({len(book_data)}) is not a multiple "
              f"of {ENTRY_SIZE} bytes.", file=sys.stderr)

    total_entries = len(book_data) // ENTRY_SIZE
    num_chunks = max(1, math.ceil(total_entries / MAX_ENTRIES_PER_CHUNK))

    print(f"  Book: {total_entries:,} entries -> {num_chunks} AppVar(s)")

    appvar_paths = []
    offset = 0

    for chunk_idx in range(num_chunks):
        chunk_num = chunk_idx + 1
        appvar_name = f"{prefix}{chunk_num:02d}"
        output_path = os.path.join(output_dir, f"{appvar_name}.8xv")

        # Determine entries for this chunk
        entries_remaining = total_entries - (offset // ENTRY_SIZE)
        entries_this_chunk = min(MAX_ENTRIES_PER_CHUNK, entries_remaining)
        chunk_data = book_data[offset:offset + entries_this_chunk * ENTRY_SIZE]
        offset += entries_this_chunk * ENTRY_SIZE

        # Build payload: 4-byte LE entry count + raw entries
        payload = struct.pack("<I", entries_this_chunk) + chunk_data

        with tempfile.NamedTemporaryFile(suffix=".bin", delete=False) as tmp:
            tmp.write(payload)
            tmp_path = tmp.name

        try:
            run_convbin(convbin, tmp_path, output_path, appvar_name)
        finally:
            os.unlink(tmp_path)

        print(f"  {appvar_name}.8xv — {entries_this_chunk:,} entries "
              f"({len(payload):,} bytes)")
        appvar_paths.append(output_path)

    return appvar_paths


def generate_group(convbin, appvar_paths, output_dir, tier):
    """Bundle all AppVars into a .8xg group for single-file transfer."""
    group_name = f"CHBOOK{tier[0].upper()}"  # e.g., CHBOOKS, CHBOOKM, ...
    output_path = os.path.join(output_dir, f"{group_name}.8xg")

    cmd = [convbin, "-j", "8x", "-k", "8xg", "-n", group_name, "-o", output_path]
    for path in appvar_paths:
        cmd.extend(["-i", path])

    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        err = result.stdout.strip() or result.stderr.strip()
        print(f"  WARNING: .8xg group generation failed: {err}", file=sys.stderr)
        return None

    size = os.path.getsize(output_path)
    print(f"  {group_name}.8xg — {size:,} bytes (group bundle)")
    return output_path


def main():
    if len(sys.argv) != 4:
        print(f"Usage: {sys.argv[0]} <input.bin> <output_dir> <tier>")
        print(f"  tier: {', '.join(TIER_PREFIXES.keys())}")
        sys.exit(1)

    input_bin = sys.argv[1]
    output_dir = sys.argv[2]
    tier = sys.argv[3]

    if not os.path.isfile(input_bin):
        print(f"Error: Input file not found: {input_bin}", file=sys.stderr)
        sys.exit(1)

    if tier not in TIER_PREFIXES:
        print(f"Error: Invalid tier '{tier}'. Choose from: "
              f"{', '.join(TIER_PREFIXES.keys())}", file=sys.stderr)
        sys.exit(1)

    convbin = find_convbin()
    if not convbin:
        print("Error: convbin not found. Install the CE C/C++ toolchain or "
              "set CEDEV env var.", file=sys.stderr)
        sys.exit(1)

    os.makedirs(output_dir, exist_ok=True)

    book_size = os.path.getsize(input_bin)
    num_entries = book_size // ENTRY_SIZE
    print(f"Input: {input_bin}")
    print(f"  Size: {book_size:,} bytes ({num_entries:,} entries)")
    print(f"  Tier: {tier} (prefix: {TIER_PREFIXES[tier]})")
    print()
    print("Generating AppVars:")

    # Randoms are now in CHDATA.8xv (generated by gen_data_appvar.py).
    # Only generate book data AppVars here.
    book_paths = generate_book_appvars(convbin, input_bin, output_dir, tier)

    # Bundle into .8xg group
    print()
    print("Bundling:")
    generate_group(convbin, book_paths, output_dir, tier)

    print()
    print(f"Total files: {len(book_paths)} AppVars + 1 group")
    print(f"Note: CHDATA.8xv (randoms + sprites) must also be loaded.")
    print(f"  Generate it with: python3 tools/gen_data_appvar.py")


if __name__ == "__main__":
    main()
