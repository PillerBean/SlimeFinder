# k=3 upper-bound B&B on real seeds

Date: 2026-11-22  
Status: candidate-level prototype; not a full-map 36-convolution implementation

## What was run

The square prescan produces 4000 seed chunks. For each seed we compute the
36-offset k=3 upper bound:

```text
U(v) = max_{o∈{0,3,6,9,12,15}²} C(v,o) + 860
```

Then:

```text
L = exact value of the ordinary square-seed baseline
S = { v : U(v) >= L }
refine only S exactly
```

This is a B&B layer over the existing square seed universe. It does **not** scan
all N² chunks with 36 convolutions.

## Results

| seed | baseline blocks | k=3 blocks | selected candidates | upper-bound time | total |
|---|---:|---:|---:|---:|---:|
| -8180004378910677489 | 14739 | 14739 | 8 / 4000 | 210.6 ms | 217.67 s |
| 0 | 14387 | 14387 | 28 / 4000 | 211.0 ms | 219.75 s |
| 12345 | 14742 | 14742 | 4 / 4000 | 210.4 ms | 219.66 s |

All three seeds reproduce the square baseline value. No quality regression.

## Interpretation

```text
The B&B layer is cheap: about 0.21 s for 4000 seeds.
It cuts exact refinement from 4000 chunks to 4–28 chunks.
Total runtime is unchanged because the full-map square prescan is ~217 s.
```

So this experiment validates:

```text
- 36-offset upper bound can be computed quickly for candidate chunks
- B&B selection does not lose the known optimum on these seeds
- the candidate reduction is large at the seed level
```

It does not validate:

```text
- a full-map 36-convolution k=3 prefilter
- soundness over all chunks (the seed universe is still square-selected)
- the expected ~4800-candidate full-map set
- a ≤210 s total runtime
```

## Why total time did not drop

The dominant cost remains:

```text
square prescan: ~216–219 s
k=3 upper bound: ~0.21 s
B&B/refine: ~0.3–10 ms
```

A genuine k=3 replacement must remove or reduce the 216 s full-map prescan.
A candidate-level B&B on top of that prescan cannot improve wall-clock time.

## Artifacts

```text
benchmarks/ring/k3_newseed.log
benchmarks/ring/k3_seed0.log
benchmarks/ring/k3_seed12345.log
```

## Next required step

Implement the 36-offset annulus kernel as the **first-stage full-map scan**
(ideally GPU), then use this B&B layer on its output. Until that exists, the
speed claim for k=3 is unverified.
