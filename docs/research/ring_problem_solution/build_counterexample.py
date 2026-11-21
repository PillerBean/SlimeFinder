"""Search for an explicit counterexample  F(X)=1  for the real geometry
   r=24, R=128, g=15, s=16, K=1.

Idea ("waste asymmetry"):
  * hidden centre v*: select every chunk that is FULLY inside the annulus at
    offset o*=(7,7) -> |A_h|=168 chunks, C(c*)=168*256=43008, S(v*)=168.
  * competitor q: select 169 chunks inside W(q) chosen to minimise the max
    coverage over B(q) u B(N(q))  (2304 block centres).
Then V_K < C(c*) <= M(X)  =>  F(X)=1.
"""
import numpy as np
import itertools, json, sys
from ring_model import Model, build_kernel

r, R, g, s = 24, 128, 15, 16
W = build_kernel(r, R)
N = 36
K = 1
M = Model(N, r, R, g, K, W=W)

# ---------- hidden blob ----------
DEL = np.arange(-8, 9)
dy, dx = np.meshgrid(DEL, DEL, indexing="ij")
wref = W[:, :, 7, 7]
full = [(int(a), int(b)) for a, b in zip(dy[wref == 256], dx[wref == 256])]
print("fully covered chunks at offset (7,7):", len(full), " -> C =", 256 * len(full))

vstar = np.array([10, 10])
hide = [(vstar[0] + a, vstar[1] + b) for a, b in full]

# ---------- competitor weights ----------
q = np.array([28, 28])
# local chunk offsets dy,dx in [-7,7]; local centres: 3x3 chunks, 16x16 offsets
loc = [(a, b) for a in range(-7, 8) for b in range(-7, 8)]
cen = [((a, b), (oy, ox)) for a in range(-1, 2) for b in range(-1, 2)
       for oy in range(16) for ox in range(16)]
wt = np.zeros((len(cen), len(loc)), dtype=np.int32)
for ci, ((ca, cb), (oy, ox)) in enumerate(cen):
    for li, (a, b) in enumerate(loc):
        # chunk q+(a,b) relative to the centre chunk q+(ca,cb): delta = (a-ca, b-cb)
        dd = (a - ca, b - cb)
        if abs(dd[0]) <= 8 and abs(dd[1]) <= 8:
            wt[ci, li] = W[dd[0] + 8, dd[1] + 8, oy, ox]
print("competitor: centres", wt.shape[0], "chunks", wt.shape[1],
      "  zero-for-all chunks:", int((wt.max(axis=0) == 0).sum()))


def phi(sel):
    return int(wt[:, list(sel)].sum(axis=1).max())


# greedy: start with all chunks invisible to every evaluated centre, then add
# the chunk with the smallest resulting maximum.
sel = set(np.flatnonzero(wt.max(axis=0) == 0).tolist())
print("start sel", len(sel), "phi", phi(sel))
while len(sel) < 169:
    best, bestv = None, None
    cur = wt[:, list(sel)].sum(axis=1) if sel else np.zeros(len(cen), np.int32)
    colmax = np.max(cur[:, None] + wt, axis=0)
    rest = [i for i in range(len(loc)) if i not in sel]
    order = sorted(rest, key=lambda i: colmax[i])
    for i in order[:40]:
        v = int((cur + wt[:, i]).max())
        if bestv is None or v < bestv:
            best, bestv = i, v
    sel.add(best)
print("greedy sel", len(sel), "phi", phi(sel))

# local search: swap one in / one out while phi improves
cur = phi(sel)
improved = True
it = 0
while improved and it < 40:
    improved = False
    it += 1
    inside = sorted(sel)
    outside = [i for i in range(len(loc)) if i not in sel]
    # candidate removals: those with the largest weight-sum in the binding centre
    for i in inside:
        base = sel - {i}
        cur2 = wt[:, list(base)].sum(axis=1)
        for j in outside:
            v = int((cur2 + wt[:, j]).max())
            if v < cur:
                sel = base | {j}
                cur = v
                improved = True
                break
        if improved:
            break
print("local sel", len(sel), "phi", phi(sel))

# ---------- build X and verify ----------
X = np.zeros((N, N), dtype=np.int8)
for a, b in hide:
    X[a, b] = 1
for li in sel:
    a, b = loc[li]
    X[q[0] + a, q[1] + b] = 1

res = M.evaluate(X)
C, S, ev = res["C"], res["S"], res["ev"]
print("slime chunks:", int(X.sum()))
print("S(v*)=", int(S[tuple(vstar)]), "  S(q)=", int(S[tuple(q)]),
      "  top1=", res["top1"], "  S(top1)=", int(S[res["top1"]]))
print("V=", res["V"], " M=", res["M"], " F=", res["F"])
print("C(c*)=", int(C[vstar[0], vstar[1], 7, 7]))
print("v* evaluated?", bool(ev[tuple(vstar)]), " q evaluated?", bool(ev[tuple(q)]))
print("score board:", [(int(sc), z, x) for sc, z, x in res["order"][:6]])
print("rank of v*:", [k for k, (sc, z, x) in enumerate(res["order"])
                      if (z, x) == tuple(vstar)][0])
np.save("X_counterexample.npy", X)
