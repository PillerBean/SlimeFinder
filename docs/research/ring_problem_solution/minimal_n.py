"""Smallest N search for the real geometry (r=24,R=128,g=15), K=1, |X|<=2.

Uses precomputed per-chunk coverage maps so that a pair costs one array add.
"""
import numpy as np
from ring_model import Model, build_kernel

W = build_kernel(24, 128)


def chunk_map(N, u):
    """coverage contributed by the single chunk u: (N,N,16,16)."""
    m = Model(N, 24, 128, 15, 1, W=W)
    X = np.zeros((N, N), dtype=np.int8)
    X[u] = 1
    return m.coverage(X)


def evaluate_maps(N, K, maps, chunks):
    """V, M, ev for the map equal to the sum of the given single-chunk maps."""
    C = np.zeros((N, N, 16, 16), dtype=np.int32)
    S = np.zeros((N, N), dtype=np.int32)
    h = 7
    for u in chunks:
        C += maps[u]
        S[max(0, u[0] - h):u[0] + h + 1, max(0, u[1] - h):u[1] + h + 1] += 1
    order = sorted(((int(S[z, x]), z, x) for z in range(N) for x in range(N)),
                   key=lambda t: (-t[0], t[1], t[2]))
    ev = np.zeros((N, N), dtype=bool)
    for sc, z, x in order[:K]:
        ev[z, x] = True
    for sc, z, x in order[:min(8, K)]:
        ev[max(0, z - 1):z + 2, max(0, x - 1):x + 2] = True
    return int(C[ev].max()), int(C.max())


for N in range(4, 9):
    maps = {(z, x): chunk_map(N, (z, x)) for z in range(N) for x in range(N)}
    fail1 = [u for u in maps if evaluate_maps(N, 1, maps, [u])[0]
             < evaluate_maps(N, 1, maps, [u])[1]]
    print(f"N={N}: single-chunk failures = {len(fail1)} / {N*N}"
          + (f"   e.g. {fail1[:4]}" if fail1 else ""))
    if N <= 7:
        found = []
        cells = list(maps)
        for i in range(len(cells)):
            for j in range(i + 1, len(cells)):
                V, M = evaluate_maps(N, 1, maps, [cells[i], cells[j]])
                if V < M:
                    found.append((cells[i], cells[j], V, M))
                    if len(found) >= 3:
                        break
            if len(found) >= 3:
                break
        print(f"       |X|=2 failures: {'YES ' + str(found) if found else 'none'}")
