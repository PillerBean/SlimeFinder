# P1 ring-weighted candidate kernel prototype

Date: 2026-11-21  
Branch: `flow-integration`

## Goal

Replace the square-window proxy with a candidate score that matches the circular
annulus:

```text
kernelSeedScore(center) =
  Σ slimeChunk(cx,cz) × area(chunkRect ∩ annulus(inner,outer))
```

## Implementation

- Input: the 4000 square-prescan seed centers.
- Center convention: chunk center `(cx*16+8, cz*16+8)`.
- Kernel support: `k = ceil(outer/16)`, `(2k+1)²` chunks.
- Weight table: 16×16 subcell sampling of
  `inner² <= dx²+dz² <= outer²`, fixed because all centers use the +8 offset.
- Ranking: all seed centers scored in one pass; top-K seeds get their full
  16×16 block suboffsets evaluated exactly.
- No new full-map scan: the kernel runs on the existing square prescan seeds.

## Results

### Top-32 kernel seeds, threshold 0.18

```text
seeds=4000
kernel rank: 4.85s
exact 8192 candidates: 4.84s
total: 237.58s
result: 14739 / 14595.893165
center: 17415252,28556491
```

### Top-1 kernel seed, threshold 0.25

```text
prescan seeds=600
kernel rank: 0.15s
exact 256 candidates: 0.15s
total run 1: 218.92s
total run 2: 220.22s
result: 14739 / 14595.893165
center: 17415252,28556491
kernel best seed center: 17415256,28556488
```

## Interpretation

```text
The circular-annulus kernel does rank the target seed first.
Top-1 kernel seed already contains the 14739 target.
No quality gain over the square prescan.
No speed gain: the full-map prescan still dominates (~216–220s).
```

## Why it did not win yet

```text
The kernel only re-ranks existing square seeds.
It does not reduce the full-map square scan.
Exact refinement was already cheap (planner only refines one candidate).
```

## Next options

1. Put the weighted kernel into the GPU full-map pass, replacing the square
   window score:
   - one GPU candidate-generation pass
   - no square prescan
2. Or use the kernel to reduce the prescan:
   - coarser square scan + kernel re-rank
   - only if recall survives on the 3-seed benchmark

## Acceptance status

```text
P1 prototype: done
≤210s reproduce reference: not met (218.92–220.22s)
≤20min exceed 14739: not met
same quality with clear speedup: not met
```

## Geometry note

The annulus is circular. The old "outer square minus inner square" idea is
wrong for this objective and must not be used as the P1 score.
