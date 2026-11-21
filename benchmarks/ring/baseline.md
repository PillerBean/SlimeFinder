# Ring benchmark baseline (P0)

Date: 2026-11-21  
Branch: `flow-integration`

## Result

| seed | role | pool rows | best blocks | best weight | reference rank |
|---|---|---:|---:|---:|---:|
| -8180004378910677489 | community reference | 8193 | 14739 | 14595.893165 | 1 |
| 0 | old internal baseline | 8193 | 14387 | 14324.516000 | 1 |
| 12345 | random smoke | 8192 | 14016 | 13878.871000 | 1 |

All three pools keep their reference at rank 1 by exact weighted coverage.

## Pool construction

```text
full-map square prescan:
  grid=15
  minDensity=0.18
  maxSeeds=4000
  prescanSep=16

candidate dump:
  top-32 seed centers × 16×16 block suboffsets
  + known reference point (where available)
```

Runtime per seed: about 237–241s, including 4.7–4.8s for 8,192 exact
candidate evaluations.

## Interpretation

This is a **ranking benchmark**, not a complete search. It is sufficient to:

```text
compare candidate-scoring ideas without rerunning the full map
check whether a known reference survives top-K
measure quality/time regressions
```

It is not sufficient to prove a candidate generator has full-map recall.

## Next

P1: ring-weighted chunk convolution prototype.

```text
input: chunk-level slime bitmap / candidate pool
score: Σ slimeChunk × chunkWeight(offset)
output: Top-K
compare: reference rank + top-K quality vs this P0
```

Only a P1 winner gets a full-map confirmation run.
