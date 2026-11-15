# Phase 5 Planner/Feature Flag/随机矩阵验收

> 分支：`refactor-compute-core`
> 只更新副分支源代码；没有安装包、没有 Release、没有改主分支。

## 1. Planner 选择四类计划

`PlanExecutor` 已按 `ExecutionPlan` 分派：

| 计划 | 条件 | 执行器 |
|---|---|---|
| Materialize Search | Search + CPU matcher + 单块 | `MaterializePipeline::run` |
| Chunked Search | Search + `PartitionKind::Chunked` | `runChunked` |
| Banded | `PlanFamily::Banded` + `cacheBandLoader` | `runBanded` |
| Fused GPU | matcher = `FusedGpuPattern/Density` | `FusedGpuPipeline` |

显式覆盖测试：

```text
explicit-materialize       plan=Search  matcher=CpuDensity   part=Single   SAME
explicit-chunked           plan=Search  matcher=CpuDensity   part=Chunked  SAME
explicit-fused-density     plan=Search  matcher=FusedGpuDensity           SAME
explicit-fused-pattern     plan=Search  matcher=FusedGpuPattern           SAME
explicit-banded            plan=Banded  matcher=CpuDensity   part=Banded   SAME
```

## 2. SearchTask feature flag

新增环境变量：

```text
SLIME_NEXT_PIPELINE=1
```

默认：

```text
未设置 / 0 / false / off
→ 走旧 SlimeEngine 路径
```

开启后：

```text
SearchTask
  → makeExecutionPlan
  → PlanExecutor::supported
  → 支持的走新计划，不支持的自动回退旧引擎
```

**默认仍关闭**；产品界面没有开关，当前只是开发验收用环境变量。

### 验证

```text
feature-on:
  done=1 ok=1 results=30/586 gpuMatch=1 chunked=0 total=3.12ms SAME

feature-off:
  done=1 ok=1 results=30/586 SAME
```

feature-on 明确显示 `gpuMatch=1`，说明确实走了 FusedGpu 计划；
feature-off 与旧 CPU 结果一致。

## 3. 随机参数矩阵

工具：`tools/refactor/phase5_plan_matrix.cpp`

覆盖：

```text
Java / Bedrock
图案 / 密度
范围 128~512
随机种子/起点/target
有/无 GPU
强制 Chunked 的 classicChunkLimit
Banded 完整缓存显式 case
```

4 个 cycle × 8 个随机 case：

```text
RANDOM-MATRIX cases=32 failures=0
PHASE5-PLAN-MATRIX ALL PASS (0 failures)
```

连续运行 4 次该工具（共 16 个 cycle、128 个随机 case）：

```text
matrix-cycle=1 rc=0 PHASE5-PLAN-MATRIX ALL PASS (0 failures)
matrix-cycle=2 rc=0 PHASE5-PLAN-MATRIX ALL PASS (0 failures)
matrix-cycle=3 rc=0 PHASE5-PLAN-MATRIX ALL PASS (0 failures)
matrix-cycle=4 rc=0 PHASE5-PLAN-MATRIX ALL PASS (0 failures)
```

补充全量回归：

```text
PHASE2 ALL PASS
PHASE2-THREADS ALL PASS
PROGRESS-CANCEL ALL PASS
PHASE3-CHUNKED ALL PASS
PHASE3-BANDED ALL PASS
PHASE4-FUSED ALL PASS
FEATURE-FLAG ALL PASS
PARITY ALL PASS
Planner ALL PASS
Collector ALL PASS
```

## 4. 验收门槛（建议）

切换默认前必须全部满足：

```text
1. explicit + random matrix 连续 4 轮全 PASS
2. Banded 部分缓存/2D tile 语义覆盖
3. 缓存构建/环形/AFK 明确继续走旧路径，不误入新路径
4. 大范围（≥ 1M 区块）结果与旧路径一致
5. 旧/新性能对比不退化超过 5%
6. 进度/取消/统计/GUI 兼容性验收
7. feature flag 可随时关闭并回退
```

当前只完成第 1 项的核心部分；**还不满足全量切换条件**。

补充（Phase 6）：

```text
Banded 2D tile / 部分缓存：已完成并 SAME
≥1M 区块大范围：结果 SAME
性能：Java pattern 2048 新/旧 = 1.34×，未达到 ≤1.05× 门槛
```

详见 `phase6_large_perf.md`。因此仍不切换默认。


## 5. 明确回退

```text
关闭 SLIME_NEXT_PIPELINE
→ 立即回到旧 SlimeEngine
```

不会自动删除旧路径，也不会在默认关闭时改变产品行为。

## 6. 当前状态

```text
分支：refactor-compute-core
主分支：未改动
正式版：3.12.3（Latest）
3.13.7：测试版（Pre-release）
安装包：未生成
Release：未创建
```
