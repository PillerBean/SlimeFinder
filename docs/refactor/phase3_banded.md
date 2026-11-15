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
按锚点 z 行分带
每个 band 通过 cacheBandLoader 请求 [minX,maxX] × [a0,a1+height-1] 的缓存
若缓存完整覆盖该数据矩形：
    直接用 CacheManager 位图填充 Bitmap
否则：
    CpuDeterminer 扫描整个数据矩形（不用部分缓存）
匹配只处理本 band 拥有的锚点行
结果合并到全局 Collector
rank 仍使用全局锚点原点/行数
```

重要限制：当前是**完整 band 缓存才用缓存**；部分缓存会保守地整带重扫，
不会错用缺失位，但还没有做 partial-cache 混合填充。

## 2. 对拍

区域：

```text
(-300,-300)+600×600
target=50
threads=1
bandRows=64 / 100
```

缓存构造：先用新 `CpuDeterminer` 生成整幅 Bitmap，再 `CacheManager::addPackedWords` 建完整缓存。

| case | band | 有缓存 | 无缓存 | 旧CPU |
|---|---:|---|---|---|
| Java pattern | 64 | SAME | SAME | SAME |
| Java pattern | 100 | SAME | SAME | SAME |
| Java density | 64 | SAME | SAME | SAME |
| Java density | 100 | SAME | SAME | SAME |
| Bedrock pattern | 64 | SAME | SAME | SAME |
| Bedrock pattern | 100 | SAME | SAME | SAME |
| Bedrock density | 64 | SAME | SAME | SAME |
| Bedrock density | 100 | SAME | SAME | SAME |

有缓存时统计显示 `backend=cache+banded`；无缓存为 `cpu-banded`。

## 3. 当前限制

- 部分缓存尚未混合填充；
- 分带是 z 方向，不是旧版 2D tile；
- 分块/分带目前串行；
- 缓存写入/构建未接入；
- 进度是 band 级粗粒度；
- 未接 GPU fused。

## 4. 下一步

FusedGpuPattern / FusedGpuDensity 已单独实现并对拍，见 `phase4_fused.md`。
