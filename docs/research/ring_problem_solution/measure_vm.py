import numpy as np
from ring_model import Model, build_kernel

W = build_kernel(24, 128)
rng = np.random.default_rng(11)
for N, K in [(100, 100), (100, 10), (64, 10), (32, 10)]:
    for rep in range(3):
        X = (rng.random((N, N)) < 0.1).astype(np.int8)
        model = Model(N, 24, 128, 15, K, W=W)
        res = model.evaluate(X)
        S, C = res["S"], res["C"]
        qz, qx = res["top1"]
        mz, mx, moy, mox = res["argmax"]
        rank = 1 + int((S > S[mz, mx]).sum())
        print(f"N={N} K={K} rep={rep}: M={res['M']} V={res['V']} F={res['F']} "
              f"S(top1)={S[qz, qx]} S(argmax chunk)={S[mz, mx]} rank(argmax)={rank} "
              f"maxC@top1chunk={int(C[qz, qx].max())} meanC={float(C.mean()):.0f}")
