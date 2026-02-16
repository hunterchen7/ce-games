#!/usr/bin/env python3
"""Texel tuning for chess/engine/src/eval.c feature groups.

This tuner keeps material + PST fixed and tunes scalar multipliers for
hand-crafted evaluation terms.
"""

from __future__ import annotations

import argparse
import csv
import json
import math
import re
from dataclasses import dataclass
from pathlib import Path
from typing import Any

import chess
import numpy as np


PARAM_SPECS = [
    {"name": "bishop_pair_mg", "mg_key": "bishop_pair_mg", "eg_key": None},
    {"name": "bishop_pair_eg", "mg_key": None, "eg_key": "bishop_pair_eg"},
    {"name": "tempo_mg", "mg_key": "tempo_mg", "eg_key": None},
    {"name": "tempo_eg", "mg_key": None, "eg_key": "tempo_eg"},
    {"name": "doubled_mg", "mg_key": "doubled_mg", "eg_key": None},
    {"name": "doubled_eg", "mg_key": None, "eg_key": "doubled_eg"},
    {"name": "isolated_mg", "mg_key": "isolated_mg", "eg_key": None},
    {"name": "isolated_eg", "mg_key": None, "eg_key": "isolated_eg"},
    {"name": "connected", "mg_key": "connected", "eg_key": "connected"},
    {"name": "passed_mg", "mg_key": "passed_mg", "eg_key": None},
    {"name": "passed_eg", "mg_key": None, "eg_key": "passed_eg"},
    {"name": "rook_open_mg", "mg_key": "rook_open_mg", "eg_key": None},
    {"name": "rook_open_eg", "mg_key": None, "eg_key": "rook_open_eg"},
    {"name": "rook_semiopen_mg", "mg_key": "rook_semiopen_mg", "eg_key": None},
    {"name": "rook_semiopen_eg", "mg_key": None, "eg_key": "rook_semiopen_eg"},
    {"name": "shield_mg", "mg_key": "shield_mg", "eg_key": None},
    {"name": "knight_mob_mg", "mg_key": "knight_mob_mg", "eg_key": None},
    {"name": "knight_mob_eg", "mg_key": None, "eg_key": "knight_mob_eg"},
    {"name": "bishop_mob_mg", "mg_key": "bishop_mob_mg", "eg_key": None},
    {"name": "bishop_mob_eg", "mg_key": None, "eg_key": "bishop_mob_eg"},
]


BASE_VALUE_ATTRS = {
    "bishop_pair_mg": "bishop_pair_mg",
    "bishop_pair_eg": "bishop_pair_eg",
    "tempo_mg": "tempo_mg",
    "tempo_eg": "tempo_eg",
    "doubled_mg": "doubled_mg",
    "doubled_eg": "doubled_eg",
    "isolated_mg": "isolated_mg",
    "isolated_eg": "isolated_eg",
    "connected": "connected_bonus",
    "passed_mg": "passed_mg",
    "passed_eg": "passed_eg",
    "rook_open_mg": "rook_open_mg",
    "rook_open_eg": "rook_open_eg",
    "rook_semiopen_mg": "rook_semiopen_mg",
    "rook_semiopen_eg": "rook_semiopen_eg",
    "shield_mg": "shield_mg",
    "knight_mob_mg": "knight_mob_mg",
    "knight_mob_eg": "knight_mob_eg",
    "bishop_mob_mg": "bishop_mob_mg",
    "bishop_mob_eg": "bishop_mob_eg",
}


@dataclass
class EvalConstants:
    mg_table: np.ndarray
    eg_table: np.ndarray
    phase_weight: np.ndarray
    bishop_pair_mg: int
    bishop_pair_eg: int
    tempo_mg: int
    tempo_eg: int
    doubled_mg: int
    doubled_eg: int
    isolated_mg: int
    isolated_eg: int
    connected_bonus: np.ndarray
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

    phase_weight = parse_named_array_1d(text, "phase_weight", expected_count=6)

    return EvalConstants(
        mg_table=mg_vals.reshape(6, 64),
        eg_table=eg_vals.reshape(6, 64),
        phase_weight=phase_weight,
        bishop_pair_mg=parse_define(text, "BISHOP_PAIR_MG"),
        bishop_pair_eg=parse_define(text, "BISHOP_PAIR_EG"),
        tempo_mg=parse_define(text, "TEMPO_MG"),
        tempo_eg=parse_define(text, "TEMPO_EG"),
        doubled_mg=parse_define(text, "DOUBLED_MG"),
        doubled_eg=parse_define(text, "DOUBLED_EG"),
        isolated_mg=parse_define(text, "ISOLATED_MG"),
        isolated_eg=parse_define(text, "ISOLATED_EG"),
        connected_bonus=parse_named_array_1d(text, "connected_bonus", expected_count=7),
        passed_mg=parse_named_array_1d(text, "passed_mg", expected_count=6),
        passed_eg=parse_named_array_1d(text, "passed_eg", expected_count=6),
        rook_open_mg=parse_define(text, "ROOK_OPEN_MG"),
        rook_open_eg=parse_define(text, "ROOK_OPEN_EG"),
        rook_semiopen_mg=parse_define(text, "ROOK_SEMIOPEN_MG"),
        rook_semiopen_eg=parse_define(text, "ROOK_SEMIOPEN_EG"),
        shield_mg=parse_define(text, "SHIELD_MG"),
        shield_eg=parse_define(text, "SHIELD_EG"),
        knight_mob_mg=parse_named_array_1d(text, "knight_mob_mg", expected_count=9),
        knight_mob_eg=parse_named_array_1d(text, "knight_mob_eg", expected_count=9),
        bishop_mob_mg=parse_named_array_1d(text, "bishop_mob_mg", expected_count=14),
        bishop_mob_eg=parse_named_array_1d(text, "bishop_mob_eg", expected_count=14),
    )


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

    if by_color == chess.WHITE:
        attacker_rank = rank - 1
    else:
        attacker_rank = rank + 1

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


def extract_position_terms(board: chess.Board, c: EvalConstants) -> dict[str, Any]:
    mg_base = 0
    eg_base = 0
    phase = 0

    for square, piece in board.piece_map().items():
        idx = piece.piece_type - 1
        sq64 = square_to_sq64(square)
        pst_sq = sq64 if piece.color == chess.WHITE else (sq64 ^ 56)
        sgn = piece_sign(piece.color)

        mg_base += sgn * int(c.mg_table[idx, pst_sq])
        eg_base += sgn * int(c.eg_table[idx, pst_sq])
        phase += int(c.phase_weight[idx])

    if phase < 0:
        phase = 0
    elif phase > 24:
        phase = 24

    raw = {
        "bishop_pair_mg": 0,
        "bishop_pair_eg": 0,
        "tempo_mg": 0,
        "tempo_eg": 0,
        "doubled_mg": 0,
        "doubled_eg": 0,
        "isolated_mg": 0,
        "isolated_eg": 0,
        "connected": 0,
        "passed_mg": 0,
        "passed_eg": 0,
        "rook_open_mg": 0,
        "rook_open_eg": 0,
        "rook_semiopen_mg": 0,
        "rook_semiopen_eg": 0,
        "shield_mg": 0,
        "knight_mob_mg": 0,
        "knight_mob_eg": 0,
        "bishop_mob_mg": 0,
        "bishop_mob_eg": 0,
    }

    w_bishops = len(board.pieces(chess.BISHOP, chess.WHITE))
    b_bishops = len(board.pieces(chess.BISHOP, chess.BLACK))
    if w_bishops >= 2:
        raw["bishop_pair_mg"] += c.bishop_pair_mg
        raw["bishop_pair_eg"] += c.bishop_pair_eg
    if b_bishops >= 2:
        raw["bishop_pair_mg"] -= c.bishop_pair_mg
        raw["bishop_pair_eg"] -= c.bishop_pair_eg

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
        rank = chess.square_rank(sq)
        rel_rank = rank

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
            if ri < len(c.connected_bonus):
                raw["connected"] += int(c.connected_bonus[ri])

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
                raw["passed_mg"] += int(c.passed_mg[ri])
                raw["passed_eg"] += int(c.passed_eg[ri])

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
            if ri < len(c.connected_bonus):
                raw["connected"] -= int(c.connected_bonus[ri])

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
                raw["passed_mg"] -= int(c.passed_mg[ri])
                raw["passed_eg"] -= int(c.passed_eg[ri])

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
            mob = min(mob, 8)
            raw["knight_mob_mg"] += sgn * int(c.knight_mob_mg[mob])
            raw["knight_mob_eg"] += sgn * int(c.knight_mob_eg[mob])

        bishop_dirs = [(-1, -1), (-1, 1), (1, -1), (1, 1)]
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
            mob = min(mob, 13)
            raw["bishop_mob_mg"] += sgn * int(c.bishop_mob_mg[mob])
            raw["bishop_mob_eg"] += sgn * int(c.bishop_mob_eg[mob])

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

    return {
        "mg_base": float(mg_base),
        "eg_base": float(eg_base),
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


def build_features(
    fens: list[str],
    labels: np.ndarray,
    constants: EvalConstants,
) -> dict[str, np.ndarray]:
    n = len(fens)
    p = len(PARAM_SPECS)

    mg_base = np.zeros(n, dtype=np.float64)
    eg_base = np.zeros(n, dtype=np.float64)
    phase = np.zeros(n, dtype=np.float64)
    side_sign = np.zeros(n, dtype=np.float64)
    mg_terms = np.zeros((n, p), dtype=np.float64)
    eg_terms = np.zeros((n, p), dtype=np.float64)

    kept = 0
    for i, fen in enumerate(fens):
        try:
            board = chess.Board(fen)
        except ValueError:
            continue

        terms = extract_position_terms(board, constants)
        mg_base[kept] = terms["mg_base"]
        eg_base[kept] = terms["eg_base"]
        phase[kept] = terms["phase"]
        side_sign[kept] = terms["side_sign"]

        raw = terms["raw"]
        for j, spec in enumerate(PARAM_SPECS):
            mg_key = spec["mg_key"]
            eg_key = spec["eg_key"]
            if mg_key is not None:
                mg_terms[kept, j] = float(raw[mg_key])
            if eg_key is not None:
                eg_terms[kept, j] = float(raw[eg_key])

        kept += 1
        if (i + 1) % 5000 == 0:
            print(f"feature_extract processed={i+1} kept={kept}", flush=True)

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


def save_feature_cache(cache_path: Path, arrays: dict[str, np.ndarray], dataset_csv: Path) -> None:
    cache_path.parent.mkdir(parents=True, exist_ok=True)
    np.savez_compressed(
        cache_path,
        dataset_csv=str(dataset_csv),
        param_names=np.array([spec["name"] for spec in PARAM_SPECS], dtype="U32"),
        **arrays,
    )


def load_feature_cache(cache_path: Path, dataset_csv: Path) -> dict[str, np.ndarray] | None:
    if not cache_path.exists():
        return None

    data = np.load(cache_path, allow_pickle=False)
    if str(data["dataset_csv"]) != str(dataset_csv):
        return None

    cached_names = [str(x) for x in data["param_names"].tolist()]
    expected_names = [spec["name"] for spec in PARAM_SPECS]
    if cached_names != expected_names:
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
) -> dict[str, Any]:
    labels = arrays["labels"]
    n = len(labels)

    train_idx, val_idx = split_indices(n, val_frac, seed)

    mg_base = arrays["mg_base"]
    eg_base = arrays["eg_base"]
    phase = arrays["phase"]
    side_sign = arrays["side_sign"]
    mg_terms = arrays["mg_terms"]
    eg_terms = arrays["eg_terms"]

    scales = np.ones(len(PARAM_SPECS), dtype=np.float64)

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
        k = k_fixed
        scores = predict_scores(tr[0], tr[1], tr[2], tr[3], tr[4], tr[5], scales)
        p = sigmoid(k * scores)
        err = p - tr[6]

        mse = float(np.mean(err * err))
        reg = float(l2 * np.sum((scales - 1.0) ** 2))
        loss = mse + reg

        common = 2.0 * err * (k * p * (1.0 - p))
        grad_scales = np.mean(common[:, None] * dscore_ds_tr, axis=0) + 2.0 * l2 * (scales - 1.0)

        m_s = beta1 * m_s + (1.0 - beta1) * grad_scales
        v_s = beta2 * v_s + (1.0 - beta2) * (grad_scales * grad_scales)
        m_s_hat = m_s / (1.0 - beta1**step)
        v_s_hat = v_s / (1.0 - beta2**step)
        scales -= lr * m_s_hat / (np.sqrt(v_s_hat) + eps)

        scales = np.clip(scales, 0.0, 3.0)

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

    result = {
        "num_positions": int(n),
        "num_train": int(len(train_idx)),
        "num_val": int(len(val_idx)),
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
        "scales": {spec["name"]: float(best_scales[i]) for i, spec in enumerate(PARAM_SPECS)},
    }
    return result


def main() -> None:
    parser = argparse.ArgumentParser(description="Texel tune eval feature scalars")
    parser.add_argument("--dataset", required=True, help="CSV from build_texel_dataset.py")
    parser.add_argument("--eval-c", default="chess/engine/src/eval.c")
    parser.add_argument("--out", required=True, help="Output JSON path")
    parser.add_argument("--feature-cache", default=None, help="Optional .npz cache path")
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
    args = parser.parse_args()

    dataset = Path(args.dataset)
    eval_c = Path(args.eval_c)
    out = Path(args.out)
    out.parent.mkdir(parents=True, exist_ok=True)

    constants = parse_eval_constants(eval_c)

    cache_path = Path(args.feature_cache) if args.feature_cache else None
    arrays = None
    if cache_path is not None:
        arrays = load_feature_cache(cache_path, dataset)
        if arrays is not None:
            print(f"Loaded feature cache: {cache_path}")

    if arrays is None:
        fens, labels = load_dataset_rows(dataset, args.max_positions)
        print(f"Loaded dataset rows: {len(fens)}")
        arrays = build_features(fens, labels, constants)
        if cache_path is not None:
            save_feature_cache(cache_path, arrays, dataset)
            print(f"Saved feature cache: {cache_path}")

    result = train_texel(
        arrays,
        seed=args.seed,
        val_frac=args.val_frac,
        iters=args.iters,
        lr=args.lr,
        l2=args.l2,
        k_min=args.k_min,
        k_max=args.k_max,
        k_search_iters=args.k_search_iters,
        log_every=args.log_every,
    )

    result["dataset"] = str(dataset)
    result["eval_c"] = str(eval_c)
    result["param_order"] = [spec["name"] for spec in PARAM_SPECS]
    base_values: dict[str, Any] = {}
    for param_name, attr_name in BASE_VALUE_ATTRS.items():
        v = getattr(constants, attr_name)
        if isinstance(v, np.ndarray):
            base_values[param_name] = [int(x) for x in v.tolist()]
        else:
            base_values[param_name] = int(v)
    result["base_values"] = base_values
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
    }

    out.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(f"Wrote tuning result: {out}")


if __name__ == "__main__":
    main()
