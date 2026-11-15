# Phase 4 FusedGpuPattern / FusedGpuDensity 对拍记录

> 分支：`refactor-compute-core`
> 只更新副分支源代码；没有安装包、没有 Release、没有改主分支。

## 1. 实现

新增：

```text
src/core/next/FusedGpu.h/.cpp
```

直接调用：

```text
GpuScan::matchPattern
GpuScan::matchDensity
```

不经过 Bitmap，结果写入公共 `ResultCollector`。

支持：

```text
严格图案、方向锁定
精确密度
无线缓存/构建/停止/智能范围
```

## 2. 对拍

区域：

```text
(-256,-256)+512×512
Java / Bedrock
pattern 2×2 全绿
density g=5 threshold=0.3
target=50
```

旧路径：`SlimeEngine::run` CPU 精确。

| case | 旧路径 | FusedGPU | 结果 |
|---|---|---|---|
| Java pattern | 24/24 | 24/24 | SAME |
| Java density | 50/586 | 50/586 | SAME |
| Bedrock pattern | 33/33 | 33/33 | SAME |
| Bedrock density | 50/647 | 50/647 | SAME |

结果坐标、数量、totalFound、排序一致。

## 3. 当前限制

- 图案只支持方向锁定、单变体；
- 密度只支持精确模式；
- 未接 stopAtTarget / smartRange / best-density；
- 未接缓存；
- 未接分块/分带；
- 进度/取消目前依赖底层 GPU 调用，尚无统一阶段进度；
- 大范围仍需后续并发/内存预算。

## 4. 测试汇总

```text
PHASE3-BANDED ALL PASS
PHASE4-FUSED  ALL PASS
```

## 5. 下一步

把四类计划接到 Planner/ExecutionPlan 选择器，再考虑 SearchTask feature flag。
仍不打包、不发布。
