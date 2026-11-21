"""Random-model probe: how often does the two-stage pipeline fail at moderate N?

Also measures structural quantities:
  * rank of S(chunk of the global C-argmax) among all chunks
  * beta = max over offsets of the annulus mass that the 15x15 window cannot see
"""
import numpy as np
from ring_model import Model, build_kernel

W = build_kernel(24, 128)
d = 8
off = np.arange(-d, d + 1)
I, J = np.meshgrid(off, off, indexing="ij")
win = (np.abs(I) <= 7) & (np.abs(J) <= 7)
beta = []
for oy in range(16):
    for ox in range(16):
        w = W[:, :, oy, ox]
        beta.append(int(w[~win].sum()))
print("beta = max mass outside the 15x15 chunk window, over offsets:", max(beta),
      " min:", min(beta), " mean:", float(np.mean(beta)))
print("annulus area:", int(W[:, :, 8, 8].sum()))

rng = np.random.default_rng(7)
for N in (32, 64, 100):
    print(f"\n=== N={N} (chunks={N*N}) p=0.1 ===")
    for K in (1, 10, 100):
        fails = 0
        reps = 12 if N <= 64 else 6
        ranks = []
        for rep in range(reps):
            X = (rng.random((N, N)) < 0.1).astype(np.int8)
            M = Model(N, 24, 128, 15, K, W=W)
            res = M.evaluate(X)
            fails += res["F"]
            z, x, oy, ox = res["argmax"]
            S = res["S"]
            rank = 1 + int((S > S[z, x]).sum())
            ranks.append(rank)
        print(f"  K={K:4d}: failures {fails}/{reps}   rank of argmax chunk "
              f"min={min(ranks)} med={int(np.median(ranks))} max={max(ranks)}")
