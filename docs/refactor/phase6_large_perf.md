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
| Java-pattern-2048 | FusedGpuPattern | 3.0 ms | 4.0 ms | 1.34× | SAME |
| Java-density-2048 | FusedGpuDensity | 2.3 ms | 2.4 ms | 1.04× | SAME |
| Bedrock-density-1024 | FusedGpuDensity | 5.9 ms | 2.0 ms | 0.34× | SAME |
| Bedrock-density-chunk | Chunked | 40.2 ms | 9.5 ms | 0.23× | SAME |

所有结果、totalFound 一致。

## 3. 结论

- 大范围结果正确性通过；
- Bedrock GPU/Chunked 在这组测试中明显更快；
- **Java pattern 2048 仍比旧 CPU 慢 34%**，不满足“性能不退化 ≤5%”的切换门槛；
- 这组数据只说明当前机器/当前参数，不构成产品性能验收。

## 4. 切换结论

```text
不切换默认路径
SLIME_NEXT_PIPELINE 仍默认关闭
```

必须先解决 Java pattern 的固定开销/路径选择，再谈切换。

## 5. 下一步

1. 查 Java pattern 2048 的 1.34×：GPU 启动/回读/Collector 还是计划选择不合适；
2. 增加 CPU pattern 大范围对比；
3. 部分缓存快速混合填充；
4. 更新验收门槛记录。
