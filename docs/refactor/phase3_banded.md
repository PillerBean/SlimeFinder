# Phase 3 分带（Banded）对拍记录

> 分支：`refactor-compute-core`
> 只更新副分支源代码；没有安装包、没有 Release、没有改主分支。

## 1. 实现

新增：

```text
MaterializePipeline::runBanded
```

行为：

```text
按锚点 x/z 分成 2D tile
每个 tile 通过 cacheBandLoader 请求自己的数据矩形
若缓存完整覆盖该数据矩形：
    直接用 CacheManager 位图填充 Bitmap
否则若缓存有部分数据：
    已覆盖点用 cache.lookup，未覆盖点用标量判定补齐
否则：
    CpuDeterminer 扫描整个数据矩形
匹配只处理本 tile 拥有的锚点行/列
结果合并到全局 Collector
rank 仍使用全局锚点原点/行数
```

当前部分缓存填充：

```text
按行合并 cache.ranges() 的覆盖区间
覆盖区间用 cache.isSlime
未覆盖区间用 fillSlimeRowJava/Bedrock 整段填充
```

这比逐点标量补齐快，但仍不是按缓存位图字级拷贝的最终实现。

## 2. 对拍

区域：

```text
(-300,-300)+600×600
target=50
threads=1
bandRows=64 / 100
```

缓存构造：

- 完整缓存：整幅 Bitmap → `CacheManager::addPackedWords`；
- 部分缓存：只建中央 [-120,120]×[-120,120] 的子区域。

| case | band | 完整缓存 | 部分缓存 | 无缓存 | 旧CPU |
|---|---:|---|---|---|---|
| Java pattern | 64 | SAME | SAME | SAME | SAME |
| Java pattern | 100 | SAME | — | SAME | SAME |
| Java density | 64 | SAME | SAME | SAME | SAME |
| Java density | 100 | SAME | — | SAME | SAME |
| Bedrock pattern | 64 | SAME | SAME | SAME | SAME |
| Bedrock pattern | 100 | SAME | — | SAME | SAME |
| Bedrock density | 64 | SAME | SAME | SAME | SAME |
| Bedrock density | 100 | SAME | — | SAME | SAME |

有完整缓存时统计显示 `backend=cache+banded`；
部分缓存为 `cache-partial+banded`；无缓存为 `cpu-banded`。

## 3. 当前限制

- 部分缓存逐点补齐，未做按 range 的快速混合填充；
- tile 目前串行；
- 缓存写入/构建未接入；
- 进度是 tile 级粗粒度；
- 未接 GPU fused。

## 4. 下一步

FusedGpuPattern / FusedGpuDensity 已单独实现并对拍，见 `phase4_fused.md`。
