"""Exact model for the SlimeFinder ring-coverage counterexample problem.

Definitions follow docs/planning/ring_counterexample_problem_paper.docx:

  chunk v = (i,j) in G_N = [0,N)^2,  block rect Q_v = [16i,16i+15] x [16j,16j+15]
  center c = (x,z) in Omega_N = [0,16N)^2
  w_v(c)   = #{(a,b) in Q_v : r^2 <= (a-x)^2+(b-z)^2 <= R^2}
  C_X(c)   = sum_v X_v w_v(c)
  S_X(v)   = sum_{|u-v|_inf <= 7} X_u                (g=15)
  TopK(X)  = chunks sorted by S desc, ties by (j,i) lex ascending
  E_K(X)   = B(TopK) u B(N(P8)),  B sets = all 256 block centers of the chunk
  F(X)=1  <=> max_{c in E_K} C(c) < max_{c in Omega_N} C(c)
"""
import numpy as np


def build_kernel(r=24, R=128, dmax=8):
    """W[dy,dx,oy,ox] = #cells of chunk (dy,dx) inside annulus centred at offset (oy,ox).

    Chunk (dy,dx) spans blocks [16dy,16dy+15] x [16dx,16dx+15] relative to the
    center's own chunk origin; the center sits at (oy,ox) inside that origin.
    """
    W = np.zeros((2 * dmax + 1, 2 * dmax + 1, 16, 16), dtype=np.int16)
    offs = np.arange(-dmax * 16, dmax * 16 + 16)
    yy, xx = np.meshgrid(offs, offs, indexing="ij")
    r2, R2 = r * r, R * R
    for oy in range(16):
        for ox in range(16):
            d2 = (xx - ox) ** 2 + (yy - oy) ** 2
            mask = (d2 >= r2) & (d2 <= R2)
            if not mask.any():
                continue
            for dy in range(-dmax, dmax + 1):
                for dx in range(-dmax, dmax + 1):
                    sub = mask[dy * 16 + dmax * 16: dy * 16 + dmax * 16 + 16,
                               dx * 16 + dmax * 16: dx * 16 + dmax * 16 + 16]
                    W[dy + dmax, dx + dmax, oy, ox] = sub.sum()
    return W


class Model:
    def __init__(self, N, r=24, R=128, g=15, K=1, W=None):
        self.N, self.r, self.R, self.g, self.K = N, r, R, g, K
        self.half = (g - 1) // 2
        self.dmax = (R + 15) // 16
        self.W = build_kernel(r, R, self.dmax) if W is None else W
        self.dmax = (self.W.shape[0] - 1) // 2
        self.X = np.zeros((N, N), dtype=np.int8)

    # ---------- geometry ----------
    def coverage(self, X=None):
        """C[v,o] = C_X(16v+o).  Returns int32 array (N,N,16,16)."""
        X = self.X if X is None else X
        N, d, W = self.N, self.dmax, self.W
        C = np.zeros((N, N, 16, 16), dtype=np.int32)
        for dy in range(-d, d + 1):
            for dx in range(-d, d + 1):
                kern = W[dy + d, dx + d]
                if not kern.any():
                    continue
                ys0, ys1 = max(0, -dy), N - max(0, dy)
                xs0, xs1 = max(0, -dx), N - max(0, dx)
                if ys0 >= ys1 or xs0 >= xs1:
                    continue
                src = X[ys0 + dy: ys1 + dy, xs0 + dx: xs1 + dx]
                if not src.any():
                    continue
                C[ys0:ys1, xs0:xs1] += src[:, :, None, None] * kern[None, None, :, :]
        return C

    def scores(self, X=None):
        """S_X(v) = box sum over the g x g chunk window (clipped at the map edge)."""
        X = self.X if X is None else X
        N, h = self.N, self.half
        P = np.zeros((N + 1, N + 1), dtype=np.int32)
        P[1:, 1:] = X.cumsum(0).cumsum(1)
        lo = np.maximum(np.arange(N) - h, 0)
        hi = np.minimum(np.arange(N) + h + 1, N)
        S = (P[np.ix_(hi, hi)] - P[np.ix_(lo, hi)]
             - P[np.ix_(hi, lo)] + P[np.ix_(lo, lo)])
        return S

    # ---------- algorithm ----------
    def order(self, X=None):
        """All (N^2) chunks in TopK order: S desc, ties by (j,i)=(z,x) lex ascending.

        The paper's chunk v=(i,j) has i = x-index, j = z-index; our arrays are
        [z, x], so the tie-break key is (z, x).
        """
        S = self.scores(X)
        idx = sorted(((int(S[z, x]), z, x) for z in range(self.N) for x in range(self.N)),
                     key=lambda t: (-t[0], t[1], t[2]))
        return idx, S

    def evaluated(self, order=None):
        """Boolean (N,N)[z,x] mask of chunks whose 256 block centers are evaluated."""
        N, K = self.N, self.K
        order = self.order()[0] if order is None else order
        ev = np.zeros((N, N), dtype=bool)
        for sc, z, x in order[:K]:
            ev[z, x] = True
        p8 = min(8, K)
        for sc, z, x in order[:p8]:
            ev[max(0, z - 1):z + 2, max(0, x - 1):x + 2] = True
        return ev

    def evaluate(self, X=None):
        """Returns V (algorithm value), M (true max), F, plus diagnostics."""
        X = self.X if X is None else X
        C = self.coverage(X)
        order, S = self.order(X)
        ev = self.evaluated(order)
        V = int(C[ev].max())
        z, x, oy, ox = np.unravel_index(int(np.argmax(C)), C.shape)
        M = int(C[z, x, oy, ox])
        return dict(V=V, M=M, F=int(V < M), C=C, S=S, order=order, ev=ev,
                    argmax=(int(z), int(x), int(oy), int(ox)),
                    top1=(order[0][1], order[0][2]))


if __name__ == "__main__":
    for r, R, g in [(24, 128, 15), (8, 32, 3)]:
        W = build_kernel(r, R)
        print(f"=== r={r} R={R} g={g} ===")
        print("total annulus cells (offset 8,8):", int(W[:, :, 8, 8].sum()))
        w = W[:, :, 8, 8]
        print("chunks with w>0:", int((w > 0).sum()), " w==256:", int((w == 256).sum()),
              " 0<w<256:", int(((w > 0) & (w < 256)).sum()))
