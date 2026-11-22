#!/usr/bin/env python3
"""Prototype k=3 annular upper-bound prefilter vs square K-seed refinement.

This is a small-map quality benchmark, not a full-map timing claim.
The exact coverage arrays come from ring_model.Model.coverage().
"""
from __future__ import annotations

import csv
import sys
from pathlib import Path

import numpy as np

from ring_model import Model

ERR_K3 = 860
SAMPLES = list(range(0, 16, 3))  # 0,3,6,9,12,15 -> 36 offsets


def square_eval(mod: Model, X: np.ndarray, K: int):
    S = mod.scores(X)
    flat = []
    for z in range(mod.N):
        for x in range(mod.N):
            flat.append((int(S[z, x]), z, x))
    flat.sort(key=lambda t: (-t[0], t[1], t[2]))
    T = flat[:K]
    ev = np.zeros((mod.N, mod.N), dtype=bool)
    for _, z, x in T:
        ev[z, x] = True
    for _, z, x in T[: min(8, len(T))]:
        ev[max(0, z - 1):z + 2, max(0, x - 1):x + 2] = True
    C = mod.coverage(X)
    V = int(C[ev].max())
    M = int(C.max())
    return V, M, int(V < M), int(S.max())


def k3_eval(mod: Model, X: np.ndarray, lower: int):
    C = mod.coverage(X)
    M = int(C.max())
    sample_max = C[:, :, SAMPLES, :][:, :, :, SAMPLES].max(axis=(2, 3))
    U = sample_max + ERR_K3
    cand = U >= int(lower)
    V = int(C[cand].max()) if cand.any() else 0
    return V, M, int(V < M), int(cand.sum()), int(U.max()), int(cand.any())


def one_case(mod: Model, X: np.ndarray, K: int):
    Vs, M, Fs, _ = square_eval(mod, X, K)
    Vk, M2, Fk, nCand, maxU, anyCand = k3_eval(mod, X, Vs)
    sound = M2 <= maxU
    return dict(Vs=Vs, Vk=Vk, M=M, Fs=Fs, Fk=Fk, nCand=nCand, sound=int(sound))


def main():
    root = Path(__file__).resolve().parent
    print("k=3 prototype: ERR=860, samples=6x6 offsets every 3 blocks")
    print("=" * 78)

    # 1) 337-chunk tie-free counterexample
    N = 36
    mod = Model(N, 24, 128, 15, 1)
    X = np.load(root / "X_counterexample.npy")
    C = mod.coverage(X)
    M = int(C.max())
    Vs, _, Fs, _ = square_eval(mod, X, 1)
    Vk, _, Fk, nCand, maxU, anyC = k3_eval(mod, X, Vs)
    print(f"N=36, K=1: square V={Vs} F={Fs}; k3 V={Vk} F={Fk} "
          f"candidates={nCand}/{N*N} maxU={maxU} M={M} sound={int(M <= maxU)}")
    print()

    # 2) Random small maps: measure recovery on square failures
    rng = np.random.default_rng(20261122)
    for N in (64, 100):
        mod = Model(N, 24, 128, 15, 1)
        for K in (1, 5, 10, 20):
            total = 0
            sq_fail = 0
            rec = 0
            bad_bound = 0
            cand_sum = 0
            for _ in range(30):
                X = (rng.random((N, N)) < 0.1).astype(np.int8)
                r = one_case(mod, X, K)
                total += 1
                cand_sum += r["nCand"]
                if r["Fs"]:
                    sq_fail += 1
                    if r["Vk"] == r["M"]:
                        rec += 1
                if not r["sound"]:
                    bad_bound += 1
            print(f"N={N} K={K:2d}: maps={total} square_fail={sq_fail:2d} "
                  f"k3_recovered={rec:2d} bound_violations={bad_bound} "
                  f"avg_candidates={cand_sum/total:.1f}")

    print("K3-PROTOTYPE OK")


if __name__ == "__main__":
    main()
