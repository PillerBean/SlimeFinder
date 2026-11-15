# Phase 8 SearchTask/GUI 兼容性验收

> 分支：`refactor-compute-core`
> 只更新副分支源代码；没有安装包、没有 Release、没有改主分支。

## 1. Banded 新路径

`SLIME_NEXT_PIPELINE=1`，Bedrock 密度 256²，完整缓存，band=64：

```text
done=1
ok=1
progress=1
final=1.000
liveMatches=20
liveScanned=71824
liveTotal=71824
results=20 / totalFound=20
SAME
```

说明：

- SearchTask 能走新的 Banded 计划；
- 进度回调至少触发一次并最终到 1.0；
- liveMatches 在完成后与 `totalFound` 一致；
- liveScanned/liveTotal 在完成后同口径，不会出现 >100%；
- Banded 的 totalFound 按旧 `runBanded` 语义等于最终结果数；
- 结果与旧 CPU 无缓存路径一致。

## 2. 完整缓存覆盖回退

`useCache=true`、`cache` 直接给完整缓存、但没有 `cacheBandLoader`：

```text
full-cache-fallback: done=1 ok=1 SAME
```

当前原型不接管“完整缓存命中”计划，SearchTask 自动回退旧引擎，行为不变。

## 3. 构建模式隔离

`buildMode=true`、`useCache=true`、`SLIME_NEXT_PIPELINE=1`：

```text
build-isolation: done=1 ok=1 entries=1744
```

构建模式仍走旧引擎，没有被新 Pipeline 误接管；
同时验证了旧路径确实把条目写进 CacheManager：

```text
build-content: expected=1744 actual=1744
```

缓存条目数与独立标量判定完全一致。

## 4. Fused 超大范围进度

为了不牺牲常见范围性能，Fused GPU 按锚点行数分带：

```text
锚点行 <= 1024：单次 GPU 调用
1024 < 锚点行 <= 8192：2 band，给一次中间进度/取消点
锚点行 > 8192：最多 4 band
```

验证：

```text
fused-large-progress:
  ok=1
  progressCount=6
  final=1.000
  SAME
```

注意：常见 512² 范围仍是一次 GPU 调用；2048² 有 2 band。

## 5. Fused 分带取消

在超大范围 band 之间取消：

```text
fused-cancel: ok=0 cancelled=1
```

说明 Fused 新路径至少在大范围分带时可以在 band 边界响应取消。

## 6. Fused SearchTask 2048²

```text
fused-searchtask:
  done=1
  progress=5
  final=1.000
  live=4194304/4194304
  SAME

fused-searchtask-cancel:
  done=1
  cancelled=1
```

说明：

- 2048² Fused 走 2 band，SearchTask 能收到中间进度；
- 完成时 liveScanned == liveTotal == 区域面积；
- 中间取消能让 SearchTask 返回 cancelled。

## 7. 环形/AFK 隔离

`AfkPlanner::planForSeed` 在 feature flag 关闭/开启下各跑一次：

```text
afk-isolation: candidates=3/3 SAME
```

说明环境变量不影响环形/AFK 规划；新 Pipeline 没有接管该路径。

## 8. 仍未覆盖

- Fused 512² 只有开始/结束进度，没有中间点（调用本身约 1ms）；
- Fused 常见范围取消只能在 GPU 调用返回后生效；
- GUI 的实时扫描计数在 Fused 路径上是按进度估算/结束后校正。

## 9. 结论

```text
Banded 的 SearchTask 进度/结果/liveMatches/liveScanned 兼容性通过
完整缓存/构建模式回退隔离通过
构建模式缓存条目数与独立判定一致
Fused 2048² SearchTask 中间进度/取消/live 计数通过
Fused 超大范围分带进度/取消通过
环形/AFK 隔离通过
Fused 512² 细粒度进度仍是可接受的短任务缺口
```

因此仍不切换默认路径。
