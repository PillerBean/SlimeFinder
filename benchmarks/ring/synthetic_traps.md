# Synthetic ring traps

Date: 2026-11-21  
Branch: `flow-integration`

## Purpose

Test candidate generation + planner refinement against hand-built slime-chunk
grid maps with known structure, without a Minecraft seed.

## Traps

| trap | structure | intended failure mode |
|---|---|---|
| `single_ring` | complete annulus | baseline correctness |
| `inner_decoy` | dense core inside inner hole + annulus | center/hole confusion |
| `square_decoy` | dense square + annulus | square-density bias |
| `border_ring` | annulus near map border | clipping |
| `thin_ring` | thin sparse annulus | low square density |
| `decoy_field` | 36 dense decoys + sparse ring | Top-32 seed starvation |
| `outer_shell` | slime only in r=120..128 + decoys | square window blind spot |
| `outer_shell_only` | slime only in outer shell | square window blind spot |
| `outer_shell_trap` | outer shell + corner decoys | Top-32 seed starvation |
| `shell_vs_singles` | outer shell + 32 isolated decoys | square score 1 beats shell score 0 |
| `checker` | deterministic sparse noise | no-structure sanity |

## Current result

```text
TRAP single_ring        oracle=(776,776,46796) got=(775,775,46796) ratio=1.000 seedRank=1
TRAP inner_decoy        oracle=(776,776,46796) got=(775,775,46796) ratio=1.000 seedRank=1
TRAP square_decoy       oracle=(776,776,46796) got=(775,775,46796) ratio=1.000 seedRank=1
TRAP border_ring        oracle=(72,72,32054)    got=(75,75,32204) ratio=1.005 seedRank=1
TRAP thin_ring          oracle=(760,744,4096)   got=(774,775,4096) ratio=1.000 seedRank=6
TRAP decoy_field        oracle=(936,184,21703)  got=(183,943,21793) ratio=1.004 seedRank=-
TRAP outer_shell        oracle=(520,264,9345)   got=(272,519,9456) ratio=1.012 seedRank=-
TRAP outer_shell_only   oracle=(520,520,3788)   got=(519,519,3788) ratio=1.000 seedRank=1
TRAP outer_shell_trap   oracle=(152,120,19464)  got=(119,151,19464) ratio=1.000 seedRank=-
TRAP shell_vs_singles   oracle=(520,520,3788)   got=(519,519,3788) ratio=1.000 seedRank=1
TRAP checker            oracle=(152,136,6461)   got=(395,500,6506) ratio=1.007 seedRank=-
SYNTHETIC-RING OK failures=0 known_gaps=0 traps=11
```

## Interpretation

The planner survives all 11 structural traps in this small grid, including
outer-shell-only maps and multi-decoy maps. No known-gap was triggered in the
current implementation.

The strongest traps are still:

```text
thin_ring: target enters Top-32 at rank 6, not rank 1
outer-shell maps: square 15×15 score is zero at the true center
```

They show where a candidate kernel could become fragile; this implementation's
planner polish currently recovers them.

## Limitation

The oracle only samples chunk centers. A block-level sub-offset can therefore
report a slightly higher result than the oracle (`border_ring` 1.005).
This is structural diagnostics, not global block-level optimality proof.

## Run

```powershell
.\build\slime_ring_synthetic_tests.exe
```

Source:

```text
tests/integration/ring_synthetic_test.cpp
```
