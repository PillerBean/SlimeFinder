# 流程架构第 2 轮（进行中）

分支：`flow-runtime`，基于 3.13.8。

## 1. Banded 全局早停

原实现：

```text
bandOpt.stopAtTarget = false
bandOpt.smartRange = false
最后才截断 targetCount
```

现在：

- `stopAtTarget` / `smartRange` 时按 band 到起点的距离排序处理；
- 每处理完一个 band，从全局合并结果取 TopN；
- 当 `worstDist + margin <= 下一 band 的距离下界` 时停止；
- 结果仍与完整扫描的最近 N 一致。

验证（Bedrock strict 2x2，512²，target=3）：

```text
完整 Banded：1568.6 ms，扫描 1,240,996
提前停止：    99.0 ms，扫描    78,400
结果：3 条，完全一致
```

加速约 **15.8×**，扫描量约 **6.3%**。

测试：`tests/integration/cache_band_test.cpp` 的 `banded-stop`。

## 2. Pack 解码 LRU

新增 `CacheStore` 内部 LRU：

```text
SLIME_PACK_CACHE_MB=128  # 默认 128MB
SLIME_PACK_CACHE_MB=0    # 关闭（A/B）
```

相邻 band 常命中同一 pack；缓存的是**已解压的 pack 位图**，
避免每次 `fillCacheManagerRange` 重新 zstd 解压。

直接 benchmark（`cache-build/局部`，256 packs，band 2048，100 次）：

```text
cache=0    3.24~3.38 ms/次
cache=128  0.66~0.76 ms/次
约 4.5~4.9×
```

工具：`tools/bench/cache_lru_bench.cpp`。

端到端 Banded 的收益取决于扫描/匹配占比；冷缓存与热缓存差异很大，
不要把它当成固定 4.5×。

## 3. GPU cap 溢出标记

新增：

```text
SearchOutcome::truncated
SearchReport::truncated
DensityMatchParams::outTruncated
PatternMatchParams::outTruncated
GpuSession::patternStream / matchDensityStream 的 outTruncated
```

行为：

- GPU 命中数超过 `maxHits` 时置位；
- 当前仍返回 false，让引擎回退 CPU，保证结果完整；
- 标志让上层知道发生了 cap 溢出，而不是只看到普通回退。

## 4. GPU 扫描失败后的行续扫

`runGpuScanStage` 现在维护：

```text
completedRows[z] = 1  当该 z 行的所有 x tile 都已集成
```

GPU 扫描失败回退 CPU 时，`scanTask` 跳过这些完整行，只补算剩余行。

新增诊断字段：

```text
SearchOutcome::gpuRowsResumed
SearchReport::gpuRowsResumed
```

测试：

```text
tests/integration/gpu_resume_test.cpp
```

注入“前两个 tile 成功、第三个失败”：

```text
CPU 参考 50 条
GPU 失败回退 50 条
结果 SAME
gpuRowsResumed = 4096
GPU-RESUME OK
```

注意：测试中的 fake GPU 每 tile 用 CPU 填充，因此总耗时不能代表真实 GPU；
这里验证的是“已集成行没有被 CPU 重算”。

## 5. 尚未完成

```text
CacheSnapshot（不可变缓存视图）
```

这是第 2 轮剩余工作。

## 6. 全链路速度复测

任务层（同引擎、CPU、GPU 关闭，15 样本中位数、奇偶交替顺序）：

| case | legacy | flow | ratio |
|---|---:|---:|---:|
| Java pattern 2048 | 0.84 ms | 0.81 ms | 0.97× |
| Java density 2048 | 1.25 ms | 1.24 ms | 0.99× |
| Bedrock density 1024 | 0.81 ms | 0.81 ms | 0.99× |
| Bedrock density 2048 | 1.41 ms | 1.51 ms | 1.07× |

结论：

```text
任务层整体仍在 ±7% 噪声内
没有可测的流程退化
```

Banded / 缓存收益见第 1、2、4 节：

```text
Banded 全局早停：约 16×（稀疏目标、缓存完整覆盖）
Pack 解码 LRU：约 5×（直接 fill 基准）
```

## 7. 回归

```text
slime_tests --unit                通过
slime_search_job_tests            ALL PASS
slime_cache_store_tests           OK
slime_cache_band_tests            OK
slime_gpu_resume_tests            GPU-RESUME OK
slime_ocl_probe                   PASS
GUI 自检（GPU off）               exit=0
```
