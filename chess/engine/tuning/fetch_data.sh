#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DATA_DIR="${1:-$SCRIPT_DIR/data}"
MONTH="${2:-2026-01}"

mkdir -p "$DATA_DIR"

FILE="lichess_db_broadcast_${MONTH}.pgn.zst"
URL="https://database.lichess.org/broadcast/${FILE}"
OUT="$DATA_DIR/$FILE"

if [[ -f "$OUT" ]]; then
  echo "Using existing file: $OUT"
else
  echo "Downloading $URL"
  curl -fL --retry 3 --retry-delay 2 -o "$OUT" "$URL"
fi

zstd -t "$OUT" >/dev/null

echo "$OUT"
