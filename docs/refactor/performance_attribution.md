# 旧版为什么“看起来慢”：路径归因

日期：2026-11-15
机器：Windows / MSVC Release / 8 线程 / `gfx1031`
target=50，预热 1 次，5 次中位数

## 1. 直接测量

| 场景 | 旧 CPU | 旧 CPU + 反馈 | 旧 GPU 路径 | 旧 GPU 标记 |
|---|---:|---:|---:|---|
| Java pattern 2048² | 2.09 ms | 1.94 ms | 1.91 ms | scan=0 match=0 |
| Java density 2048² | 2.36 ms | 2.39 ms | **0.84 ms** | scan=0 match=1 |
| Bedrock density 1024² | 9.09 ms | 8.23 ms | **1.39 ms** | scan=0 match=1 |

反馈开销：

```text
Java pattern   -7.2%（噪声范围）
Java density   +1.3%
Bedrock density -9.4%（噪声范围）
```

## 2. 结论

### 反馈不是主因

打开：

```text
liveMatches / liveScanned / liveTotal
ProgressFn 回调
```

没有出现稳定的、数量级级别的变慢；差异基本在测量噪声内。

所以“进度/实时计数/取消把性能吃掉了”**不是主要解释**。

### 旧版并不总是慢

Java 密度：

```text
旧 GPU 0.84 ms
新 Fused 2.23 ms
```

旧版反而快约 2.6 倍。

Bedrock 密度：

```text
旧 GPU 1.39 ms
新 Fused 1.43 ms
```

基本持平。

### 新路径真正赢的地方

Java pattern：

```text
旧 CPU 1.91 ms
新 Fused 0.62 ms
```

这是新计划把 GPU 图案融合用起来的收益。

## 3. 之前表格的偏差

Phase12 的“旧路径”是：

```text
旧引擎 + gpuScan=nullptr（强制 CPU）
```

而新路径很多是：

```text
新 Fused GPU
```

所以“新/旧 0.21×”里，很大一部分是 **GPU vs CPU**，
不是“重构架构 vs 旧架构”。

必须区分三类对比：

```text
旧 CPU vs 新 CPU       → 新 CPU 目前更慢
旧 CPU vs 新 GPU       → 新 GPU 更快（但这是路径不同）
旧 GPU vs 新 GPU       → 才是同硬件路径的真实对比
```

## 4. 当前真实结论

```text
新 CPU Materialize/Chunked：比旧 CPU 慢，不开放
Java pattern Fused：比旧 GPU/CPU 路径快
Java density Fused：比旧 GPU 路径慢
Bedrock density Fused：与旧 GPU 路径基本持平
Banded：比旧 runBanded 快，但仍是独立计划差异
```

不能据此说“重构收益很高”；只能说：

> 新框架在部分计划上选到了更合适的执行路径，
> 但在同硬件路径上并没有全面胜出。

## 5. 下一步应该做的性能验收

```text
1. 旧 GPU vs 新 Fused，同范围/同 target
2. 旧 CPU vs 新 CPU，分离物化/匹配开销
3. 反馈开关的长任务大样本测试
4. 解释 Java density 新路径 2-band 的固定成本
```
