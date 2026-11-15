# Phase 6 大范围结果与耗时记录

> 分支：`refactor-compute-core`
> 只更新副分支源代码；没有安装包、没有 Release、没有改主分支。

## 1. 用例

```text
8 线程
target=50
Java pattern 2048×2048
Java density 2048×2048
Bedrock density 1024×1024（GPU fused）
Bedrock density 1500×1500（强制 Chunked，无 GPU）
```

旧路径：`SlimeEngine::run` CPU。
新路径：`Planner -> PlanExecutor`。

## 2. 结果

| case | 计划 | 旧 | 新 | 新/旧 | 结果 |
|---|---|---:|---:|---:|---|
| Java-pattern-2048 | FusedGpuPattern | 2.9 ms | 1.7 ms | 0.59× | SAME |
| Java-density-2048 | FusedGpuDensity | 2.3 ms | 2.5 ms | 1.05× | SAME |
| Bedrock-density-1024 | FusedGpuDensity | 6.0 ms | 1.6 ms | 0.27× | SAME |
| Bedrock-density-chunk | Chunked | 40.3 ms | 9.9 ms | 0.24× | SAME |

所有结果、totalFound 一致。

Java density 2048² 在加入 2-band 中间进度后接近 1.05×，
达到“不退化超过 5%”的边界；不同运行会有小幅波动。

## 3. 冷启动修正

第一次测量 Java pattern 时得到：

```text
old=3.0ms new=4.0ms ratio=1.34x
```

其中包含 OpenCL 首次初始化/编译。加入预热后：

```text
old=2.7ms new=1.0ms ratio=0.37x
```

Phase7 单独拆分：

```text
old-CPU              median 1.76ms
new-CPU-materialize  median 4.38ms
new-CPU-chunked      median 4.59ms
new-fused-GPU        median 0.41ms
```

结论：

- Fused GPU 预热后明显快于旧 CPU；
- **新 CPU Materialize/Chunked 仍比旧 CPU 慢**；
- 因此 SearchTask feature flag 只开放 Fused GPU 或 Banded，
  CPU Materialize/Chunked 仍留在测试工具里，不跟着 flag 进产品。

## 4. 结论

- 大范围结果正确性通过；
- GPU fused 计划在这组测试中不退化且更快；
- CPU 新路径未达到性能门槛，已从生产 flag 的自动选择中排除；
- 这组数据只说明当前机器/当前参数，不构成最终产品性能承诺。

## 5. 切换结论

```text
不切换默认路径
SLIME_NEXT_PIPELINE 仍默认关闭
```

性能门槛对“已开放的 Fused/Banded”基本满足；全量切换仍缺 GUI/缓存构建/环形隔离验收。

## 6. 下一步

1. 继续验收 Banded 部分缓存性能（当前逐点补齐）；
2. 缓存构建/环形明确继续旧路径；
3. GUI 进度/取消/统计兼容性；
4. 再决定是否切换默认。

