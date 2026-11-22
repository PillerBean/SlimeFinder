# k=3 annular prefilter prototype vs square K-seed refinement

Date: 2026-11-22  
Status: quality prototype only; full-map timing not yet measured

## Method

For each chunk `v` and each of the 36 sampled block offsets

```text
o ∈ {0,3,6,9,12,15}²
```

compute the exact annular coverage `C(v,o)`.

Upper bound:

```text
U(v) = max_o C(v,o) + 860
```

The 860-cell correction is the k=3 Lipschitz error from the design note.

Algorithm:

```text
1. Run square prescan; refine K seeds to obtain lower bound L = V_square.
2. Keep every chunk with U(v) >= L.
3. Refine those chunks exactly.
4. Return the best exact value.
```

The square baseline is the same square prescan with K seeds and 3×3 polish.

## 1. The 337-chunk counterexample

```text
N=36, K=1
square: V=33426, F=1
k=3:    V=43008, F=0
candidates: 46 / 1296
max U=43842 >= M=43008
bound violations: 0
```

The k=3 upper bound includes the hidden optimum and recovers the full
`C*=43008`, while square top-1 fails.

## 2. Random small maps

30 iid p=0.1 maps per cell.

| N | K | square failures | k=3 recovered | bound violations | avg k=3 candidates |
|---:|---:|---:|---:|---:|---:|
| 64 | 1 | 18/30 | 18/18 | 0 | 175.0 |
| 64 | 5 | 11/30 | 11/11 | 0 | 96.4 |
| 64 | 10 | 7/30 | 7/7 | 0 | 70.1 |
| 64 | 20 | 5/30 | 5/5 | 0 | 88.8 |
| 100 | 1 | 21/30 | 21/21 | 0 | 180.9 |
| 100 | 5 | 11/30 | 11/11 | 0 | 112.6 |
| 100 | 10 | 5/30 | 5/5 | 0 | 85.4 |
| 100 | 20 | 4/30 | 4/4 | 0 | 74.0 |

In this sample:

```text
square failures: 82
k=3 recovered:   82/82
bound violations: 0
```

## Interpretation

```text
Quality: k=3 upper-bound candidate set recovers every square failure in this
         prototype and the 337-chunk counterexample.
Soundness: no sampled case violated U(v) >= true per-chunk maximum.
Candidate count: small on N=64/100; consistent with the design estimate of
                  ~4.8k candidates at full scale.
```

## What this does NOT establish

```text
- no full-map 36-convolution GPU implementation
- no 210s timing
- no full-map B&B implementation
- no large-sample probability estimate
- the k=3 correction is taken from the design note, not re-derived here
```

The prototype validates the **candidate completeness mechanism**, not the
production runtime.

## Reproduce

```powershell
cd docs/research/ring_problem_solution
python k3_prefilter_test.py
```
