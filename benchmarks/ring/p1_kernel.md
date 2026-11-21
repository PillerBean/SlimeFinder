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

## 2026-11-21 15:56 快速方形路径复测

```text
grid=15, minDensity=0.25, topN=1
prescanSeeds=600
prescan_ms=222228.8
total_ms=223880.2
result=14739 / 14595.893
```

结果复现，但总时间 223.88s，仍未 ≤210s。

说明：

- threshold 0.25 之前曾测到 203s，当前 GPU 状态下不稳定；
- 预筛仍是主要成本，约 222s；
- 不能靠阈值/种子数把预筛压到 210s 以下。
- 下一阶段必须改 GPU 预筛的扫描分辨率/核，而不是继续调 threshold。

验收 A 仍未达。

## 2026-11-21 16:22 质量优先检查：种子截断会漏参考

P1 核在 3 个种子上的质量结果：

```text
newseed：top1 复现 14739 / 14595.893
seed0：top1 = 14155，未复现 14387；核 top5 也未复现
seed12345：top1 = 14311 / 13822.542，超过原暂定 14016
```

seed0 的关键发现：

- 参考点所在 chunk 的中心没有出现在本次 4000 个 prescan seed 中；
- 环形核只能重排 seed，不能恢复被预筛截断的候选；
- 所以 seed0 的 P0 参考点是**手工注入池**，不是当前 seed 集自然召回的；
- 这验证了“更快/更少种子可能只是漏搜索”，不能作为质量进步。

seed12345 的 top1 是真实质量提升：

```text
14016 → 14311 blocks
13878.871 → 13822.542 weight（blocks 提升，weight 略低）
```

结论：

```text
P1 质量门未通过（seed0 漏参考）
下一步先修 prescan 种子选择的确定性/覆盖，再谈时间
```

## 2026-11-21 16:27 质量修正：3×3 抛光

之前 top1/top5 只做“种子内 256 子偏移”，漏掉了 planner 的 3×3 区块抛光。
把核筛选后的 seed 交回 `AfkPlanner::plan` 后：

```text
seed=0, kernel top5 + planner polish:
  blocks 14387
  center 3472965,-13446988
  plan 2.4ms
  总耗时 231.85s（预筛仍占绝大部分）
```

所以：

```text
环形核 top1 单点评估会漏目标
环形核 top5 + 3×3 抛光可以复现 seed0 参考
```

P1 质量门修正为：

| seed | 核 top-K + 抛光 | 参考 | 结果 |
|---|---|---|---|
| newseed | top1 | 14739 | 复现 |
| seed0 | top5 | 14387 | 复现 |
| seed12345 | top1 | 14016 → 14311 | 提升 |

时间没有改善：预筛约 230s，仍是主要成本。
按新策略，时间优化放到质量门之后。
