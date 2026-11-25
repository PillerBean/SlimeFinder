#!/usr/bin/env python3
"""Test height<=24 theorem and multi-height candidate recall."""
from __future__ import annotations

import csv
import sys
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

import height_region_prototype as proto  # noqa: E402
import height_region_random as rnd  # noqa: E402


def main():
    count = int(sys.argv[1]) if len(sys.argv) > 1 else 30
    rng = np.random.default_rng(20261125)
    rows = []
    for case in range(count):
        w = rnd.random_weights(rng)
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
                best, best_h = v, h
                yy, xx = np.unravel_index(int(np.argmax(mc)), mc.shape)
                best_yx = (yy + proto.CZ0, xx + proto.CX0)
        y0, x0 = best_yx

        # Single map rank (best height map).
        m = maps[best_h]
        rank_best_h = int(np.sum(m > m[y0 - proto.CZ0, x0 - proto.CX0])) + 1

        # Union topK over all 25 height maps (or selected heights).
        def union_hit(ks):
            cand = set()
            for hh in range(25):
                flat = maps[hh].ravel()
                top = np.argpartition(flat, -ks)[-ks:]
                for idx in top:
                    yy, xx = divmod(int(idx), maps[hh].shape[1])
                    cand.add((yy + proto.CZ0, xx + proto.CX0))
            return (y0, x0) in cand, len(cand)

        hit10, n10 = union_hit(10)
        hit20, n20 = union_hit(20)
        hit50, n50 = union_hit(50)
        hit100, n100 = union_hit(100)

        # Union of 0/12/24 topK only.
        cand3 = set()
        for hh in (0, 12, 24):
            flat = maps[hh].ravel()
            for idx in np.argpartition(flat, -50)[-50:]:
                yy, xx = divmod(int(idx), maps[hh].shape[1])
                cand3.add((yy + proto.CZ0, xx + proto.CX0))
        hit3 = (y0, x0) in cand3

        row = {
            "case": case, "best": best, "h": best_h,
            "rank_best_h": rank_best_h,
            "hit10": int(hit10), "hit20": int(hit20), "hit50": int(hit50),
            "hit100": int(hit100), "hit3height": int(hit3),
            "n10": n10, "n20": n20, "n50": n50, "n100": n100, "n3": len(cand3),
        }
        rows.append(row)
        print(f"case={case:03d} best={best:9.2f} h={best_h:2d} rank_bestH={rank_best_h:4d} "
              f"union10={int(hit10)} (n={n10}) union20={int(hit20)} (n={n20}) "
              f"union50={int(hit50)} (n={n50}) union3={int(hit3)} (n={len(cand3)})")

    path = HERE / "height_region_union.csv"
    with path.open("w", encoding="utf-8", newline="") as f:
        wr = csv.DictWriter(f, fieldnames=list(rows[0]))
        wr.writeheader()
        wr.writerows(rows)

    n = len(rows)
    print("=" * 78)
    for key in ("hit10", "hit20", "hit50", "hit100", "hit3height"):
        hits = sum(r[key] for r in rows)
        print(f"{key}: {hits}/{n} = {hits*100.0/n:.1f}%")
    print(f"avg candidates union10={np.mean([r['n10'] for r in rows]):.0f}, "
          f"union20={np.mean([r['n20'] for r in rows]):.0f}, "
          f"union50={np.mean([r['n50'] for r in rows]):.0f}, "
          f"union3={np.mean([r['n3'] for r in rows]):.0f}")
    print(f"rank_bestH max={max(r['rank_best_h'] for r in rows)}")
    print(f"csv: {path}")


if __name__ == "__main__":
    main()
