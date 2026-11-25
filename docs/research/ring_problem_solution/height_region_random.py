#!/usr/bin/env python3
"""Recall/region statistics for height-dependent biome-weighted area.

For each random biome map:
  - exact maximum over all centers and H=0..128 (FFT convolution)
  - rank of the exact best center(s) in the height-independent disk upper bound
  - Top-K recall for K values
  - best-height distribution and max-region size

Small 384x384 research maps; not full world.
"""
from __future__ import annotations

import csv
import json
import sys
import time
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

import height_region_prototype as proto  # noqa: E402


def random_weights(rng: np.random.Generator) -> np.ndarray:
    n = proto.N
    w = np.full((n, n), rng.uniform(0.05, 0.20), dtype=np.float64)
    # 4..10 broad rectangular patches, like coarse biome regions.
    for _ in range(int(rng.integers(4, 11))):
        h = int(rng.integers(40, 180))
        wdt = int(rng.integers(40, 180))
        y0 = int(rng.integers(0, n - h))
        x0 = int(rng.integers(0, n - wdt))
        val = float(rng.uniform(0.25, 1.0))
        w[y0:y0 + h, x0:x0 + wdt] = val
    # Soft variation, not block-random.
    w += rng.normal(0.0, 0.01, size=(n, n))
    return np.clip(w, 0.0, 1.0)


def exact_best(w: np.ndarray):
    best = -1.0
    best_hs = []
    best_yx = None
    for h in range(proto.R + 1):
        m = proto.fft_conv_same(w, proto.annulus_kernel(h))
        mc = m[proto.CZ0:proto.CZ1, proto.CX0:proto.CX1]
        v = float(mc.max())
        if v > best + 1e-9:
            best = v
            best_hs = [h]
            yy, xx = np.unravel_index(int(np.argmax(mc)), mc.shape)
            best_yx = (yy + proto.CZ0, xx + proto.CX0)
        elif abs(v - best) <= 1e-9:
            best_hs.append(h)
    return best, best_hs, best_yx


def region_stats(w: np.ndarray, h: int, best: float):
    m = proto.fft_conv_same(w, proto.annulus_kernel(h))
    mc = m[proto.CZ0:proto.CZ1, proto.CX0:proto.CX1]
    mask = mc >= best - 1e-9
    comps = proto.connected_components(mask)
    comps.sort(key=len, reverse=True)
    return len(mask.nonzero()[0]), len(comps), (len(comps[0]) if comps else 0)


def main():
    count = int(sys.argv[1]) if len(sys.argv) > 1 else 30
    rng = np.random.default_rng(20261125)
    rows = []
    t0 = time.time()
    for case in range(count):
        w = random_weights(rng)
        U = proto.fft_conv_same(w, proto.disk_kernel(proto.R))
        uc = U[proto.CZ0:proto.CZ1, proto.CX0:proto.CX1]
        best, best_hs, best_yx = exact_best(w)
        y0, x0 = best_yx
        u0 = float(U[y0, x0])
        rank = int(np.sum(uc > u0 + 1e-12)) + 1  # 1-based
        n_max, n_comp, largest = region_stats(w, best_hs[0], best)
        row = {
            "case": case,
            "best": best,
            "h": best_hs[0],
            "n_best_h": len(best_hs),
            "best_hs": ",".join(map(str, best_hs[:8])),
            "rank_1based": rank,
            "U_best": u0,
            "U_gap": u0 - best,
            "n_max_centers": n_max,
            "n_components": n_comp,
            "largest_region": largest,
        }
        rows.append(row)
        print(
            f"case={case:03d} best={best:.2f} h={best_hs[0]:3d} nH={len(best_hs):2d} "
            f"rank={rank:5d} U-gap={u0-best:7.2f} maxcenters={n_max:5d} "
            f"comps={n_comp:3d} largest={largest:5d}"
        )
    out_csv = HERE / "height_region_random.csv"
    with out_csv.open("w", encoding="utf-8", newline="") as f:
        wr = csv.DictWriter(f, fieldnames=list(rows[0]))
        wr.writeheader()
        wr.writerows(rows)
    ranks = np.array([r["rank_1based"] for r in rows], dtype=float)
    hs = np.array([r["h"] for r in rows], dtype=int)
    print("=" * 78)
    print(f"cases={count}, elapsed={time.time()-t0:.1f}s")
    print(f"rank min/median/p90/max = {ranks.min():.0f}/{np.median(ranks):.0f}/"
          f"{np.percentile(ranks,90):.0f}/{ranks.max():.0f}")
    for K in (10, 20, 50, 100, 200, 500, 1000, 2000):
        hit = int(np.sum(ranks <= K))
        print(f"Top-{K:4d}: {hit:3d}/{count} = {hit*100.0/count:5.1f}%")
    print(f"height <=24: {int(np.sum(hs <= 24))}/{count}; "
          f"height >24: {int(np.sum(hs > 24))}/{count}")
    print(f"height unique: {len(set(hs.tolist()))} values; list={sorted(set(hs.tolist()))}")
    print(f"multi-height max: {sum(1 for r in rows if r['n_best_h']>1)}/{count}")
    print(f"single-center max: {sum(1 for r in rows if r['n_max_centers']==1)}/{count}")
    print(f"largest region == all: {sum(1 for r in rows if r['largest_region']==r['n_max_centers'])}/{count}")
    print(f"csv: {out_csv}")


if __name__ == "__main__":
    main()
