# Phase 3 分块（Chunked）对拍记录

> 分支：`refactor-compute-core`
> 只更新副分支源代码；没有安装包、没有 Release、没有改主分支。

## 1. 实现

`MaterializePipeline::runChunked`：

```text
global anchor z range = [minZ, maxZ - height + 1]
每个 chunk 拥有自己的 anchor 行 [a0, a1]
扫描范围 = [a0, a1 + height - 1]
匹配只处理 [a0, a1]
结果合并到全局 Collector
```

其中：

- `height = pattern.rows()`（严格图案）或 `gridSize`（密度）；
- 每块只扫描自己需要的 halo 行；
- 块之间不重复拥有 anchor 行，因此不会重复结果；
- `MatchLimits` 显式限制匹配锚点行，但 `rank` 仍使用全局锚点行数和全局原点。

## 2. 对拍

区域：

```text
(-300,-300)+600×600
target=50
threads=1
chunkRows=37 / 100
```

| case | chunk | 旧路径 | 新分块 | 结果 |
|---|---:|---|---|---|
| Java pattern | 37 | 34/34 | 34/34 | SAME |
| Java pattern | 100 | 34/34 | 34/34 | SAME |
| Java density | 37 | 50/785 | 50/785 | SAME |
| Java density | 100 | 50/785 | 50/785 | SAME |
| Bedrock pattern | 37 | 39/39 | 39/39 | SAME |
| Bedrock pattern | 100 | 39/39 | 39/39 | SAME |
| Bedrock density | 37 | 50/903 | 50/903 | SAME |
| Bedrock density | 100 | 50/903 | 50/903 | SAME |

结果坐标、数量、totalFound、排序全部一致。

注意：这里的新分块路径仍使用 `strictRowAvx2` / `densityWindowCountsAvx2`，不是朴素 matcher。

## 3. 当前限制

- 分块目前**串行**执行；并发/流水线是下一步；
- 进度是 chunk 级粗粒度，仍把块处理标记在扫描阶段；
- 还没有分带（Banded）的缓存加载/合并；
- 还没有缓存、GPU fused、smart/stop/best、旋转图案。

## 4. 下一步

1. 分块并发 + 取消/进度细化；
2. 分带（Banded）读取 CacheManager；
3. 再接 FusedGpuPattern / FusedGpuDensity。
