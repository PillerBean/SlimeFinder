# Ring quality benchmark

P0 benchmark for comparing candidate-generation ideas without re-running the
full map for every experiment.

## Files

```text
seeds.csv                   3 seeds + known/provisional reference points
candidates/*.csv            candidate pools (block coordinates + exact scores)
candidates/*.log            full-map prescan log for each pool
evaluate_pool.py            offline ranking evaluator
```

## Candidate pool schema

```text
seed,cx,cz,blocks,weight,pct
```

- `cx,cz`: candidate center in block coordinates.
- `blocks`: exact ring covered blocks at this center.
- `weight`: exact biome-weighted coverage.
- `pct`: coverage percentage.

Pools are built from:

```text
full-map square prescan grid=15 minDensity=0.18 maxSeeds=4000
+ top-32 seed centers × all 16×16 block suboffsets
+ known reference point (when supplied)
```

They are a **ranking benchmark**, not a complete candidate set. A new candidate
generator should be compared on recall and quality at the same K, then verified
on the full map only if it wins offline.

## Reproduce a pool

```powershell
$env:RING_SEED='-8180004378910677489'
$env:RING_GRID='15'
$env:RING_MIN_DENSITY='18'
$env:RING_MAX_SEEDS='4000'
$env:RING_POOL_TOP_SEEDS='32'
$env:RING_POOL_K='10000'
$env:RING_POOL_CSV='benchmarks\ring\candidates\newseed.csv'
$env:RING_POOL_REF='17415252,28556492'
.\build\slime_ring_fullmap_bench.exe prescan
```

## Evaluate

```powershell
py benchmarks\ring\evaluate_pool.py `
  benchmarks\ring\candidates\newseed.csv `
  benchmarks\ring\candidates\seed0.csv `
  --ref 17415252,28556492 --radius 16
```

The evaluator ranks by `weight`; `reference_rank` is the first candidate within
`--radius` blocks of the reference. `topK.has_reference` reports whether the
reference survives the prefix.

## Current baseline

| seed | role | ref blocks | ref weight | status |
|---|---|---:|---:|---|
| -8180004378910677489 | community reference | 14739 | 14595.893 | verified |
| 0 | old internal baseline | 14387 | 14324.516 | verified |
| 12345 | random smoke | 14016 | 13878.871 | provisional pool best |

## Metrics for a new candidate kernel

```text
recall@1/10/100/1000: reference within K and radius
best weight in top K
candidate generation time
exact evaluation count
memory peak
```

## Community references

Used as algorithm ideas and quality references, not as proof of optimality and
not copied as code without checking each repository's license.

- https://github.com/yiezias/BetterSlimeFarm
- https://github.com/wifi504/find-slime-chunk
- https://github.com/dunxuan/SlimeChunkCounter
- https://pypi.org/project/active-cubiomes/0.2.0/
- https://www.bilibili.com/en/video/BV1JUTR6CEtS/

## Known limitations

- The third seed's reference is the best pool candidate, not a separately
  verified planner optimum.
- Pools contain only top-32 seed neighborhoods plus a reference point.
- A pool win is necessary but not sufficient for a full-map win.
