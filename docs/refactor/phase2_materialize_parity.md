# Phase 2 Materialize SearchPlan 对拍（2026-11-15）

> 分支：`refactor-compute-core`
> 只更新副分支源代码；没有安装包、没有 Release、没有改主分支。

## 1. 实现范围

新增：

```text
src/core/next/Matcher.h/.cpp
src/core/next/Pipeline.h/.cpp
```

当前只支持：

```text
CPU 判定（CpuDeterminer）
CPU 精确匹配（CpuExactMatcher）
严格图案 + 方向锁定
精确密度
DistanceTopN Collector
无线程>1、无缓存、无 GPU、无 smart/stop/ignoreThreshold
```

不支持的输入会返回错误，不会静默给近似结果。

## 2. 对拍对象

旧路径：

```text
SlimeEngine::run(...)
GPU=null
useCache=false
algorithm=Exact
threads=1
targetCount=50
```

新路径：

```text
MaterializePipeline::run(...)
CpuDeterminer -> Bitmap -> CpuExactMatcher -> ResultCollector
```

区域：

```text
(-256,-256)+512×512
Java/Bedrock
图案：2×2 全绿，方向锁定
密度：g=5，阈值0.3
```

## 3. 结果

| case | 旧路径 | 新路径 | 命中/总数 | 排序 |
|---|---|---|---|---|
| Java 图案 | 24 条 / 24 | 24 条 / 24 | SAME | SAME |
| Java 密度 | 50 条 / 586 | 50 条 / 586 | SAME | SAME |
| Bedrock 图案 | 33 条 / 33 | 33 条 / 33 | SAME | SAME |
| Bedrock 密度 | 50 条 / 647 | 50 条 / 647 | SAME | SAME |

四个 case 的：

```text
结果坐标
结果数量
totalFound
density
排序
```

全部一致。

连续四轮：

```text
cycle=1 PHASE2 ALL PASS (0 failures)
cycle=2 PHASE2 ALL PASS (0 failures)
cycle=3 PHASE2 ALL PASS (0 failures)
cycle=4 PHASE2 ALL PASS (0 failures)
```

## 4. 性能说明

本阶段的新 Matcher 是**朴素实现**，用于验证数据流和语义，不是性能实现：

```text
Java 密度：
  旧路径约 0.46 ms
  新原型约 7.31 ms
```

这是预期的：Phase 2 先证明“新计划能产出与旧引擎一致的结果”，
下一步才把 FastMatch 的滑动窗口/位运算内核接进 Matcher。

不能拿当前原型的耗时判断重构成败。

## 5. 未覆盖

- 分块/分带；
- 缓存读取/构建；
- GPU fused；
- 智能范围/提前停止；
- 旋转图案；
- 分层/采样/保守；
- 取消/进度/统计；
- 多线程/内存预算。

这些按计划在 Phase 3/4 接入。

## 6. 当前状态

```text
副分支：refactor-compute-core
主分支：未改动
正式版：3.12.3（Latest）
3.13.7：测试版（Pre-release）
安装包：未生成
Release：未创建
```
