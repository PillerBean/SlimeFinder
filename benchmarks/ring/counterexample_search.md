# Counterexample search

Date: 2026-11-21  
Branch: `flow-integration`

## Goal

Stop using reference-tail probability. Search for actual maps where:

```text
algorithmOutput.coveredBlocks < exactGlobalOptimum
```

## Exhaustive 4×4 model

Tool:

```text
tools/bench/ring_counterexample.cpp
```

Geometry:

```text
G = 4 chunks
R = 32
inner = 8
square = 3 chunks
K = 1 or 2
```

All configurations:

```text
2^16 = 65,536
```

Oracle:

```text
every 1-block center
exact ring coverage
```

### Result

```text
K=1: failures = 0 / 65,536
K=2: failures = 0 / 65,536
K=1, square=g1: failures = 0 / 65,536
```

For this reduced model, the exact failure probability is:

```text
P_fail = 0
```

This is an exhaustive result for the small model only, not the full map.

## Random 8×8 model

Because `2^64` is not exhaustively enumerable, sampled iid maps with
`p(slime)=0.1`.

| G | R | inner | square | K | samples | failures |
|---:|---:|---:|---:|---:|---:|---:|
| 8 | 32 | 8 | 7 | 1 | 200 | 0 |
| 8 | 32 | 8 | 3 | 1 | 500 | 0 |
| 8 | 48 | 16 | 1 | 1 | 1000 | 0 |
| 8 | 32 | 8 | 7 | 0 | 200 | 0 |

`K=0` is a fallback sanity check, not a product mode.

## What this means

```text
No counterexample was found in the exhaustive 4×4 model.
No counterexample was found in 1,900 random 8×8 maps.
```

It does **not** mean the full-map failure probability is zero.

With 1,000 samples and zero failures, the rule-of-three gives a 95% upper
bound of about:

```text
3 / 1000 = 0.3% per 8×8 map
```

That bound is still far above “practically impossible”; more samples or a
larger exhaustive model would be needed.

## Why no probability from reference tails

The previous `0.0048%` was **not** an algorithm failure probability. It was only
a geometric tail estimate for one kind of candidate. This search replaces it
with actual algorithm/oracle comparisons, but only on reduced maps.

## Next

```text
1. Exhaustive G=5 (2^25) if runtime permits
2. Random G=8 with more samples (10k+)
3. SAT/CP-SAT adversarial search for G=16+
4. Only after a counterexample exists, compute its p=0.1 generation mass
```

## Run

```powershell
$env:CE_G='4'; $env:CE_R='32'; $env:CE_INNER='8'; $env:CE_SQUARE='3'; $env:CE_K='1'
.\build\slime_ring_counterexample.exe

$env:CE_G='8'; $env:CE_R='32'; $env:CE_INNER='8'; $env:CE_SQUARE='3'; $env:CE_K='1'; $env:CE_RANDOM='500'
.\build\slime_ring_counterexample.exe
```
