"""Threshold-definition (new Section 3) vs lexicographic definition.

New T_K = { v : S_X(v) >= t_K }   (uncapped threshold set)
E_K    = B(T_K) u B(N(P8)),  P8 = first min(8,|T_K|) of T_K by (S desc, lex)
"""
import numpy as np
from ring_model import Model, build_kernel

W = build_kernel(24, 128)


def evaluate_thr(N, K, maps, chunks, C_all=None):
    C = np.zeros((N, N, 16, 16), dtype=np.int32)
    S = np.zeros((N, N), dtype=np.int32)
    for u in chunks:
        C += maps[u]
        S[max(0, u[0] - 7):u[0] + 8, max(0, u[1] - 7):u[1] + 8] += 1
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
    return V, M, int(V < M), tK, len(T), tuple(T[0][1:])


def chunk_map(N, u):
    m = Model(N, 24, 128, 15, 1, W=W)
    X = np.zeros((N, N), dtype=np.int8)
    X[u] = 1
    return m.coverage(X)


print("--- single chunk under the NEW threshold definition ---")
for N, u in [(3, (0, 0)), (3, (1, 1)), (8, (7, 7)), (16, (7, 7))]:
    maps = {u: chunk_map(N, u)}
    print(f"  N={N} u={u}: V,M,F,tK,|T|,top1 = {evaluate_thr(N, 1, maps, [u])}")

print("\n--- 337-chunk tie-free example under the NEW definition (N=36, K=1) ---")
N = 36
X = np.load("X_counterexample.npy")
mod = Model(N, 24, 128, 15, 1, W=W)
S = mod.scores(X)
t1 = int(S.max())
print(f"  max S = {t1}, number of chunks attaining it = {int((S == t1).sum())}  "
      f"(unique => threshold set == lex set)")
res = mod.evaluate(X)
print(f"  V={res['V']}, M={res['M']}, F={res['F']}")

print("\n--- exhaustive minimal-N search under the NEW definition, |X|<=3 ---")
for N in range(3, 8):
    cells = [(z, x) for z in range(N) for x in range(N)]
    maps = {u: chunk_map(N, u) for u in cells}
    found = []
    for i in range(len(cells)):
        for j in range(i + 1, len(cells)):
            r = evaluate_thr(N, 1, maps, [cells[i], cells[j]])
            if r[2]:
                found.append((cells[i], cells[j], r))
                break
        if found:
            break
    print(f"  N={N}: |X|=2 failure: {'YES ' + str(found[0]) if found else 'none'}")
