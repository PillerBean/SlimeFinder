#!/usr/bin/env python3
"""Run the height-region prototype on a real Java biome weight dump."""
from __future__ import annotations

import sys
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

import height_region_prototype as proto  # noqa: E402


def main():
    path = HERE / "biome_weights_12345.tsv"
    w = np.loadtxt(path, comments="#", delimiter="\t")
    print(f"loaded {path.name}: shape={w.shape}, min={w.min():.4f}, max={w.max():.4f}, "
          f"mean={w.mean():.4f}, unique={len(np.unique(np.round(w, 3)))}")

    U = proto.fft_conv_same(w, proto.disk_kernel(proto.R))
    uc = U[proto.CZ0:proto.CZ1, proto.CX0:proto.CX1]

    best = -1.0
    best_h = -1
    best_yx = None
    maps = []
    for h in range(25):
        m = proto.fft_conv_same(w, proto.annulus_kernel(h))
        mc = m[proto.CZ0:proto.CZ1, proto.CX0:proto.CX1]
        maps.append(mc)
        v = float(mc.max())
        print(f"h={h:2d} max={v:.3f}")
        if v > best + 1e-9:
            best = v
            best_h = h
            yy, xx = np.unravel_index(int(np.argmax(mc)), mc.shape)
            best_yx = (yy + proto.CZ0, xx + proto.CX0)

    y0, x0 = best_yx
    rank = int(np.sum(uc > U[y0, x0] + 1e-12)) + 1
    print(f"exact best={best:.3f} h={best_h} center(y,x)={best_yx} disk rank={rank}/{uc.size}")

    # Region at best height.
    m = maps[best_h]
    mask = m >= best - 1e-9
    comps = proto.connected_components(mask)
    comps.sort(key=len, reverse=True)
    print(f"max centers={int(mask.sum())}, components={len(comps)}, "
          f"largest={len(comps[0]) if comps else 0}")
    if comps:
        c = comps[0]
        ys = [p[0] + proto.CZ0 for p in c]
        xs = [p[1] + proto.CX0 for p in c]
        print(f"largest bbox y={min(ys)}..{max(ys)} x={min(xs)}..{max(xs)}")

    # Candidate recall: union of top-K at h=0/12/24.
    for K in (10, 20, 50, 100):
        cand = set()
        for hh in (0, 12, 24):
            flat = maps[hh].ravel()
            for idx in np.argpartition(flat, -K)[-K:]:
                yy, xx = divmod(int(idx), maps[hh].shape[1])
                cand.add((yy + proto.CZ0, xx + proto.CX0))
        print(f"union3 K={K}: candidates={len(cand)} hit={int((y0, x0) in cand)}")


if __name__ == "__main__":
    main()
