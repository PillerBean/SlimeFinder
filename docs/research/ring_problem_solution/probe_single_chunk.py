"""Minimal-mass counterexample probe: |X| = 1.

With one slime chunk, S(v)=1 on the whole 15x15 chunk plateau around it, and the
lexicographic tie-break picks the extreme corner q of that plateau.  The 3x3
polish around q covers only 9 chunks, while the annulus wants the centre to sit
2..8 chunks away from the slime chunk -> the true maximiser is never evaluated.

This script checks, for a range of N and K, whether a single 1 already fails.
"""
import numpy as np
from ring_model import Model, build_kernel

W = build_kernel(24, 128)


def probe(N, K, z, x, reverse_tie=False):
    M = Model(N, 24, 128, 15, K, W=W)
    X = np.zeros((N, N), dtype=np.int8)
    X[z, x] = 1
    # order is sorted by (-S, z, x); emulate the opposite tie-break with a wrapper
    if reverse_tie:
        S = M.scores(X)
        order = sorted(((int(S[a, b]), a, b) for a in range(N) for b in range(N)),
                       key=lambda t: (-t[0], -t[1], -t[2]))
        C = M.coverage(X)
        ev = np.zeros((N, N), dtype=bool)
        for sc, a, b in order[:K]:
            ev[a, b] = True
        for sc, a, b in order[:min(8, K)]:
            ev[max(0, a - 1):a + 2, max(0, b - 1):b + 2] = True
        V = int(C[ev].max())
        Ms = int(C.max())
        return dict(V=V, M=Ms, F=int(V < Ms), top1=(order[0][1], order[0][2]), score=order[0][0])
    r = M.evaluate(X)
    return dict(V=r["V"], M=r["M"], F=r["F"], top1=r["top1"], score=r["S"][r["top1"]])


print("N   K  slime(z,x)  top1      S   V     M    F")
for N in range(4, 19):
    z = x = min(7, N - 1)
    for K in (1, 2, 8):
        r = probe(N, K, z, x)
        print(f"{N:3d} {K:2d}  ({z},{x})     {str(r['top1']):9s} {r['score']:3d} "
              f"{r['V']:5d} {r['M']:5d}  {r['F']}")
    if N >= 10:
        break
print()
print("single chunk, fixed N=16, all placements (K=1): does any placement fail?")
for z in range(16):
    row = []
    for x in range(16):
        r = probe(16, 1, z, x)
        row.append("F" if r["F"] else ".")
    print(f" z={z:2d} " + "".join(row))
print()
print("same, N=8 (K=1):")
for z in range(8):
    row = []
    for x in range(8):
        r = probe(8, 1, z, x)
        row.append("F" if r["F"] else ".")
    print(f" z={z:2d} " + "".join(row))
print()
print("reverse tie-break, N=8/16, K=1, corner placement:")
for N in (8, 16):
    z = x = min(7, N - 1)
    print(N, probe(N, 1, z, x, reverse_tie=True))
print()
print("K dependence, N=16, one chunk at (7,7):")
for K in (1, 2, 4, 8, 16, 32, 64, 128, 200, 224, 225, 226, 255, 256):
    r = probe(16, K, 7, 7)
    print(f"  K={K:4d}  top1={r['top1']} S={r['score']} V={r['V']} M={r['M']} F={r['F']}")
