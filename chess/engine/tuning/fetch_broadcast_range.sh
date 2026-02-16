#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DATA_DIR="${1:-$SCRIPT_DIR/data}"
START_MONTH="${2:-2024-01}"
END_MONTH="${3:-2026-01}"

mkdir -p "$DATA_DIR"

python3 - "$START_MONTH" "$END_MONTH" <<'PY' | while IFS= read -r month; do
import sys
start = sys.argv[1]
end = sys.argv[2]

def parse(ym: str):
    y, m = ym.split('-')
    return int(y), int(m)

y, m = parse(start)
ey, em = parse(end)
if (y, m) > (ey, em):
    raise SystemExit("start month is after end month")

while (y, m) <= (ey, em):
    print(f"{y:04d}-{m:02d}")
    m += 1
    if m == 13:
        y += 1
        m = 1
PY
  "$SCRIPT_DIR/fetch_data.sh" "$DATA_DIR" "$month"
done

printf 'Done downloading broadcast range %s..%s into %s\n' "$START_MONTH" "$END_MONTH" "$DATA_DIR"
