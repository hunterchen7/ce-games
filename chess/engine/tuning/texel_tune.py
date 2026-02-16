#!/usr/bin/env python3
"""Texel tuning for chess/engine/src/eval.c feature groups.

This tuner supports two feature backends:
- engine: exact C-side term extraction via chess/engine/build/eval_terms
- python: fallback Python reconstruction of eval terms
"""

from __future__ import annotations

import argparse
import csv
import json
import math
import re
import subprocess
import tempfile
from dataclasses import dataclass
from pathlib import Path
from typing import Any

import chess
import numpy as np


PIECE_NAMES = ["pawn", "knight", "bishop", "rook", "queen", "king"]
PIECE_INDEX = {name: i for i, name in enumerate(PIECE_NAMES)}

# PeSTO material anchors (also present in eval.c comments).
DEFAULT_MG_VALUE = np.array([82, 337, 365, 477, 1025, 0], dtype=np.int32)
DEFAULT_EG_VALUE = np.array([94, 281, 297, 512, 936, 0], dtype=np.int32)

SCALAR_PARAMS = [
    "bishop_pair_mg",
    "bishop_pair_eg",
    "tempo_mg",
    "tempo_eg",
    "doubled_mg",
    "doubled_eg",
    "isolated_mg",
    "isolated_eg",
    "rook_open_mg",
    "rook_open_eg",
    "rook_semiopen_mg",
    "rook_semiopen_eg",
    "shield_mg",
    "shield_eg",
]


@dataclass(frozen=True)
class ParamSpec:
    name: str
    mg_key: str | None
    eg_key: str | None


@dataclass
class EvalConstants:
    mg_table: np.ndarray
    eg_table: np.ndarray
    phase_weight: np.ndarray
    mg_value: np.ndarray
    eg_value: np.ndarray
    bishop_pair_mg: int
    bishop_pair_eg: int
    tempo_mg: int
    tempo_eg: int
    doubled_mg: int
    doubled_eg: int
    isolated_mg: int
    isolated_eg: int
    connected_bonus_mg: np.ndarray
    connected_bonus_eg: np.ndarray
    passed_mg: np.ndarray
    passed_eg: np.ndarray
    rook_open_mg: int
    rook_open_eg: int
    rook_semiopen_mg: int
    rook_semiopen_eg: int
    shield_mg: int
    shield_eg: int
    knight_mob_mg: np.ndarray
    knight_mob_eg: np.ndarray
    bishop_mob_mg: np.ndarray
    bishop_mob_eg: np.ndarray


def strip_c_comments(text: str) -> str:
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    text = re.sub(r"//.*", "", text)
    return text


def parse_define(text: str, name: str) -> int:
    m = re.search(rf"#define\s+{re.escape(name)}\s+(-?\d+)", text)
    if not m:
        raise ValueError(f"missing define: {name}")
    return int(m.group(1))


def parse_array_block(block: str, expected_count: int | None = None) -> np.ndarray:
    nums = [int(x) for x in re.findall(r"-?\d+", block)]
    if expected_count is not None and len(nums) != expected_count:
        raise ValueError(f"expected {expected_count} entries, got {len(nums)}")
    return np.array(nums, dtype=np.int32)


def parse_named_array_1d(text: str, name: str, expected_count: int | None = None) -> np.ndarray:
    m = re.search(
        rf"(?:static\s+)?const\s+int16_t\s+{re.escape(name)}\s*\[[^\]]*\]\s*=\s*\{{(.*?)\}}\s*;",
        text,
        flags=re.S,
    )
    if not m:
        raise ValueError(f"missing array: {name}")
    return parse_array_block(m.group(1), expected_count)


def parse_material_from_comment(raw_text: str, key: str) -> np.ndarray | None:
    m = re.search(rf"{re.escape(key)}\s*=\s*\{{(.*?)\}}", raw_text, flags=re.S)
    if not m:
        return None
    vals = parse_array_block(m.group(1), expected_count=6)
    return vals


def parse_eval_constants(eval_c_path: Path) -> EvalConstants:
    raw = eval_c_path.read_text(encoding="utf-8")
    text = strip_c_comments(raw)

    mg_start = text.index("const int16_t mg_table")
    eg_start = text.index("const int16_t eg_table")
    feat_start = text.index("#define BISHOP_PAIR_MG")

    mg_block = text[mg_start:eg_start]
    eg_block = text[eg_start:feat_start]

    mg_vals = parse_array_block(mg_block[mg_block.index("{") : mg_block.rindex("}") + 1], 6 * 64)
    eg_vals = parse_array_block(eg_block[eg_block.index("{") : eg_block.rindex("}") + 1], 6 * 64)

    mg_table = mg_vals.reshape(6, 64)
    eg_table = eg_vals.reshape(6, 64)

    mg_value = parse_material_from_comment(raw, "mg_value")
    eg_value = parse_material_from_comment(raw, "eg_value")

    if mg_value is None:
        mg_value = np.rint(np.mean(mg_table, axis=1)).astype(np.int32)
    if eg_value is None:
        eg_value = np.rint(np.mean(eg_table, axis=1)).astype(np.int32)

    phase_weight = parse_named_array_1d(text, "phase_weight", expected_count=6)

    try:
        connected_bonus_mg = parse_named_array_1d(text, "connected_bonus_mg")
        connected_bonus_eg = parse_named_array_1d(text, "connected_bonus_eg")
    except ValueError:
        connected_bonus = parse_named_array_1d(text, "connected_bonus")
        connected_bonus_mg = connected_bonus.copy()
        connected_bonus_eg = connected_bonus.copy()

    # Eval uses ranks 2..7 => 6 buckets.
    connected_bonus_mg = connected_bonus_mg[:6]
    connected_bonus_eg = connected_bonus_eg[:6]

    return EvalConstants(
        mg_table=mg_table,
        eg_table=eg_table,
        phase_weight=phase_weight,
        mg_value=mg_value,
        eg_value=eg_value,
        bishop_pair_mg=parse_define(text, "BISHOP_PAIR_MG"),
        bishop_pair_eg=parse_define(text, "BISHOP_PAIR_EG"),
        tempo_mg=parse_define(text, "TEMPO_MG"),
        tempo_eg=parse_define(text, "TEMPO_EG"),
        doubled_mg=parse_define(text, "DOUBLED_MG"),
        doubled_eg=parse_define(text, "DOUBLED_EG"),
        isolated_mg=parse_define(text, "ISOLATED_MG"),
        isolated_eg=parse_define(text, "ISOLATED_EG"),
        connected_bonus_mg=connected_bonus_mg,
        connected_bonus_eg=connected_bonus_eg,
        passed_mg=parse_named_array_1d(text, "passed_mg")[:6],
        passed_eg=parse_named_array_1d(text, "passed_eg")[:6],
        rook_open_mg=parse_define(text, "ROOK_OPEN_MG"),
        rook_open_eg=parse_define(text, "ROOK_OPEN_EG"),
        rook_semiopen_mg=parse_define(text, "ROOK_SEMIOPEN_MG"),
        rook_semiopen_eg=parse_define(text, "ROOK_SEMIOPEN_EG"),
        shield_mg=parse_define(text, "SHIELD_MG"),
        shield_eg=parse_define(text, "SHIELD_EG"),
        knight_mob_mg=parse_named_array_1d(text, "knight_mob_mg"),
        knight_mob_eg=parse_named_array_1d(text, "knight_mob_eg"),
        bishop_mob_mg=parse_named_array_1d(text, "bishop_mob_mg"),
        bishop_mob_eg=parse_named_array_1d(text, "bishop_mob_eg"),
    )


def build_param_specs(c: EvalConstants) -> list[ParamSpec]:
    specs: list[ParamSpec] = []

    for piece in PIECE_NAMES:
        specs.append(ParamSpec(name=f"material_{piece}_mg", mg_key=f"material_{piece}_mg", eg_key=None))
        specs.append(ParamSpec(name=f"material_{piece}_eg", mg_key=None, eg_key=f"material_{piece}_eg"))
        specs.append(ParamSpec(name=f"pst_{piece}_mg", mg_key=f"pst_{piece}_mg", eg_key=None))
        specs.append(ParamSpec(name=f"pst_{piece}_eg", mg_key=None, eg_key=f"pst_{piece}_eg"))

    for name in SCALAR_PARAMS:
        if name.endswith("_mg"):
            specs.append(ParamSpec(name=name, mg_key=name, eg_key=None))
        else:
            specs.append(ParamSpec(name=name, mg_key=None, eg_key=name))

    for i in range(len(c.connected_bonus_mg)):
        rank = i + 2
        specs.append(ParamSpec(name=f"connected_mg_r{rank}", mg_key=f"connected_mg_r{rank}", eg_key=None))
        specs.append(ParamSpec(name=f"connected_eg_r{rank}", mg_key=None, eg_key=f"connected_eg_r{rank}"))

    for i in range(len(c.passed_mg)):
        rank = i + 2
        specs.append(ParamSpec(name=f"passed_mg_r{rank}", mg_key=f"passed_mg_r{rank}", eg_key=None))
        specs.append(ParamSpec(name=f"passed_eg_r{rank}", mg_key=None, eg_key=f"passed_eg_r{rank}"))

    for i in range(len(c.knight_mob_mg)):
        specs.append(ParamSpec(name=f"knight_mob_mg_{i}", mg_key=f"knight_mob_mg_{i}", eg_key=None))
        specs.append(ParamSpec(name=f"knight_mob_eg_{i}", mg_key=None, eg_key=f"knight_mob_eg_{i}"))

    for i in range(len(c.bishop_mob_mg)):
        specs.append(ParamSpec(name=f"bishop_mob_mg_{i}", mg_key=f"bishop_mob_mg_{i}", eg_key=None))
        specs.append(ParamSpec(name=f"bishop_mob_eg_{i}", mg_key=None, eg_key=f"bishop_mob_eg_{i}"))

    return specs


def build_base_values(c: EvalConstants, param_specs: list[ParamSpec]) -> dict[str, int]:
    scalar_attr_map = {name: name for name in SCALAR_PARAMS}

    out: dict[str, int] = {}
    for spec in param_specs:
        name = spec.name

        if name.startswith("material_") or name.startswith("pst_"):
            out[name] = 1
            continue

        if name in scalar_attr_map:
            out[name] = int(getattr(c, scalar_attr_map[name]))
            continue

        m = re.fullmatch(r"connected_(mg|eg)_r(\d+)", name)
        if m:
            phase, rank_s = m.groups()
            idx = int(rank_s) - 2
            arr = c.connected_bonus_mg if phase == "mg" else c.connected_bonus_eg
            out[name] = int(arr[idx])
            continue

        m = re.fullmatch(r"passed_(mg|eg)_r(\d+)", name)
        if m:
            phase, rank_s = m.groups()
            idx = int(rank_s) - 2
            arr = c.passed_mg if phase == "mg" else c.passed_eg
            out[name] = int(arr[idx])
            continue

        m = re.fullmatch(r"knight_mob_(mg|eg)_(\d+)", name)
        if m:
            phase, idx_s = m.groups()
            idx = int(idx_s)
            arr = c.knight_mob_mg if phase == "mg" else c.knight_mob_eg
            out[name] = int(arr[idx])
            continue

        m = re.fullmatch(r"bishop_mob_(mg|eg)_(\d+)", name)
        if m:
            phase, idx_s = m.groups()
            idx = int(idx_s)
            arr = c.bishop_mob_mg if phase == "mg" else c.bishop_mob_eg
            out[name] = int(arr[idx])
            continue

        raise ValueError(f"unknown param for base value: {name}")

    return out


def build_table_decomp(c: EvalConstants) -> dict[str, Any]:
    mg_material = c.mg_value.astype(np.int32)
    eg_material = c.eg_value.astype(np.int32)

    mg_pst = c.mg_table.astype(np.int32) - mg_material[:, None]
    eg_pst = c.eg_table.astype(np.int32) - eg_material[:, None]

    return {
        "mg_material": [int(x) for x in mg_material.tolist()],
        "eg_material": [int(x) for x in eg_material.tolist()],
        "mg_pst": [[int(v) for v in row] for row in mg_pst.tolist()],
        "eg_pst": [[int(v) for v in row] for row in eg_pst.tolist()],
    }


def raw_keys_from_specs(param_specs: list[ParamSpec]) -> set[str]:
    keys: set[str] = set()
    for spec in param_specs:
        if spec.mg_key is not None:
            keys.add(spec.mg_key)
        if spec.eg_key is not None:
            keys.add(spec.eg_key)
    return keys


def square_row_col(square: chess.Square) -> tuple[int, int]:
    rank = chess.square_rank(square)
    file = chess.square_file(square)
    row = 7 - rank
    col = file
    return row, col


def square_to_sq64(square: chess.Square) -> int:
    rank = chess.square_rank(square)
    file = chess.square_file(square)
    return (7 - rank) * 8 + file


def piece_sign(color: chess.Color) -> int:
    return 1 if color == chess.WHITE else -1


def build_pawn_masks(board: chess.Board, color: chess.Color) -> list[int]:
    masks = [0] * 8
    for sq in board.pieces(chess.PAWN, color):
        row, col = square_row_col(sq)
        masks[col] |= 1 << row
    return masks


def pawn_attacks_square(board: chess.Board, square: chess.Square, by_color: chess.Color) -> bool:
    rank = chess.square_rank(square)
    file = chess.square_file(square)

    attacker_rank = rank - 1 if by_color == chess.WHITE else rank + 1
    if attacker_rank < 0 or attacker_rank > 7:
        return False

    for attacker_file in (file - 1, file + 1):
        if attacker_file < 0 or attacker_file > 7:
            continue
        sq = chess.square(attacker_file, attacker_rank)
        p = board.piece_at(sq)
        if p is not None and p.piece_type == chess.PAWN and p.color == by_color:
            return True

    return False


def extract_position_terms_python(board: chess.Board, c: EvalConstants, raw_keys: set[str]) -> dict[str, Any]:
    phase = 0
    raw = {k: 0 for k in raw_keys}

    for square, piece in board.piece_map().items():
        idx = piece.piece_type - 1
        sq64 = square_to_sq64(square)
        pst_sq = sq64 if piece.color == chess.WHITE else (sq64 ^ 56)
        sgn = piece_sign(piece.color)

        piece_name = PIECE_NAMES[idx]
        mg_v = int(c.mg_value[idx])
        eg_v = int(c.eg_value[idx])
        mg_t = int(c.mg_table[idx, pst_sq])
        eg_t = int(c.eg_table[idx, pst_sq])

        raw[f"material_{piece_name}_mg"] += sgn * mg_v
        raw[f"material_{piece_name}_eg"] += sgn * eg_v
        raw[f"pst_{piece_name}_mg"] += sgn * (mg_t - mg_v)
        raw[f"pst_{piece_name}_eg"] += sgn * (eg_t - eg_v)

        phase += int(c.phase_weight[idx])

    phase = max(0, min(24, phase))

    w_bishops = len(board.pieces(chess.BISHOP, chess.WHITE))
    b_bishops = len(board.pieces(chess.BISHOP, chess.BLACK))
    if w_bishops >= 2:
        raw["bishop_pair_mg"] += c.bishop_pair_mg
        raw["bishop_pair_eg"] += c.bishop_pair_eg
    if b_bishops >= 2:
        raw["bishop_pair_mg"] -= c.bishop_pair_mg
        raw["bishop_pair_eg"] -= c.bishop_pair_eg

    # Preserve current eval.c behavior (including duplicated white tempo add).
    if board.turn == chess.WHITE:
        raw["tempo_mg"] += c.tempo_mg
        raw["tempo_eg"] += c.tempo_eg
    if board.turn == chess.WHITE:
        raw["tempo_mg"] += c.tempo_mg
        raw["tempo_eg"] += c.tempo_eg
    else:
        raw["tempo_mg"] -= c.tempo_mg
        raw["tempo_eg"] -= c.tempo_eg

    w_pawns = build_pawn_masks(board, chess.WHITE)
    b_pawns = build_pawn_masks(board, chess.BLACK)

    for sq in board.pieces(chess.PAWN, chess.WHITE):
        row, col = square_row_col(sq)
        rel_rank = chess.square_rank(sq)

        if w_pawns[col] & ~(1 << row):
            raw["doubled_mg"] -= c.doubled_mg
            raw["doubled_eg"] -= c.doubled_eg

        adj = 0
        if col > 0:
            adj |= w_pawns[col - 1]
        if col < 7:
            adj |= w_pawns[col + 1]
        if adj == 0:
            raw["isolated_mg"] -= c.isolated_mg
            raw["isolated_eg"] -= c.isolated_eg

        supported = False
        for df in (-1, 1):
            f = chess.square_file(sq) + df
            r = chess.square_rank(sq) - 1
            if 0 <= f <= 7 and 0 <= r <= 7:
                p = board.piece_at(chess.square(f, r))
                if p is not None and p.piece_type == chess.PAWN and p.color == chess.WHITE:
                    supported = True
                    break
        if supported and 2 <= rel_rank <= 7:
            ri = rel_rank - 2
            if ri < len(c.connected_bonus_mg):
                rank = ri + 2
                raw[f"connected_mg_r{rank}"] += int(c.connected_bonus_mg[ri])
                raw[f"connected_eg_r{rank}"] += int(c.connected_bonus_eg[ri])

        passed = True
        for f in range(col - 1, col + 2):
            if not (0 <= f <= 7):
                continue
            mask = b_pawns[f]
            for rr in range(0, row):
                if mask & (1 << rr):
                    passed = False
                    break
            if not passed:
                break

        if passed and rel_rank >= 2:
            ri = rel_rank - 2
            if ri < len(c.passed_mg):
                rank = ri + 2
                raw[f"passed_mg_r{rank}"] += int(c.passed_mg[ri])
                raw[f"passed_eg_r{rank}"] += int(c.passed_eg[ri])

    for sq in board.pieces(chess.PAWN, chess.BLACK):
        row, col = square_row_col(sq)
        rel_rank = row

        if b_pawns[col] & ~(1 << row):
            raw["doubled_mg"] += c.doubled_mg
            raw["doubled_eg"] += c.doubled_eg

        adj = 0
        if col > 0:
            adj |= b_pawns[col - 1]
        if col < 7:
            adj |= b_pawns[col + 1]
        if adj == 0:
            raw["isolated_mg"] += c.isolated_mg
            raw["isolated_eg"] += c.isolated_eg

        supported = False
        for df in (-1, 1):
            f = chess.square_file(sq) + df
            r = chess.square_rank(sq) + 1
            if 0 <= f <= 7 and 0 <= r <= 7:
                p = board.piece_at(chess.square(f, r))
                if p is not None and p.piece_type == chess.PAWN and p.color == chess.BLACK:
                    supported = True
                    break
        if supported and 2 <= rel_rank <= 7:
            ri = rel_rank - 2
            if ri < len(c.connected_bonus_mg):
                rank = ri + 2
                raw[f"connected_mg_r{rank}"] -= int(c.connected_bonus_mg[ri])
                raw[f"connected_eg_r{rank}"] -= int(c.connected_bonus_eg[ri])

        passed = True
        for f in range(col - 1, col + 2):
            if not (0 <= f <= 7):
                continue
            mask = w_pawns[f]
            for rr in range(row + 1, 8):
                if mask & (1 << rr):
                    passed = False
                    break
            if not passed:
                break

        if passed and rel_rank >= 2:
            ri = rel_rank - 2
            if ri < len(c.passed_mg):
                rank = ri + 2
                raw[f"passed_mg_r{rank}"] -= int(c.passed_mg[ri])
                raw[f"passed_eg_r{rank}"] -= int(c.passed_eg[ri])

    for sq in board.pieces(chess.ROOK, chess.WHITE):
        col = chess.square_file(sq)
        if w_pawns[col] == 0 and b_pawns[col] == 0:
            raw["rook_open_mg"] += c.rook_open_mg
            raw["rook_open_eg"] += c.rook_open_eg
        elif w_pawns[col] == 0 and b_pawns[col] != 0:
            raw["rook_semiopen_mg"] += c.rook_semiopen_mg
            raw["rook_semiopen_eg"] += c.rook_semiopen_eg

    for sq in board.pieces(chess.ROOK, chess.BLACK):
        col = chess.square_file(sq)
        if b_pawns[col] == 0 and w_pawns[col] == 0:
            raw["rook_open_mg"] -= c.rook_open_mg
            raw["rook_open_eg"] -= c.rook_open_eg
        elif b_pawns[col] == 0 and w_pawns[col] != 0:
            raw["rook_semiopen_mg"] -= c.rook_semiopen_mg
            raw["rook_semiopen_eg"] -= c.rook_semiopen_eg

    knight_offsets = [
        (-2, -1),
        (-2, 1),
        (-1, -2),
        (-1, 2),
        (1, -2),
        (1, 2),
        (2, -1),
        (2, 1),
    ]
    bishop_dirs = [(-1, -1), (-1, 1), (1, -1), (1, 1)]

    for color, enemy in ((chess.WHITE, chess.BLACK), (chess.BLACK, chess.WHITE)):
        sgn = 1 if color == chess.WHITE else -1

        for sq in board.pieces(chess.KNIGHT, color):
            mob = 0
            r0 = chess.square_rank(sq)
            f0 = chess.square_file(sq)
            for dr, df in knight_offsets:
                r = r0 + dr
                f = f0 + df
                if not (0 <= r <= 7 and 0 <= f <= 7):
                    continue
                dst = chess.square(f, r)
                occ = board.piece_at(dst)
                if occ is not None and occ.color == color:
                    continue
                if pawn_attacks_square(board, dst, enemy):
                    continue
                mob += 1
            mob = min(mob, len(c.knight_mob_mg) - 1)
            raw[f"knight_mob_mg_{mob}"] += sgn * int(c.knight_mob_mg[mob])
            raw[f"knight_mob_eg_{mob}"] += sgn * int(c.knight_mob_eg[mob])

        for sq in board.pieces(chess.BISHOP, color):
            mob = 0
            r0 = chess.square_rank(sq)
            f0 = chess.square_file(sq)
            for dr, df in bishop_dirs:
                r = r0 + dr
                f = f0 + df
                while 0 <= r <= 7 and 0 <= f <= 7:
                    dst = chess.square(f, r)
                    occ = board.piece_at(dst)
                    if occ is not None and occ.color == color:
                        break
                    if not pawn_attacks_square(board, dst, enemy):
                        mob += 1
                    if occ is not None:
                        break
                    r += dr
                    f += df
            mob = min(mob, len(c.bishop_mob_mg) - 1)
            raw[f"bishop_mob_mg_{mob}"] += sgn * int(c.bishop_mob_mg[mob])
            raw[f"bishop_mob_eg_{mob}"] += sgn * int(c.bishop_mob_eg[mob])

    w_king = board.king(chess.WHITE)
    if w_king is not None:
        r = chess.square_rank(w_king)
        f = chess.square_file(w_king)
        shield = 0
        if r < 7:
            for ff in range(f - 1, f + 2):
                if 0 <= ff <= 7:
                    p = board.piece_at(chess.square(ff, r + 1))
                    if p is not None and p.piece_type == chess.PAWN and p.color == chess.WHITE:
                        shield += 1
        raw["shield_mg"] += shield * c.shield_mg
        raw["shield_eg"] += shield * c.shield_eg

    b_king = board.king(chess.BLACK)
    if b_king is not None:
        r = chess.square_rank(b_king)
        f = chess.square_file(b_king)
        shield = 0
        if r > 0:
            for ff in range(f - 1, f + 2):
                if 0 <= ff <= 7:
                    p = board.piece_at(chess.square(ff, r - 1))
                    if p is not None and p.piece_type == chess.PAWN and p.color == chess.BLACK:
                        shield += 1
        raw["shield_mg"] -= shield * c.shield_mg
        raw["shield_eg"] -= shield * c.shield_eg

    return {
        "mg_base": 0.0,
        "eg_base": 0.0,
        "phase": float(phase),
        "side_sign": 1.0 if board.turn == chess.WHITE else -1.0,
        "raw": raw,
    }


def load_dataset_rows(path: Path, max_positions: int | None) -> tuple[list[str], np.ndarray]:
    fens: list[str] = []
    labels: list[float] = []

    with path.open("r", encoding="utf-8", newline="") as f:
        reader = csv.DictReader(f)
        for row in reader:
            fens.append(row["fen"])
            labels.append(float(row["label"]))
            if max_positions is not None and len(fens) >= max_positions:
                break

    return fens, np.array(labels, dtype=np.float64)


def build_features_from_python(
    fens: list[str],
    labels: np.ndarray,
    constants: EvalConstants,
    param_specs: list[ParamSpec],
) -> dict[str, np.ndarray]:
    n = len(fens)
    p = len(param_specs)

    mg_base = np.zeros(n, dtype=np.float64)
    eg_base = np.zeros(n, dtype=np.float64)
    phase = np.zeros(n, dtype=np.float64)
    side_sign = np.zeros(n, dtype=np.float64)
    mg_terms = np.zeros((n, p), dtype=np.float64)
    eg_terms = np.zeros((n, p), dtype=np.float64)

    raw_keys = raw_keys_from_specs(param_specs)

    kept = 0
    for i, fen in enumerate(fens):
        try:
            board = chess.Board(fen)
        except ValueError:
            continue

        terms = extract_position_terms_python(board, constants, raw_keys)
        mg_base[kept] = terms["mg_base"]
        eg_base[kept] = terms["eg_base"]
        phase[kept] = terms["phase"]
        side_sign[kept] = terms["side_sign"]

        raw = terms["raw"]
        for j, spec in enumerate(param_specs):
            if spec.mg_key is not None:
                mg_terms[kept, j] = float(raw[spec.mg_key])
            if spec.eg_key is not None:
                eg_terms[kept, j] = float(raw[spec.eg_key])

        kept += 1
        if (i + 1) % 5000 == 0:
            print(f"feature_extract(py) processed={i+1} kept={kept}", flush=True)

    if kept == 0:
        raise RuntimeError("no valid positions extracted")

    return {
        "mg_base": mg_base[:kept],
        "eg_base": eg_base[:kept],
        "phase": phase[:kept],
        "side_sign": side_sign[:kept],
        "mg_terms": mg_terms[:kept],
        "eg_terms": eg_terms[:kept],
        "labels": labels[:kept],
    }


def build_features_from_engine(
    fens: list[str],
    labels: np.ndarray,
    constants: EvalConstants,
    param_specs: list[ParamSpec],
    terms_bin: Path,
) -> dict[str, np.ndarray]:
    if not terms_bin.exists():
        raise FileNotFoundError(f"terms binary not found: {terms_bin}")

    n = len(fens)
    p = len(param_specs)

    mg_base = np.zeros(n, dtype=np.float64)
    eg_base = np.zeros(n, dtype=np.float64)
    phase = np.zeros(n, dtype=np.float64)
    side_sign = np.zeros(n, dtype=np.float64)
    mg_terms = np.zeros((n, p), dtype=np.float64)
    eg_terms = np.zeros((n, p), dtype=np.float64)

    with tempfile.NamedTemporaryFile("w", encoding="utf-8", delete=False) as tf:
        for fen in fens:
            tf.write(fen)
            tf.write("\n")
        temp_fens_path = Path(tf.name)

    try:
        with temp_fens_path.open("r", encoding="utf-8") as f_in:
            proc = subprocess.Popen(
                [str(terms_bin)],
                stdin=f_in,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
            )

            assert proc.stdout is not None
            header_line = proc.stdout.readline().strip()
            if not header_line:
                stderr = proc.stderr.read() if proc.stderr else ""
                proc.wait()
                raise RuntimeError(f"eval_terms produced no header. stderr={stderr}")

            header = [x.strip() for x in header_line.split(",")]
            name_to_col = {name: i for i, name in enumerate(header)}

            for required in ("mg_base", "eg_base", "phase", "side_sign"):
                if required not in name_to_col:
                    stderr = proc.stderr.read() if proc.stderr else ""
                    proc.wait()
                    raise RuntimeError(f"eval_terms missing column '{required}'. stderr={stderr}")

            # Required for material/pst decomposition when the engine emits
            # table+count terms instead of direct material/pst columns.
            piece_count_col: dict[str, int] = {}
            table_mg_col: dict[str, int] = {}
            table_eg_col: dict[str, int] = {}
            for piece in PIECE_NAMES:
                count_name = f"count_{piece}"
                mg_name = f"table_{piece}_mg"
                eg_name = f"table_{piece}_eg"
                if count_name not in name_to_col or mg_name not in name_to_col or eg_name not in name_to_col:
                    raise RuntimeError(
                        f"eval_terms missing decomposition columns for '{piece}' "
                        f"(need {count_name}, {mg_name}, {eg_name})"
                    )
                piece_count_col[piece] = name_to_col[count_name]
                table_mg_col[piece] = name_to_col[mg_name]
                table_eg_col[piece] = name_to_col[eg_name]

            mg_col_idx: list[int] = []
            mg_param_idx: list[int] = []
            eg_col_idx: list[int] = []
            eg_param_idx: list[int] = []

            # (param_index, is_pst, count_col, table_col, material_value)
            derived_mg: list[tuple[int, bool, int, int, float]] = []
            derived_eg: list[tuple[int, bool, int, int, float]] = []

            mat_pst_mg_re = re.compile(r"(material|pst)_([a-z]+)_mg$")
            mat_pst_eg_re = re.compile(r"(material|pst)_([a-z]+)_eg$")

            for j, spec in enumerate(param_specs):
                if spec.mg_key is not None:
                    col = name_to_col.get(spec.mg_key)
                    if col is not None:
                        mg_col_idx.append(col)
                        mg_param_idx.append(j)
                    else:
                        m = mat_pst_mg_re.fullmatch(spec.name)
                        if not m:
                            raise RuntimeError(f"eval_terms missing feature column '{spec.mg_key}'")
                        kind, piece = m.groups()
                        piece_idx = PIECE_INDEX.get(piece)
                        if piece_idx is None:
                            raise RuntimeError(f"unknown piece in param: {spec.name}")
                        derived_mg.append(
                            (
                                j,
                                kind == "pst",
                                piece_count_col[piece],
                                table_mg_col[piece],
                                float(constants.mg_value[piece_idx]),
                            )
                        )
                if spec.eg_key is not None:
                    col = name_to_col.get(spec.eg_key)
                    if col is not None:
                        eg_col_idx.append(col)
                        eg_param_idx.append(j)
                    else:
                        m = mat_pst_eg_re.fullmatch(spec.name)
                        if not m:
                            raise RuntimeError(f"eval_terms missing feature column '{spec.eg_key}'")
                        kind, piece = m.groups()
                        piece_idx = PIECE_INDEX.get(piece)
                        if piece_idx is None:
                            raise RuntimeError(f"unknown piece in param: {spec.name}")
                        derived_eg.append(
                            (
                                j,
                                kind == "pst",
                                piece_count_col[piece],
                                table_eg_col[piece],
                                float(constants.eg_value[piece_idx]),
                            )
                        )

            mg_col_idx_arr = np.array(mg_col_idx, dtype=np.int32)
            mg_param_idx_arr = np.array(mg_param_idx, dtype=np.int32)
            eg_col_idx_arr = np.array(eg_col_idx, dtype=np.int32)
            eg_param_idx_arr = np.array(eg_param_idx, dtype=np.int32)

            kept = 0
            for i, line in enumerate(proc.stdout):
                line = line.strip()
                if not line:
                    continue
                if line.startswith("ERR"):
                    continue

                vals = np.fromstring(line, sep=",", dtype=np.float64)
                if vals.size != len(header):
                    continue

                mg_base[kept] = vals[name_to_col["mg_base"]]
                eg_base[kept] = vals[name_to_col["eg_base"]]
                phase[kept] = vals[name_to_col["phase"]]
                side_sign[kept] = vals[name_to_col["side_sign"]]

                if mg_col_idx_arr.size:
                    mg_terms[kept, mg_param_idx_arr] = vals[mg_col_idx_arr]
                if eg_col_idx_arr.size:
                    eg_terms[kept, eg_param_idx_arr] = vals[eg_col_idx_arr]

                for param_idx, is_pst, count_col, table_col, mat_val in derived_mg:
                    material_term = vals[count_col] * mat_val
                    mg_terms[kept, param_idx] = vals[table_col] - material_term if is_pst else material_term

                for param_idx, is_pst, count_col, table_col, mat_val in derived_eg:
                    material_term = vals[count_col] * mat_val
                    eg_terms[kept, param_idx] = vals[table_col] - material_term if is_pst else material_term

                kept += 1
                if (i + 1) % 5000 == 0:
                    print(f"feature_extract(engine) processed={i+1} kept={kept}", flush=True)

            stderr = proc.stderr.read() if proc.stderr is not None else ""
            rc = proc.wait()
            if rc != 0:
                raise RuntimeError(f"eval_terms exited with {rc}. stderr={stderr}")

            if kept == 0:
                raise RuntimeError("eval_terms produced zero usable rows")

            if kept < n:
                print(f"feature_extract(engine) warning: kept={kept} expected={n}")

    finally:
        temp_fens_path.unlink(missing_ok=True)

    return {
        "mg_base": mg_base[:kept],
        "eg_base": eg_base[:kept],
        "phase": phase[:kept],
        "side_sign": side_sign[:kept],
        "mg_terms": mg_terms[:kept],
        "eg_terms": eg_terms[:kept],
        "labels": labels[:kept],
    }


def save_feature_cache(
    cache_path: Path,
    arrays: dict[str, np.ndarray],
    dataset_csv: Path,
    param_specs: list[ParamSpec],
    feature_backend: str,
    terms_bin: str,
) -> None:
    cache_path.parent.mkdir(parents=True, exist_ok=True)
    np.savez_compressed(
        cache_path,
        dataset_csv=str(dataset_csv),
        param_names=np.array([spec.name for spec in param_specs], dtype="U64"),
        feature_backend=feature_backend,
        terms_bin=terms_bin,
        **arrays,
    )


def load_feature_cache(
    cache_path: Path,
    dataset_csv: Path,
    param_specs: list[ParamSpec],
    feature_backend: str,
    terms_bin: str,
) -> dict[str, np.ndarray] | None:
    if not cache_path.exists():
        return None

    data = np.load(cache_path, allow_pickle=False)
    if str(data["dataset_csv"]) != str(dataset_csv):
        return None

    cached_names = [str(x) for x in data["param_names"].tolist()]
    expected_names = [spec.name for spec in param_specs]
    if cached_names != expected_names:
        return None

    if "feature_backend" in data and str(data["feature_backend"]) != feature_backend:
        return None

    if feature_backend == "engine" and "terms_bin" in data:
        if str(data["terms_bin"]) != terms_bin:
            return None

    return {
        "mg_base": data["mg_base"],
        "eg_base": data["eg_base"],
        "phase": data["phase"],
        "side_sign": data["side_sign"],
        "mg_terms": data["mg_terms"],
        "eg_terms": data["eg_terms"],
        "labels": data["labels"],
    }


def sigmoid(x: np.ndarray) -> np.ndarray:
    x = np.clip(x, -60.0, 60.0)
    return 1.0 / (1.0 + np.exp(-x))


def predict_scores(
    mg_base: np.ndarray,
    eg_base: np.ndarray,
    phase: np.ndarray,
    side_sign: np.ndarray,
    mg_terms: np.ndarray,
    eg_terms: np.ndarray,
    scales: np.ndarray,
) -> np.ndarray:
    mg = mg_base + mg_terms @ scales
    eg = eg_base + eg_terms @ scales
    tapered = (mg * phase + eg * (24.0 - phase)) / 24.0
    return side_sign * tapered


def split_indices(n: int, val_frac: float, seed: int) -> tuple[np.ndarray, np.ndarray]:
    rng = np.random.default_rng(seed)
    idx = np.arange(n)
    rng.shuffle(idx)

    val_n = max(1, int(round(n * val_frac)))
    val_idx = idx[:val_n]
    train_idx = idx[val_n:]
    if len(train_idx) == 0:
        raise ValueError("validation fraction too high for dataset size")
    return train_idx, val_idx


def mse_from_scores(scores: np.ndarray, labels: np.ndarray, k: float) -> float:
    p = sigmoid(k * scores)
    err = p - labels
    return float(np.mean(err * err))


def optimize_k_for_scores(
    scores: np.ndarray,
    labels: np.ndarray,
    *,
    k_min: float,
    k_max: float,
    iters: int,
) -> float:
    """1D golden-section search for Texel's sigmoid slope K."""
    if not (0.0 < k_min < k_max):
        raise ValueError("invalid K search bounds")

    phi = (1.0 + math.sqrt(5.0)) / 2.0
    inv_phi = 1.0 / phi

    a = k_min
    b = k_max
    c = b - (b - a) * inv_phi
    d = a + (b - a) * inv_phi
    fc = mse_from_scores(scores, labels, c)
    fd = mse_from_scores(scores, labels, d)

    for _ in range(iters):
        if fc < fd:
            b = d
            d = c
            fd = fc
            c = b - (b - a) * inv_phi
            fc = mse_from_scores(scores, labels, c)
        else:
            a = c
            c = d
            fc = fd
            d = a + (b - a) * inv_phi
            fd = mse_from_scores(scores, labels, d)

    return (a + b) * 0.5


def train_texel(
    arrays: dict[str, np.ndarray],
    param_specs: list[ParamSpec],
    *,
    seed: int,
    val_frac: float,
    iters: int,
    lr: float,
    l2: float,
    k_min: float,
    k_max: float,
    k_search_iters: int,
    log_every: int,
    scale_min: float,
    scale_max: float,
) -> dict[str, Any]:
    labels = arrays["labels"]
    n = len(labels)

    if scale_min > scale_max:
        raise ValueError("scale_min must be <= scale_max")

    train_idx, val_idx = split_indices(n, val_frac, seed)

    mg_base = arrays["mg_base"]
    eg_base = arrays["eg_base"]
    phase = arrays["phase"]
    side_sign = arrays["side_sign"]
    mg_terms = arrays["mg_terms"]
    eg_terms = arrays["eg_terms"]

    scales = np.ones(len(param_specs), dtype=np.float64)

    m_s = np.zeros_like(scales)
    v_s = np.zeros_like(scales)

    beta1 = 0.9
    beta2 = 0.999
    eps = 1e-8

    def dataset_view(idx: np.ndarray) -> tuple[np.ndarray, np.ndarray, np.ndarray, np.ndarray, np.ndarray, np.ndarray, np.ndarray]:
        return (
            mg_base[idx],
            eg_base[idx],
            phase[idx],
            side_sign[idx],
            mg_terms[idx],
            eg_terms[idx],
            labels[idx],
        )

    tr = dataset_view(train_idx)
    va = dataset_view(val_idx)

    initial_scores = predict_scores(tr[0], tr[1], tr[2], tr[3], tr[4], tr[5], scales)
    k_fixed = optimize_k_for_scores(
        initial_scores,
        tr[6],
        k_min=k_min,
        k_max=k_max,
        iters=k_search_iters,
    )

    initial_train_mse = mse_from_scores(initial_scores, tr[6], k_fixed)
    initial_val_scores = predict_scores(va[0], va[1], va[2], va[3], va[4], va[5], scales)
    initial_val_mse = mse_from_scores(initial_val_scores, va[6], k_fixed)

    print(f"k_search done: k={k_fixed:.6f} train_mse={initial_train_mse:.6f}", flush=True)

    best_val_mse = float("inf")
    best_scales = scales.copy()
    best_k = k_fixed
    best_iter = 0

    phase_mix_tr = tr[2][:, None] / 24.0
    eg_mix_tr = (24.0 - tr[2])[:, None] / 24.0
    dscore_ds_tr = tr[3][:, None] * (phase_mix_tr * tr[4] + eg_mix_tr * tr[5])

    for step in range(1, iters + 1):
        scores = predict_scores(tr[0], tr[1], tr[2], tr[3], tr[4], tr[5], scales)
        p = sigmoid(k_fixed * scores)
        err = p - tr[6]

        mse = float(np.mean(err * err))
        reg = float(l2 * np.sum((scales - 1.0) ** 2))
        loss = mse + reg

        common = 2.0 * err * (k_fixed * p * (1.0 - p))
        grad_scales = np.mean(common[:, None] * dscore_ds_tr, axis=0) + 2.0 * l2 * (scales - 1.0)

        m_s = beta1 * m_s + (1.0 - beta1) * grad_scales
        v_s = beta2 * v_s + (1.0 - beta2) * (grad_scales * grad_scales)
        m_s_hat = m_s / (1.0 - beta1**step)
        v_s_hat = v_s / (1.0 - beta2**step)
        scales -= lr * m_s_hat / (np.sqrt(v_s_hat) + eps)

        scales = np.clip(scales, scale_min, scale_max)

        if step % log_every == 0 or step == 1 or step == iters:
            val_scores = predict_scores(va[0], va[1], va[2], va[3], va[4], va[5], scales)
            val_mse = mse_from_scores(val_scores, va[6], k_fixed)
            print(
                f"iter={step} loss={loss:.6f} train_mse={mse:.6f} val_mse={val_mse:.6f} k={k_fixed:.6f}",
                flush=True,
            )
            if val_mse < best_val_mse:
                best_val_mse = val_mse
                best_scales = scales.copy()
                best_k = k_fixed
                best_iter = step

    final_train_scores = predict_scores(tr[0], tr[1], tr[2], tr[3], tr[4], tr[5], best_scales)
    final_val_scores = predict_scores(va[0], va[1], va[2], va[3], va[4], va[5], best_scales)

    final_train_mse = mse_from_scores(final_train_scores, tr[6], best_k)
    final_val_mse = mse_from_scores(final_val_scores, va[6], best_k)

    return {
        "num_positions": int(n),
        "num_train": int(len(train_idx)),
        "num_val": int(len(val_idx)),
        "num_params": int(len(param_specs)),
        "initial_train_mse": initial_train_mse,
        "initial_val_mse": initial_val_mse,
        "best_train_mse": final_train_mse,
        "best_val_mse": final_val_mse,
        "best_iter": int(best_iter),
        "k_method": "golden_section_search_baseline_static_eval",
        "k_search": {
            "k_min": k_min,
            "k_max": k_max,
            "iters": k_search_iters,
        },
        "k": best_k,
        "scales": {spec.name: float(best_scales[i]) for i, spec in enumerate(param_specs)},
    }


def main() -> None:
    parser = argparse.ArgumentParser(description="Texel tune eval feature scales")
    parser.add_argument("--dataset", required=True, help="CSV from build_texel_dataset.py")
    parser.add_argument("--eval-c", default="chess/engine/src/eval.c")
    parser.add_argument("--out", required=True, help="Output JSON path")
    parser.add_argument("--feature-cache", default=None, help="Optional .npz cache path")
    parser.add_argument("--feature-backend", choices=["engine", "python"], default="engine")
    parser.add_argument("--terms-bin", default="chess/engine/build/eval_terms")
    parser.add_argument("--max-positions", type=int, default=None)
    parser.add_argument("--iters", type=int, default=400)
    parser.add_argument("--lr", type=float, default=0.02)
    parser.add_argument("--l2", type=float, default=1e-4)
    parser.add_argument("--k-min", type=float, default=1e-4)
    parser.add_argument("--k-max", type=float, default=0.02)
    parser.add_argument("--k-search-iters", type=int, default=60)
    parser.add_argument("--val-frac", type=float, default=0.1)
    parser.add_argument("--seed", type=int, default=42)
    parser.add_argument("--log-every", type=int, default=20)
    parser.add_argument("--scale-min", type=float, default=0.0)
    parser.add_argument("--scale-max", type=float, default=3.0)
    args = parser.parse_args()

    dataset = Path(args.dataset)
    eval_c = Path(args.eval_c)
    out = Path(args.out)
    out.parent.mkdir(parents=True, exist_ok=True)

    constants = parse_eval_constants(eval_c)
    param_specs = build_param_specs(constants)

    cache_path = Path(args.feature_cache) if args.feature_cache else None
    arrays = None
    if cache_path is not None:
        arrays = load_feature_cache(
            cache_path,
            dataset,
            param_specs,
            args.feature_backend,
            str(Path(args.terms_bin)),
        )
        if arrays is not None:
            print(f"Loaded feature cache: {cache_path}")

    if arrays is None:
        fens, labels = load_dataset_rows(dataset, args.max_positions)
        print(f"Loaded dataset rows: {len(fens)}")

        if args.feature_backend == "engine":
            arrays = build_features_from_engine(
                fens,
                labels,
                constants,
                param_specs,
                Path(args.terms_bin),
            )
        else:
            arrays = build_features_from_python(
                fens,
                labels,
                constants,
                param_specs,
            )

        if cache_path is not None:
            save_feature_cache(
                cache_path,
                arrays,
                dataset,
                param_specs,
                args.feature_backend,
                str(Path(args.terms_bin)),
            )
            print(f"Saved feature cache: {cache_path}")

    result = train_texel(
        arrays,
        param_specs,
        seed=args.seed,
        val_frac=args.val_frac,
        iters=args.iters,
        lr=args.lr,
        l2=args.l2,
        k_min=args.k_min,
        k_max=args.k_max,
        k_search_iters=args.k_search_iters,
        log_every=args.log_every,
        scale_min=args.scale_min,
        scale_max=args.scale_max,
    )

    result["dataset"] = str(dataset)
    result["eval_c"] = str(eval_c)
    result["param_order"] = [spec.name for spec in param_specs]
    result["base_values"] = build_base_values(constants, param_specs)
    result["base_tables"] = {
        "mg_table": [[int(v) for v in row] for row in constants.mg_table.tolist()],
        "eg_table": [[int(v) for v in row] for row in constants.eg_table.tolist()],
    }
    result["table_decomp"] = build_table_decomp(constants)
    result["config"] = {
        "iters": args.iters,
        "lr": args.lr,
        "l2": args.l2,
        "k_min": args.k_min,
        "k_max": args.k_max,
        "k_search_iters": args.k_search_iters,
        "val_frac": args.val_frac,
        "seed": args.seed,
        "max_positions": args.max_positions,
        "scale_min": args.scale_min,
        "scale_max": args.scale_max,
        "feature_backend": args.feature_backend,
        "terms_bin": str(Path(args.terms_bin)),
    }

    out.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(f"Wrote tuning result: {out}")


if __name__ == "__main__":
    main()
