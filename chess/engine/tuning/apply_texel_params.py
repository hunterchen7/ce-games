#!/usr/bin/env python3
"""Apply trained Texel scales to chess/engine/src/eval.c."""

from __future__ import annotations

import argparse
import json
import re
from pathlib import Path

from texel_tune import EvalConstants, parse_eval_constants


MACRO_PARAM_MAP = {
    "BISHOP_PAIR_MG": "bishop_pair_mg",
    "BISHOP_PAIR_EG": "bishop_pair_eg",
    "TEMPO_MG": "tempo_mg",
    "TEMPO_EG": "tempo_eg",
    "DOUBLED_MG": "doubled_mg",
    "DOUBLED_EG": "doubled_eg",
    "ISOLATED_MG": "isolated_mg",
    "ISOLATED_EG": "isolated_eg",
    "ROOK_OPEN_MG": "rook_open_mg",
    "ROOK_OPEN_EG": "rook_open_eg",
    "ROOK_SEMIOPEN_MG": "rook_semiopen_mg",
    "ROOK_SEMIOPEN_EG": "rook_semiopen_eg",
    "SHIELD_MG": "shield_mg",
}

ARRAY_PARAM_MAP = {
    "connected_bonus": "connected",
    "passed_mg": "passed_mg",
    "passed_eg": "passed_eg",
    "knight_mob_mg": "knight_mob_mg",
    "knight_mob_eg": "knight_mob_eg",
    "bishop_mob_mg": "bishop_mob_mg",
    "bishop_mob_eg": "bishop_mob_eg",
}


def get_attr(c: EvalConstants, name: str):
    return getattr(c, name)


def replace_define(text: str, macro: str, new_value: int) -> tuple[str, int]:
    pattern = rf"(#define\s+{re.escape(macro)}\s+)(-?\d+)"
    repl = rf"\g<1>{new_value}"
    return re.subn(pattern, repl, text)


def replace_array(text: str, name: str, values: list[int]) -> tuple[str, int]:
    array_body = "{ " + ", ".join(str(v) for v in values) + " }"
    pattern = (
        rf"((?:static\s+)?const\s+int16_t\s+{re.escape(name)}\s*\[[^\]]*\]\s*=\s*)"
        rf"\{{[^\}}]*\}}\s*;"
    )
    repl = rf"\g<1>{array_body};"
    return re.subn(pattern, repl, text, flags=re.S)


def main() -> None:
    parser = argparse.ArgumentParser(description="Apply trained Texel params to eval.c")
    parser.add_argument("--params", required=True, help="JSON output from texel_tune.py")
    parser.add_argument("--eval-c", default="chess/engine/src/eval.c")
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()

    params_path = Path(args.params)
    eval_c_path = Path(args.eval_c)

    data = json.loads(params_path.read_text(encoding="utf-8"))
    scales: dict[str, float] = data["scales"]
    base_values: dict[str, object] | None = data.get("base_values")

    constants = parse_eval_constants(eval_c_path)
    text = eval_c_path.read_text(encoding="utf-8")

    changes: list[str] = []

    for macro, param_name in MACRO_PARAM_MAP.items():
        old = int(get_attr(constants, macro.lower()))
        scale = float(scales[param_name])
        if base_values is not None and param_name in base_values:
            base = int(base_values[param_name])  # type: ignore[arg-type]
        else:
            base = old
        new = int(round(base * scale))
        text, n = replace_define(text, macro, new)
        if n != 1:
            raise RuntimeError(f"failed to replace macro {macro}")
        changes.append(f"{macro}: {old} -> {new} (base={base}, scale={scale:.5f})")

    for array_name, param_name in ARRAY_PARAM_MAP.items():
        old_vals = [int(x) for x in get_attr(constants, array_name).tolist()]
        scale = float(scales[param_name])
        if base_values is not None and param_name in base_values:
            base_vals = [int(x) for x in base_values[param_name]]  # type: ignore[index]
        else:
            base_vals = old_vals
        new_vals = [int(round(v * scale)) for v in base_vals]
        text, n = replace_array(text, array_name, new_vals)
        if n != 1:
            raise RuntimeError(f"failed to replace array {array_name}")
        changes.append(f"{array_name}: scale={scale:.5f}")

    print("Applying tuned values:")
    for line in changes:
        print(f"  {line}")

    if args.dry_run:
        print("Dry run; eval.c not written")
        return

    eval_c_path.write_text(text, encoding="utf-8")
    print(f"Updated {eval_c_path}")


if __name__ == "__main__":
    main()
