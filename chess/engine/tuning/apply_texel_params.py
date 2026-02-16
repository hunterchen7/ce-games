#!/usr/bin/env python3
"""Apply trained Texel scales to chess/engine/src/eval.c."""

from __future__ import annotations

import argparse
import json
import re
from pathlib import Path

from texel_tune import PIECE_NAMES, EvalConstants, parse_eval_constants


SCALAR_MACRO_MAP = {
    "bishop_pair_mg": "BISHOP_PAIR_MG",
    "bishop_pair_eg": "BISHOP_PAIR_EG",
    "tempo_mg": "TEMPO_MG",
    "tempo_eg": "TEMPO_EG",
    "doubled_mg": "DOUBLED_MG",
    "doubled_eg": "DOUBLED_EG",
    "isolated_mg": "ISOLATED_MG",
    "isolated_eg": "ISOLATED_EG",
    "rook_open_mg": "ROOK_OPEN_MG",
    "rook_open_eg": "ROOK_OPEN_EG",
    "rook_semiopen_mg": "ROOK_SEMIOPEN_MG",
    "rook_semiopen_eg": "ROOK_SEMIOPEN_EG",
    "shield_mg": "SHIELD_MG",
    "shield_eg": "SHIELD_EG",
}

PIECE_LABELS = ["Pawn", "Knight", "Bishop", "Rook", "Queen", "King"]


def get_attr(c: EvalConstants, name: str):
    return getattr(c, name)


def replace_define(text: str, macro: str, new_value: int) -> tuple[str, int]:
    pattern = rf"(#define\s+{re.escape(macro)}\s+)(-?\d+)"
    repl = rf"\g<1>{new_value}"
    return re.subn(pattern, repl, text)


def format_array_1d(values: list[int]) -> str:
    return "{ " + ", ".join(str(v) for v in values) + " }"


def replace_array_1d(text: str, name: str, values: list[int]) -> tuple[str, int]:
    array_body = format_array_1d(values)
    pattern = (
        rf"((?:static\s+)?const\s+int16_t\s+{re.escape(name)}\s*\[[^\]]*\]\s*=\s*)"
        rf"\{{[^\}}]*\}}\s*;"
    )
    repl = rf"\g<1>{array_body};"
    return re.subn(pattern, repl, text, flags=re.S)


def has_array_decl(text: str, name: str) -> bool:
    pattern = rf"(?:static\s+)?const\s+int16_t\s+{re.escape(name)}\s*\[[^\]]*\]\s*="
    return re.search(pattern, text, flags=re.S) is not None


def format_table(values: list[list[int]]) -> str:
    lines = ["{"]
    for i, row in enumerate(values):
        lines.append(f"    /* {PIECE_LABELS[i]} */")
        lines.append("    {")
        for off in range(0, 64, 8):
            seg = ", ".join(f"{int(v):4d}" for v in row[off : off + 8])
            lines.append(f"        {seg},")
        lines.append("    },")
    lines.append("}")
    return "\n".join(lines)


def replace_table(text: str, name: str, values: list[list[int]]) -> tuple[str, int]:
    table_body = format_table(values)
    pattern = rf"(const\s+int16_t\s+{re.escape(name)}\s*\[[^\]]+\]\s*\[[^\]]+\]\s*=\s*)\{{.*?\}}\s*;"

    def repl(m: re.Match[str]) -> str:
        return m.group(1) + table_body + ";"

    return re.subn(pattern, repl, text, flags=re.S)


def param_scale(scales: dict[str, float], name: str) -> float:
    return float(scales.get(name, 1.0))


def param_base(base_values: dict[str, object] | None, name: str, fallback: int) -> int:
    if base_values is None:
        return fallback
    if name not in base_values:
        return fallback
    return int(base_values[name])  # type: ignore[arg-type]


def apply_vector_scales(
    *,
    scales: dict[str, float],
    base_values: dict[str, object] | None,
    prefix: str,
    suffix_from_index,
    current: list[int],
) -> list[int]:
    out: list[int] = []
    for i, old in enumerate(current):
        suffix = suffix_from_index(i)
        pname = f"{prefix}{suffix}"
        scale = param_scale(scales, pname)
        base = param_base(base_values, pname, old)
        out.append(int(round(base * scale)))
    return out


def validate_table(table: list[list[int]], name: str) -> None:
    if len(table) != 6:
        raise ValueError(f"{name}: expected 6 rows, got {len(table)}")
    for i, row in enumerate(table):
        if len(row) != 64:
            raise ValueError(f"{name}[{i}]: expected 64 cols, got {len(row)}")


def replace_material_comment_values(text: str, key: str, values: list[int]) -> tuple[str, int]:
    pattern = rf"({re.escape(key)}\s*=\s*)\{{[^\}}]*\}}"
    repl = rf"\g<1>{format_array_1d(values)}"
    return re.subn(pattern, repl, text, count=1)


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
    base_tables: dict[str, object] | None = data.get("base_tables")
    table_decomp: dict[str, object] | None = data.get("table_decomp")

    constants = parse_eval_constants(eval_c_path)
    text = eval_c_path.read_text(encoding="utf-8")

    changes: list[str] = []

    for param_name, macro in SCALAR_MACRO_MAP.items():
        old = int(get_attr(constants, param_name))
        scale = param_scale(scales, param_name)
        base = param_base(base_values, param_name, old)
        new = int(round(base * scale))

        text, n = replace_define(text, macro, new)
        if n != 1:
            raise RuntimeError(f"failed to replace macro {macro}")

        if new != old:
            changes.append(f"{macro}: {old} -> {new} (base={base}, scale={scale:.6f})")

    connected_mg_old = [int(x) for x in constants.connected_bonus_mg.tolist()]
    connected_eg_old = [int(x) for x in constants.connected_bonus_eg.tolist()]
    passed_mg_old = [int(x) for x in constants.passed_mg.tolist()]
    passed_eg_old = [int(x) for x in constants.passed_eg.tolist()]
    knight_mg_old = [int(x) for x in constants.knight_mob_mg.tolist()]
    knight_eg_old = [int(x) for x in constants.knight_mob_eg.tolist()]
    bishop_mg_old = [int(x) for x in constants.bishop_mob_mg.tolist()]
    bishop_eg_old = [int(x) for x in constants.bishop_mob_eg.tolist()]

    connected_mg_new = apply_vector_scales(
        scales=scales,
        base_values=base_values,
        prefix="connected_mg_r",
        suffix_from_index=lambda i: str(i + 2),
        current=connected_mg_old,
    )
    connected_eg_new = apply_vector_scales(
        scales=scales,
        base_values=base_values,
        prefix="connected_eg_r",
        suffix_from_index=lambda i: str(i + 2),
        current=connected_eg_old,
    )

    passed_mg_new = apply_vector_scales(
        scales=scales,
        base_values=base_values,
        prefix="passed_mg_r",
        suffix_from_index=lambda i: str(i + 2),
        current=passed_mg_old,
    )
    passed_eg_new = apply_vector_scales(
        scales=scales,
        base_values=base_values,
        prefix="passed_eg_r",
        suffix_from_index=lambda i: str(i + 2),
        current=passed_eg_old,
    )

    knight_mg_new = apply_vector_scales(
        scales=scales,
        base_values=base_values,
        prefix="knight_mob_mg_",
        suffix_from_index=lambda i: str(i),
        current=knight_mg_old,
    )
    knight_eg_new = apply_vector_scales(
        scales=scales,
        base_values=base_values,
        prefix="knight_mob_eg_",
        suffix_from_index=lambda i: str(i),
        current=knight_eg_old,
    )

    bishop_mg_new = apply_vector_scales(
        scales=scales,
        base_values=base_values,
        prefix="bishop_mob_mg_",
        suffix_from_index=lambda i: str(i),
        current=bishop_mg_old,
    )
    bishop_eg_new = apply_vector_scales(
        scales=scales,
        base_values=base_values,
        prefix="bishop_mob_eg_",
        suffix_from_index=lambda i: str(i),
        current=bishop_eg_old,
    )

    has_connected_split = has_array_decl(text, "connected_bonus_mg") and has_array_decl(text, "connected_bonus_eg")
    if has_connected_split:
        for array_name, values_new, values_old in (
            ("connected_bonus_mg", connected_mg_new, connected_mg_old),
            ("connected_bonus_eg", connected_eg_new, connected_eg_old),
        ):
            text, n = replace_array_1d(text, array_name, values_new)
            if n != 1:
                raise RuntimeError(f"failed to replace array {array_name}")
            if values_new != values_old:
                changes.append(f"{array_name}: updated")
    else:
        # Backward compatibility: one connected array used for both mg and eg.
        merged_connected = [int(round((a + b) * 0.5)) for a, b in zip(connected_mg_new, connected_eg_new)]
        text, n = replace_array_1d(text, "connected_bonus", merged_connected)
        if n != 1:
            raise RuntimeError("failed to replace array connected_bonus")
        changes.append("connected_bonus: updated (merged mg/eg)")

    for array_name, values_new, values_old in (
        ("passed_mg", passed_mg_new, passed_mg_old),
        ("passed_eg", passed_eg_new, passed_eg_old),
        ("knight_mob_mg", knight_mg_new, knight_mg_old),
        ("knight_mob_eg", knight_eg_new, knight_eg_old),
        ("bishop_mob_mg", bishop_mg_new, bishop_mg_old),
        ("bishop_mob_eg", bishop_eg_new, bishop_eg_old),
    ):
        text, n = replace_array_1d(text, array_name, values_new)
        if n != 1:
            raise RuntimeError(f"failed to replace array {array_name}")
        if values_new != values_old:
            changes.append(f"{array_name}: updated")

    has_split_scales = any(k.startswith("material_") or k.startswith("pst_") for k in scales)

    if has_split_scales:
        if (
            table_decomp
            and "mg_material" in table_decomp
            and "eg_material" in table_decomp
            and "mg_pst" in table_decomp
            and "eg_pst" in table_decomp
        ):
            mg_material_base = [int(v) for v in table_decomp["mg_material"]]  # type: ignore[index]
            eg_material_base = [int(v) for v in table_decomp["eg_material"]]  # type: ignore[index]
            mg_pst_base = [[int(v) for v in row] for row in table_decomp["mg_pst"]]  # type: ignore[index]
            eg_pst_base = [[int(v) for v in row] for row in table_decomp["eg_pst"]]  # type: ignore[index]
        else:
            if base_tables and "mg_table" in base_tables:
                mg_table_base = [[int(v) for v in row] for row in base_tables["mg_table"]]  # type: ignore[index]
            else:
                mg_table_base = [[int(v) for v in row] for row in constants.mg_table.tolist()]
            if base_tables and "eg_table" in base_tables:
                eg_table_base = [[int(v) for v in row] for row in base_tables["eg_table"]]  # type: ignore[index]
            else:
                eg_table_base = [[int(v) for v in row] for row in constants.eg_table.tolist()]

            validate_table(mg_table_base, "mg_table")
            validate_table(eg_table_base, "eg_table")

            mg_material_base = [int(v) for v in constants.mg_value.tolist()]
            eg_material_base = [int(v) for v in constants.eg_value.tolist()]
            mg_pst_base = [
                [int(mg_table_base[p][sq] - mg_material_base[p]) for sq in range(64)]
                for p in range(6)
            ]
            eg_pst_base = [
                [int(eg_table_base[p][sq] - eg_material_base[p]) for sq in range(64)]
                for p in range(6)
            ]

        validate_table(mg_pst_base, "mg_pst")
        validate_table(eg_pst_base, "eg_pst")
        if len(mg_material_base) != 6 or len(eg_material_base) != 6:
            raise ValueError("material bases must have 6 entries")

        mg_new = [[0 for _ in range(64)] for _ in range(6)]
        eg_new = [[0 for _ in range(64)] for _ in range(6)]

        mg_material_new: list[int] = []
        eg_material_new: list[int] = []

        for piece_idx, piece_name in enumerate(PIECE_NAMES):
            m_mg_scale = param_scale(scales, f"material_{piece_name}_mg")
            m_eg_scale = param_scale(scales, f"material_{piece_name}_eg")
            pst_mg_scale = param_scale(scales, f"pst_{piece_name}_mg")
            pst_eg_scale = param_scale(scales, f"pst_{piece_name}_eg")

            mat_mg = int(round(mg_material_base[piece_idx] * m_mg_scale))
            mat_eg = int(round(eg_material_base[piece_idx] * m_eg_scale))
            mg_material_new.append(mat_mg)
            eg_material_new.append(mat_eg)

            for sq in range(64):
                mg_term = int(round(mg_pst_base[piece_idx][sq] * pst_mg_scale))
                eg_term = int(round(eg_pst_base[piece_idx][sq] * pst_eg_scale))
                mg_new[piece_idx][sq] = mat_mg + mg_term
                eg_new[piece_idx][sq] = mat_eg + eg_term

            if (
                abs(m_mg_scale - 1.0) > 1e-12
                or abs(m_eg_scale - 1.0) > 1e-12
                or abs(pst_mg_scale - 1.0) > 1e-12
                or abs(pst_eg_scale - 1.0) > 1e-12
            ):
                changes.append(
                    f"{piece_name}: material_mg={m_mg_scale:.6f} material_eg={m_eg_scale:.6f} "
                    f"pst_mg={pst_mg_scale:.6f} pst_eg={pst_eg_scale:.6f}"
                )

        text, _ = replace_material_comment_values(text, "mg_value", mg_material_new)
        text, _ = replace_material_comment_values(text, "eg_value", eg_material_new)
    else:
        if base_tables and "mg_table" in base_tables:
            mg_base = [[int(v) for v in row] for row in base_tables["mg_table"]]  # type: ignore[index]
        else:
            mg_base = [[int(v) for v in row] for row in constants.mg_table.tolist()]
        if base_tables and "eg_table" in base_tables:
            eg_base = [[int(v) for v in row] for row in base_tables["eg_table"]]  # type: ignore[index]
        else:
            eg_base = [[int(v) for v in row] for row in constants.eg_table.tolist()]

        validate_table(mg_base, "mg_table")
        validate_table(eg_base, "eg_table")

        mg_new = [row[:] for row in mg_base]
        eg_new = [row[:] for row in eg_base]

        for piece_idx, piece_name in enumerate(PIECE_NAMES):
            mg_scale = param_scale(scales, f"table_{piece_name}_mg")
            eg_scale = param_scale(scales, f"table_{piece_name}_eg")
            for sq in range(64):
                mg_new[piece_idx][sq] = int(round(mg_base[piece_idx][sq] * mg_scale))
                eg_new[piece_idx][sq] = int(round(eg_base[piece_idx][sq] * eg_scale))
            if abs(mg_scale - 1.0) > 1e-12 or abs(eg_scale - 1.0) > 1e-12:
                changes.append(
                    f"table_{piece_name}: mg_scale={mg_scale:.6f} eg_scale={eg_scale:.6f}"
                )

    text, n = replace_table(text, "mg_table", mg_new)
    if n != 1:
        raise RuntimeError("failed to replace mg_table")
    text, n = replace_table(text, "eg_table", eg_new)
    if n != 1:
        raise RuntimeError("failed to replace eg_table")

    print("Applying tuned values:")
    if changes:
        for line in changes:
            print(f"  {line}")
    else:
        print("  (no effective changes)")

    if args.dry_run:
        print("Dry run; eval.c not written")
        return

    eval_c_path.write_text(text, encoding="utf-8")
    print(f"Updated {eval_c_path}")


if __name__ == "__main__":
    main()
