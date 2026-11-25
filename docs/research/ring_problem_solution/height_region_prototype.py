#!/usr/bin/env python3
"""Small exact prototype for height-dependent effective spawn area.

Fixed bedrock platform, biome-weighted area only, Euclidean shell 24..128.
This is a research prototype, not product code.
"""
from __future__ import annotations

import time
from collections import deque

import numpy as np

R = 128
INNER = 24
N = 384
CX0, CX1 = R, N - R  # center range (64..255 if N=384? actually 128..255)
CZ0, CZ1 = R, N - R


def fft_conv_same(a: np.ndarray, k: np.ndarray) -> np.ndarray:
    """Full convolution, then crop to 'same' (a's shape) with centered kernel."""
    full_h = a.shape[0] + k.shape[0] - 1
    full_w = a.shape[1] + k.shape[1] - 1
    fh = 1 << (full_h - 1).bit_length()
    fw = 1 << (full_w - 1).bit_length()
    fa = np.fft.rfft2(a, s=(fh, fw))
    fk = np.fft.rfft2(k, s=(fh, fw))
    full = np.fft.irfft2(fa * fk, s=(fh, fw))
    r = k.shape[0] // 2
    c = k.shape[1] // 2
    return full[r:r + a.shape[0], c:c + a.shape[1]]


def annulus_kernel(h: int) -> np.ndarray:
    y, x = np.mgrid[-R:R + 1, -R:R + 1]
    rho2 = x * x + y * y
    d2 = rho2 + h * h
    return ((d2 >= INNER * INNER) & (d2 <= R * R)).astype(np.float64)


def disk_kernel(radius: int) -> np.ndarray:
    y, x = np.mgrid[-radius:radius + 1, -radius:radius + 1]
    return ((x * x + y * y) <= radius * radius).astype(np.float64)


def make_weights() -> np.ndarray:
    """Synthetic biome weight map with broad patches + fine noise."""
    rng = np.random.default_rng(20261125)
    w = np.full((N, N), 0.12, dtype=np.float64)
    # Broad biome patches (high weight regions).
    for (y0, y1, x0, x1, v) in [
        (20, 150, 40, 170, 0.95),
        (150, 300, 190, 350, 0.65),
        (240, 360, 30, 150, 0.40),
        (60, 110, 250, 330, 0.80),
    ]:
        w[y0:y1, x0:x1] = v
    # Soft, fine variation (not block-random; biome is patchy).
    noise = rng.normal(0.0, 0.015, size=(N, N))
    w = np.clip(w + noise, 0.0, 1.0)
    return w


def radial_profile(w: np.ndarray, cy: int, cx: int):
    y, x = np.mgrid[-R:R + 1, -R:R + 1]
    rho2 = (x * x + y * y).ravel()
    mask = rho2 <= R * R
    patch = w[cy - R:cy + R + 1, cx - R:cx + R + 1].ravel()
    vals = patch[mask]
    r = rho2[mask]
    hist = np.bincount(r, weights=vals, minlength=R * R + 1)
    pref = np.concatenate(([0.0], np.cumsum(hist)))
    prof = np.zeros(R + 1, dtype=np.float64)
    for h in range(R + 1):
        lo2 = max(0, INNER * INNER - h * h)
        hi2 = R * R - h * h
        if hi2 > R * R:
            hi2 = R * R
        if lo2 <= hi2:
            prof[h] = pref[hi2 + 1] - pref[lo2]
    return prof


def connected_components(mask: np.ndarray):
    seen = np.zeros_like(mask, dtype=bool)
    comps = []
    h, w = mask.shape
    for y in range(h):
        for x in range(w):
            if not mask[y, x] or seen[y, x]:
                continue
            q = deque([(y, x)])
            seen[y, x] = True
            pts = []
            while q:
                yy, xx = q.popleft()
                pts.append((yy, xx))
                for dy, dx in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                    ny, nx = yy + dy, xx + dx
                    if 0 <= ny < h and 0 <= nx < w and mask[ny, nx] and not seen[ny, nx]:
                        seen[ny, nx] = True
                        q.append((ny, nx))
            comps.append(pts)
    return comps


def main():
    t0 = time.time()
    w = make_weights()
    t1 = time.time()
    print(f"weights: {N}x{N}, patchy, sum={w.sum():.2f}, t={t1-t0:.3f}s")

    # One height-independent upper bound: disk radius R.
    U = fft_conv_same(w, disk_kernel(R))
    print(f"disk upper bound: t={time.time()-t1:.3f}s")

    times = []
    best = -1.0
    best_hs = []
    maps = []
    for h in range(R + 1):
        th = time.time()
        m = fft_conv_same(w, annulus_kernel(h))
        maps.append(m[CZ0:CZ1, CX0:CX1])
        mx = float(maps[-1].max())
        if mx > best + 1e-9:
            best = mx
            best_hs = [h]
        elif abs(mx - best) <= 1e-9:
            best_hs.append(h)
        times.append(time.time() - th)
        if h % 16 == 0:
            print(f"  h={h:3d} max={mx:.6f} best={best:.6f} t={times[-1]*1000:.1f}ms")

    total = time.time() - t1
    print(f"exact all heights: {total:.2f}s, avg {np.mean(times)*1000:.1f}ms/h")
    print(f"global max={best:.8f}, best heights={best_hs}")
    print(f"upper bound max in center region={U[CZ0:CZ1, CX0:CX1].max():.6f}")

    # Group consecutive heights.
    bands = []
    if best_hs:
        a = b = best_hs[0]
        for h in best_hs[1:]:
            if h == b + 1:
                b = h
            else:
                bands.append((a, b))
                a = b = h
        bands.append((a, b))
    print(f"height bands at global max: {bands}")

    # Regions for the first best height.
    h0 = best_hs[0]
    m0 = maps[h0]
    mask = m0 >= best - 1e-9
    comps = connected_components(mask)
    comps.sort(key=len, reverse=True)
    print(f"regions at h={h0}: {len(comps)}, largest sizes={[len(c) for c in comps[:10]]}")
    for i, comp in enumerate(comps[:3]):
        ys = [p[0] for p in comp]
        xs = [p[1] for p in comp]
        print(f"  region {i}: n={len(comp)} center-bbox y={min(ys)+CZ0}..{max(ys)+CZ0}, "
              f"x={min(xs)+CX0}..{max(xs)+CX0}, rep=({xs[0]+CX0},{ys[0]+CZ0})")

    # Candidate fast check: top K by disk U.
    K = 200
    flat = U[CZ0:CZ1, CX0:CX1].ravel()
    order = np.argsort(flat)[::-1][:K]
    cy0 = CZ0
    cx0 = CX0
    best_fast = -1.0
    best_fast_c = None
    best_fast_h = -1
    for idx in order:
        yy, xx = divmod(int(idx), CX1 - CX0)
        cy, cx = yy + cy0, xx + cx0
        prof = radial_profile(w, cy, cx)
        hh = int(np.argmax(prof))
        vv = float(prof[hh])
        if vv > best_fast:
            best_fast = vv
            best_fast_c = (cx, cy)
            best_fast_h = hh
    print(f"fast topK={K}: best={best_fast:.8f}, center={best_fast_c}, h={best_fast_h}")
    print(f"exact max={best:.8f}, gap={best-best_fast:.8f}")
    print(f"total t={time.time()-t0:.2f}s")


if __name__ == "__main__":
    main()
