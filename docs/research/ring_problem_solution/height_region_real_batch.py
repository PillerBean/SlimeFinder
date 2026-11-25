#!/usr/bin/env python3
"""Batch real-biome checks for height-region search."""
from __future__ import annotations

import csv
import sys
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import height_region_prototype as proto  # noqa: E402


def region_info(mask):
    comps = proto.connected_components(mask)
    comps.sort(key=len, reverse=True)
    return int(mask.sum()), len(comps), (len(comps[0]) if comps else 0)


def main():
    files = sorted((HERE / "biome_cases").glob("*.tsv"))
    rows = []
    for path in files:
        w = np.loadtxt(path, comments="#", delimiter="\t")
        U = proto.fft_conv_same(w, proto.disk_kernel(proto.R))
        uc = U[proto.CZ0:proto.CZ1, proto.CX0:proto.CX1]
        maps = []
        best = -1.0
        best_h = -1
        best_yx = None
        for h in range(25):
            m = proto.fft_conv_same(w, proto.annulus_kernel(h))
            mc = m[proto.CZ0:proto.CZ1, proto.CX0:proto.CX1]
            maps.append(mc)
            v = float(mc.max())
            if v > best + 1e-9:
                best = v
                best_h = h
                yy, xx = np.unravel_index(int(np.argmax(mc)), mc.shape)
                best_yx = (yy + proto.CZ0, xx + proto.CX0)
        y0, x0 = best_yx
        rank = int(np.sum(uc > U[y0, x0] + 1e-12)) + 1
        m = maps[best_h]
        best_mask = m >= best - 1e-9
        exact = region_info(best_mask)
        tol001 = region_info(m >= best * (1 - 0.001))
        tol005 = region_info(m >= best * (1 - 0.005))
        tol01 = region_info(m >= best * (1 - 0.01))
        hits = {}
        for K in (10, 20, 50):
            cand = set()
            for hh in (0, 12, 24):
                flat = maps[hh].ravel()
                for idx in np.argpartition(flat, -K)[-K:]:
                    yy, xx = divmod(int(idx), maps[hh].shape[1])
                    cand.add((yy + proto.CZ0, xx + proto.CX0))
            hits[K] = int(any(best_mask[yy - proto.CZ0, xx - proto.CX0] for (yy, xx) in cand))
        row = {
            "case": path.stem,
            "min": w.min(), "max": w.max(), "mean": w.mean(),
            "unique3": len(np.unique(np.round(w, 3))),
            "best": best, "h": best_h,
            "rank": rank,
            "exact_n": exact[0], "exact_comps": exact[1], "exact_largest": exact[2],
            "tol01_n": tol001[0], "tol01_comps": tol001[1], "tol01_largest": tol001[2],
            "tol05_n": tol005[0], "tol05_comps": tol005[1], "tol05_largest": tol005[2],
            "tol1_n": tol01[0], "tol1_comps": tol01[1], "tol1_largest": tol01[2],
            "hit10": hits[10], "hit20": hits[20], "hit50": hits[50],
        }
        rows.append(row)
        print(f"{path.stem:18s} w={w.min():.3f}..{w.max():.3f} mean={w.mean():.4f} "
              f"uniq3={row['unique3']:3d} best={best:9.2f} h={best_h:2d} rank={rank:5d} "
              f"exact={exact} tol0.1%={tol001} tol0.5%={tol005} tol1%={tol01} "
              f"hit={hits}")

    out = HERE / "height_region_real_batch.csv"
    with out.open("w", encoding="utf-8", newline="") as f:
        wr = csv.DictWriter(f, fieldnames=list(rows[0]))
        wr.writeheader()
        wr.writerows(rows)
    print("=" * 78)
    print(f"cases={len(rows)}")
    print("heights", sorted((r["h"], sum(1 for x in rows if x["h"] == r["h"])) for r in rows))
    for K in (10, 20, 50):
        print(f"union3 Top-{K}: {sum(r['hit'+str(K)] for r in rows)}/{len(rows)}")
    print("rank max", max(r["rank"] for r in rows))
    print("largest exact region max", max(r["exact_largest"] for r in rows))
    print("largest tol0.5% region max", max(r["tol05_largest"] for r in rows))
    print("csv", out)


if __name__ == "__main__":
    main()


