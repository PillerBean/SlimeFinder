"""Minimal-N search under the NEW threshold definition (T_K = {S >= t_K}).

Random + hill-climbing search for small N, K=1.
"""
import numpy as np
from ring_model import Model, build_kernel

W = build_kernel(24, 128)
rng = np.random.default_rng(20261121)


def evaluate_thr(mod, X):
    N, K = mod.N, mod.K
    C = mod.coverage(X)
    S = mod.scores(X)
    tK = int(np.sort(S.ravel())[::-1][K - 1])
    T = [(int(S[z, x]), z, x) for z in range(N) for x in range(N) if S[z, x] >= tK]
    T.sort(key=lambda t: (-t[0], t[1], t[2]))
    ev = np.zeros((N, N), dtype=bool)
    for sc, z, x in T:
        ev[z, x] = True
    for sc, z, x in T[:min(8, len(T))]:
        ev[max(0, z - 1):z + 2, max(0, x - 1):x + 2] = True
    V = int(C[ev].max())
    M = int(C.max())
    return V, M, int(V < M), tK, len(T)


best = []
for N in (10, 12, 14, 16, 20, 24, 32):
    mod = Model(N, 24, 128, 15, 1, W=W)
    found = None
    for trial in range(4000):
        k = int(rng.integers(2, 7))
        cells = rng.choice(N * N, size=k, replace=False)
        X = np.zeros((N, N), dtype=np.int8)
        X.ravel()[cells] = 1
        V, M, F, tK, nT = evaluate_thr(mod, X)
        if F:
            found = (k, V, M, tK, nT, sorted(map(tuple, np.argwhere(X).tolist())))
            break
    print(f"N={N:3d}: {'FOUND ' + str(found) if found else 'none in 4000 random trials'}")
    if found:
        best.append((N, found))
        if N >= 20:
            break
print()
print(best)
