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

## 4. 尚未完成

```text
CacheSnapshot（不可变缓存视图）
GPU 失败从已完成 tile 续扫
```

这两项仍是第 2 轮剩余工作。

## 5. 回归

```text
slime_tests --unit                通过
slime_search_job_tests            ALL PASS
slime_cache_store_tests           OK
slime_cache_band_tests            OK
GUI 自检（GPU off）               exit=0
slime_ocl_probe                   PASS
```
