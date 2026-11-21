"""Real-scale extreme-value scales for the two competing quantities.

(1) M = max over all block centres of C.  Single-centre tail via the exact
    Chernoff/Legendre rate, then union bound over the number of centres.
(2) t_K = K-th largest square score.  Exact binomial tail.

Also: beta = max invisible annulus mass (outside the 15x15 chunk window).
"""
import numpy as np
from ring_model import build_kernel

r, R, g, p = 24, 128, 15, 0.1
W = build_kernel(r, R)
d = 8
off = np.arange(-d, d + 1)
I, J = np.meshgrid(off, off, indexing="ij")
win = (np.abs(I) <= 7) & (np.abs(J) <= 7)

w = W[:, :, 8, 8].astype(float)          # interior centre
area = w.sum()
beta = max(int(W[:, :, oy, ox][~win].sum()) for oy in range(16) for ox in range(16))
print(f"annulus area A = {area:.0f},  beta_max = {beta}")
print(f"E[C] = p*A = {p*area:.1f}")

# ---- (1) Chernoff rate for one centre ----
ww = w[w > 0]
print("chunks carrying annulus mass:", ww.size, " sum of squares:", float((ww ** 2).sum()))
sd = np.sqrt(p * (1 - p) * (ww ** 2).sum())
print(f"sd(C) = {sd:.1f}")


def lam(theta):
    return float(np.log1p(p * (np.exp(theta * ww) - 1)).sum())


def lam_deriv(theta):
    e = np.exp(theta * ww)
    return float((p * ww * e / (1 + p * (e - 1))).sum())


N = 3_750_000
centres = N * N * 256
print(f"number of block centres = {centres:.3e}")

# solve Lambda*(v) = ln(centres) + margin  via v = Lambda'(theta)
target = np.log(centres)
lo, hi = 0.0, 1e-2
for _ in range(200):
    mid = (lo + hi) / 2
    if lam_deriv(mid) < target and lam(mid) < 1e12:
        pass
    # bisect on  g(theta) = theta*Lambda'(theta) - Lambda(theta) - ln(centres)
    g = mid * lam_deriv(mid) - lam(mid) - target
    if g < 0:
        lo = mid
    else:
        hi = mid
th = (lo + hi) / 2
v_star = lam_deriv(th)
print(f"theta* = {th:.6f}   M-scale (union bound, E[#exceed]=1): v = {v_star:.1f}")
print(f"   in chunk units: {v_star/256:.2f}")

# ---- (2) K-th square score ----
from math import lgamma, exp, log

def binom_sf(n, q, t):
    """P(Binom(n,q) >= t)"""
    tot = 0.0
    for k in range(t, n + 1):
        tot += exp(lgamma(n + 1) - lgamma(k + 1) - lgamma(n - k + 1)
                   + k * log(q) + (n - k) * log(1 - q))
    return tot

n = 225
for K in (4000,):
    target = K / (N * N)
    lo, hi = 0, n
    while lo < hi:
        mid = (lo + hi + 1) // 2
        if binom_sf(n, p, mid) >= target:
            lo = mid
        else:
            hi = mid - 1
    tK = lo
    print(f"K={K}: K/N^2 = {target:.3e}   t_K = {tK}   "
          f"(P(S>=t_K)={binom_sf(n,p,tK):.3e}, P(S>=t_K+1)={binom_sf(n,p,tK+1):.3e})")
    print(f"   256*t_K = {256*tK},  +beta = {256*tK+beta},   "
          f"in chunk units t_K + beta/256 = {tK + beta/256:.2f}")

print()
print("M scale sensitivity to the effective number of independent centres:")
for name, cnt in [("all 256*N^2 centres", N * N * 256),
                  ("N^2 chunks * 8 offsets", N * N * 8),
                  ("N^2 chunks * 4 offsets", N * N * 4),
                  ("N^2 chunks", N * N)]:
    tgt = np.log(cnt)
    lo2, hi2 = 1e-9, 0.02
    for _ in range(300):
        mid = (lo2 + hi2) / 2
        g = mid * lam_deriv(mid) - lam(mid) - tgt
        if g < 0:
            lo2 = mid
        else:
            hi2 = mid
    th2 = (lo2 + hi2) / 2
    print(f"  {name:26s}: M = {lam_deriv(th2):8.0f}  (M/256 = {lam_deriv(th2)/256:5.2f})")

# what does the success criterion need?  M >= 256*t_K + beta
print()
print("SUCCESS GUARANTEE:  M >= 256*t_K + beta  <=>  M/256 >= t_K + beta/256")
print(f"   M/256 scale = {v_star/256:.2f}  vs  t_K + beta/256 = {tK + beta/256:.2f}")
