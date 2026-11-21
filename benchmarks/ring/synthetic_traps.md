# Synthetic ring traps

Date: 2026-11-21  
Branch: `flow-integration`

## Purpose

Test the **candidate generation + planner refinement** against hand-built
slime-chunk grid maps with known structure, without a Minecraft seed.

The oracle is computed at **chunk centers only**, not every block center. A
planner result at a sub-chunk offset can therefore be slightly higher than the
reported oracle; that is expected.

## Traps

| trap | structure | intended failure mode |
|---|---|---|
| `single_ring` | complete annulus around one center | baseline correctness |
| `inner_decoy` | dense core inside the inner hole + annulus | center/hole confusion |
| `square_decoy` | dense square + annulus | square-density proxy bias |
| `border_ring` | annulus near map border | clipping/edge handling |
| `thin_ring` | thin sparse annulus | low square density hides the true ring |
| `decoy_field` | 36 dense decoy squares + sparse center annulus | Top-32 square seeds filled by decoys |
| `checker` | deterministic sparse noise | no-structure sanity |

## Current result

```text
TRAP single_ring      oracle=(776,776,46796) got=(775,775,46796) ratio=1.000 seedRank=1
TRAP inner_decoy      oracle=(776,776,46796) got=(775,775,46796) ratio=1.000 seedRank=1
TRAP square_decoy     oracle=(776,776,46796) got=(775,775,46796) ratio=1.000 seedRank=1
TRAP border_ring      oracle=(72,72,32054)    got=(75,75,32204) ratio=1.005 seedRank=1
TRAP thin_ring        oracle=(760,744,4096)   got=(774,775,4096) ratio=1.000 seedRank=6
TRAP decoy_field      oracle=(936,184,21703)  got=(183,943,21793) ratio=1.004 seedRank=-1
TRAP checker          oracle=(152,136,6461)   got=(395,500,6506) ratio=1.007 seedRank=-1
SYNTHETIC-RING OK failures=0 traps=7
```

## Interpretation

- The planner is not fooled by inner holes or dense square decoys in these
  small maps.
- `thin_ring` still enters the Top-32 square seeds (rank 6).
- `decoy_field` did not force a Top-32 miss; the reference ring was not the
  reported chunk-center oracle, and the planner found a slightly better edge.
- `checker` has no meaningful reference; its result is only a sanity check.

## Limitation

The oracle only samples chunk centers. This is enough for structural diagnostics
but not a proof of global block-level optimality.

## Run

```powershell
.\build\slime_ring_synthetic_tests.exe
```

Source:

```text
tests/integration/ring_synthetic_test.cpp
```
