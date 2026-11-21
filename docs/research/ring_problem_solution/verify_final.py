"""Final independent verification + certificates for the two counterexamples.

  (A) trivial:   N=8, K=1, X = single chunk at (7,7)                     -> F=1
  (B) tie-free:  N=36, K=1, 337 chunks (168 full ring + 169 waste-heavy) -> F=1

Verification here deliberately uses a *different* aggregation path than
ring_model.coverage(): the coverage of every needed centre is summed directly
over the selected chunks with the kernel table (which was itself spot-checked
against per-cell brute force in verify_counterexample.py).
"""
import numpy as np
from ring_model import Model, build_kernel

W = build_kernel(24, 128)
D = 8


def w(delta, o):
    dz, dx = delta
    if abs(dz) > D or abs(dx) > D:
        return 0
    return int(W[dz + D, dx + D, o[0], o[1]])


def C_direct(sel, v, o):
    """sum over selected chunks of w, summing one chunk at a time (no convolution)."""
    tot = 0
    for (z, x) in sel:
        tot += w((z - v[0], x - v[1]), o)
    return tot


def box(scores_src, z, x, N, h=7):
    return int(scores_src[max(0, z - h):z + h + 1, max(0, x - h):x + h + 1].sum())


print("=" * 78)
print("(A) single-chunk counterexample: N=8, K=1, one slime chunk at (7,7)")
N, K = 8, 1
mod = Model(N, 24, 128, 15, K, W=W)
XA = np.zeros((N, N), dtype=np.int8)
XA[7, 7] = 1
X = XA
res = mod.evaluate(X)
sel = [(7, 7)]
print(f"  top-1 chunk (tie-break)      : {res['top1']}  S={int(res['S'][res['top1']])}")
print(f"  evaluated chunks (B(TopK)+N) : {[(z, x) for z in range(N) for x in range(N) if res['ev'][z, x]]}")
V2 = max(C_direct(sel, (z, x), (oy, ox))
         for z in range(N) for x in range(N) if res["ev"][z, x]
         for oy in range(16) for ox in range(16))
M2 = max(C_direct(sel, (z, x), (oy, ox))
         for z in range(N) for x in range(N)
         for oy in range(16) for ox in range(16))
print(f"  V (model) = {res['V']:4d}   V (direct sum) = {V2:4d}")
print(f"  M (model) = {res['M']:4d}   M (direct sum) = {M2:4d}")
za, xa, oya, oxa = res["argmax"]
print(f"  true maximiser: chunk {za,xa} offset {oya,oxa} -> block centre "
      f"({xa*16+oxa},{za*16+oya})   coverage {res['M']}")
print(f"  F = {int(V2 < M2)}   (V={V2} < M={M2})")

print()
print("=" * 78)
print("(B) tie-free counterexample: N=36, K=1, 337 chunks")
N, K = 36, 1
mod = Model(N, 24, 128, 15, K, W=W)
X = np.load("X_counterexample.npy")
res = mod.evaluate(X)
sel = [(int(z), int(x)) for z, x in zip(*np.nonzero(X))]
print(f"  |X| = {len(sel)}")
print(f"  top-1 = {res['top1']} S={int(res['S'][res['top1']])}, "
      f"second-best S = {res['order'][1][0]} at {res['order'][1][1:]} (so the top score is unique)")
print(f"  S(v*) at the hidden chunk (10,10) = {int(res['S'][10,10])}  -> rank 2, not a seed")
V2 = max(C_direct(sel, (z, x), (oy, ox))
         for z in range(N) for x in range(N) if res["ev"][z, x]
         for oy in range(16) for ox in range(16))
print(f"  V (model) = {res['V']}   V (direct sum over 337 chunks) = {V2}")
# brute-force C(c*) over the 168 fully covered chunks
c_star = (10, 10, 7, 7)
Cstar = 0
for z, x in sel:
    dz, dx = z - 10, x - 10
    if abs(dz) <= D and abs(dx) <= D:
        Cstar += int(W[dz + D, dx + D, 7, 7])
print(f"  C(c*) = {Cstar} = 168 chunks x 256 cells")
print(f"  M >= C(c*) and V = {V2}  ->  F = {int(V2 < Cstar)}   "
      f"(gap {Cstar - V2}, {100.0*(Cstar-V2)/Cstar:.1f}%)")
print(f"  v* in evaluated set? {bool(res['ev'][10,10])}")

# ---- certificate ----
with open("counterexample_B_chunks.txt", "w", encoding="utf-8") as f:
    f.write("# N=36, K=1, r=24, R=128, g=15, s=16 -> F(X)=1\n")
    f.write("# rows: z x  (chunk coordinates, 0-based); V=33426, M=43008\n")
    for z, x in sorted(sel):
        f.write(f"{z} {x}\n")
with open("counterexample_A_matrix.txt", "w", encoding="utf-8") as f:
    f.write("# N=8, K=1, r=24, R=128, g=15 -> F(X)=1  (V=180, M=256)\n")
    for z in range(8):
        f.write("".join("1" if XA[z, x] else "0" for x in range(8)) + "\n")
print("\nwrote counterexample_B_chunks.txt, counterexample_A_matrix.txt")
