# Phase 9 Banded 全缓存性能

> 分支：`refactor-compute-core`
> 只更新副分支源代码；没有安装包、没有 Release、没有改主分支。

## 1. 场景

```text
Bedrock density 1024×1024
target=50
8 线程
完整缓存
cacheBandSize=256
```

对比：

```text
旧 SlimeEngine::runBanded
新 MaterializePipeline::runBanded
```

## 2. 结果

```text
old=20.49ms
new=10.02ms
ratio=0.49×
SAME
```

新 Banded 全缓存路径在这组测试中约为旧路径的一半耗时。

`totalFound` 已对齐旧 `runBanded` 语义：

```text
old=50/50
new=50/50
```

原始命中窗口数仍保留在 `match.totalFound`，但 `SearchOutcome.totalFound` 报告最终显示结果数。

## 3. 注意

- 这是单次测量，不是统计中位数；
- 完整缓存路径才走 cache-filled bitmap；
- 部分缓存路径仍是混合填充；
- 不代表所有 tile/线程数下的性能。

## 4. 当前结论

```text
Banded 全缓存：结果一致，实测更快
Banded 部分缓存：结果一致，混合填充
仍然不切换默认路径
```
