#!/usr/bin/env python3
"""Analyze candidate recall for the height-region prototype."""
from __future__ import annotations

import sys
from pathlib import Path
import numpy as np

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

import height_region_prototype as proto  # noqa: E402


def run_case(name, w, Ks=(100, 500, 2000, 5000)):
    print("=" * 78)
    print(name)
    U = proto.fft_conv_same(w, proto.disk_kernel(proto.R))
    uc = U[proto.CZ0:proto.CZ1, proto.CX0:proto.CX1]
    print(f"U center max={uc.max():.3f}, mean={uc.mean():.3f}")

    best = -1.0
    best_h = -1
    best_yx = None
    maps = []
    for h in range(proto.R + 1):
        m = proto.fft_conv_same(w, proto.annulus_kernel(h))
        m = m[proto.CZ0:proto.CZ1, proto.CX0:proto.CX1]
        maps.append(m)
        v = float(m.max())
        if v > best:
            best = v
            best_h = h
            yy, xx = np.unravel_index(int(np.argmax(m)), m.shape)
            best_yx = (yy + proto.CZ0, xx + proto.CX0)
    print(f"exact best={best:.3f}, h={best_h}, center(y,x)={best_yx}")

    # Rank of exact best center by disk upper bound.
    y0, x0 = best_yx
    u0 = U[y0, x0]
    rank = int(np.sum(uc > u0))
    print(f"exact center U={u0:.3f}, disk rank={rank} / {uc.size}")

    flat = uc.ravel()
    order = np.argsort(flat)[::-1]
    for K in Ks:
        if K > order.size:
            continue
        best_f = -1.0
        best_fh = -1
        best_fc = None
        for idx in order[:K]:
            yy, xx = divmod(int(idx), uc.shape[1])
            cy, cx = yy + proto.CZ0, xx + proto.CX0
            prof = proto.radial_profile(w, cy, cx)
            hh = int(np.argmax(prof))
            vv = float(prof[hh])
            if vv > best_f:
                best_f, best_fh, best_fc = vv, hh, (cx, cy)
        print(f"  disk K={K:5d}: best={best_f:.3f} h={best_fh} center={best_fc} gap={best-best_f:.3f}")

    # Union: disk top200 + exact maps at h=0 and h=24 top200.
    cand = set()
    for idx in order[:200]:
        yy, xx = divmod(int(idx), uc.shape[1])
        cand.add((yy + proto.CZ0, xx + proto.CX0))
    for hh in (0, 24):
        m = maps[hh]
        for idx in np.argsort(m.ravel())[::-1][:200]:
            yy, xx = divmod(int(idx), m.shape[1])
            cand.add((yy + proto.CZ0, xx + proto.CX0))
    best_u = -1.0
    best_uh = -1
    best_uc = None
    for cy, cx in cand:
        prof = proto.radial_profile(w, cy, cx)
        hh = int(np.argmax(prof))
        vv = float(prof[hh])
        if vv > best_u:
            best_u, best_uh, best_uc = vv, hh, (cx, cy)
    print(f"union candidates={len(cand)}: best={best_u:.3f} h={best_uh} center={best_uc} gap={best-best_u:.3f}")


def main():
    w = proto.make_weights()
    run_case("patchy synthetic biome", w)
    run_case("uniform weights", np.ones_like(w), Ks=(100, 500))


if __name__ == "__main__":
    main()
