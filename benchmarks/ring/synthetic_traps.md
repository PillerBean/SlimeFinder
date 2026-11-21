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

## 1) shell_vs_singles 断言修正

诱饵从“单块”加强为 5×5 后：

```text
TRAP shell_vs_singles oracle=(136,136,39922)
  got=(135,135,39922)
  seedRank=-1
  sq=16

KNOWN-GAP shell_vs_singles seeded-out (absent)
```

外壳中心确实被挤出方形 Top-32。  
但注意：该图真正的最优是诱饵边缘（39922），不是外壳中心（3788），
所以这只能证明“外壳中心不被方形召回”，不能证明质量漏检。

## 2) 全 1 方块 oracle

高价值小图：

```text
grid = 16×16 chunks
radius = 64
inner = 16
centers = 65,536
```

结果：

```text
EXACT-ORACLE oracle=(135,135,9486)
  planner=(135,135,9486)
```

全 1 方块枚举与 planner 完全一致，通过。

## 3) outer_shell 接入环形加权核

```text
KERNEL-RECOVERY shell_vs_singles rank=1538
  top_score=39922
  target_score=3788
```

环形核也没有把外壳中心补回 Top-32。

原因不是核算错，而是该图的外壳中心不是真实最优：

- 方形角落看到 16 个外壳块；
- 5×5 诱饵给出更高的真实环覆盖；
- 核正确地排名诱饵，而不是外壳中心。

因此需要重新设计一个“外壳中心是真实最优”的图，才能验证核补盲区。
当前这轮证明了：**方形角落可见性使外壳陷阱不再构成真实质量盲区。**
