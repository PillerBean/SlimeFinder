# 新分支速度状态（Phase 12 统一采样）

日期：2026-11-15
机器：Windows / MSVC Release / OpenMP
GPU：`gfx1031`
线程：8
target：50
采样：预热 1 次，计时 5 次，取中位数
开关：`SLIME_NEXT_PIPELINE=1`（默认关闭）

> 旧路径均为 `SlimeEngine` CPU（`gpuScan=nullptr`）。
> 这是“新分支路径 vs 旧 CPU 路径”，不是“新 GPU vs 旧 GPU”。
>
> **重要修正：不能把这张表直接当作“重构收益”。**
> 旧引擎的生产 GPU 路径更快；见 `performance_attribution.md`。

## 1. 总表

| 场景 | 旧路径 | 新路径 | 新/旧 | 结果 |
|---|---:|---:|---:|---|
| Java pattern 2048² | 1.89 ms | 0.62 ms | **0.33×** | SAME |
| Java density 2048² | 2.27 ms | 2.23 ms | **0.98×** | SAME |
| Bedrock density 1024² | 6.67 ms | 1.43 ms | **0.21×** | SAME |
| Bedrock chunked 1500² | 39.87 ms | 9.07 ms | **0.23×** | SAME |
| Bedrock banded 1024² 全缓存 | 20.53 ms | 7.95 ms | **0.39×** | SAME |

所有 case 的结果坐标、target 数量、`totalFound` 一致。

## 2. 旧 GPU 路径对照

旧引擎在生产中并不是只能用 CPU。同机实测：

| 场景 | 旧 CPU | 旧 GPU 路径 | 新 Fused |
|---|---:|---:|---:|
| Java pattern 2048² | 2.09 ms | 1.91 ms | **0.62 ms** |
| Java density 2048² | 2.36 ms | **0.84 ms** | 2.23 ms |
| Bedrock density 1024² | 9.09 ms | **1.39 ms** | 1.43 ms |

结论：

```text
Java pattern：新 Fused 确实更快
Java density：新 Fused 比旧 GPU 慢约 2.6×
Bedrock density：新旧 GPU 基本持平
```

反馈开关测试的波动在噪声范围内（约 -9%~+1%），
没有证据表明“进度/实时计数”是旧版慢的主因。
详见 `performance_attribution.md`。

## 2. 对照：新 CPU Materialize

同一 2048² Java pattern：

```text
旧 CPU              1.89 ms
新 CPU Materialize  4.71 ms
```

新 CPU 物化路径仍慢约 2.5 倍，所以：

```text
SearchTask feature flag 不开放 CPU Materialize/Chunked
只开放 FusedGpu 和 Banded
```

## 3. 怎么解读

### 明显更快

```text
Bedrock Fused GPU        0.21×
Bedrock Chunked          0.23×
Banded 全缓存            0.39×
Java pattern Fused       0.33×
```

### 基本持平

```text
Java density Fused       0.98×
```

2048² Java 密度是 2-band Fused；GPU dispatch 与回读占比不低，
所以优势远小于 Bedrock 密度。

### 明显更慢

```text
新 CPU Materialize       约 2.5× 旧 CPU
```

这是当前 CPU 新路径不进入产品 feature flag 的原因。

## 4. 速度边界

当前数据只覆盖：

```text
2048² 及以下
8 线程
target=50
无缓存构建
Banded 只测完整缓存
```

不能直接外推到：

```text
超大范围（>10k²）
低线程数
缓存构建
部分缓存大范围
GPU 显存边界
```

小毫秒级数据有噪声；上表使用 5 次中位数，但 Java density 仍可能落在 0.95~1.05×。

## 5. 当前结论

```text
Java pattern Fused：比旧路径快，收益明确
Java density Fused：比旧 GPU 路径慢，不能算收益
Bedrock density Fused：与旧 GPU 基本持平
Banded：比旧 runBanded 快，但属于独立计划差异
CPU Materialize/Chunked：结果正确但更慢，不开放
默认路径：未切换
```

因此不能据此说“重构收益很高”；更准确的说法是：

> 新框架在部分计划上选到了更合适的执行路径，
> 但同类路径对比并没有全面胜出。

## 6. 复现

```text
build\slime_next_phase12_speed.exe
```

源码：`tools/refactor/phase12_speed_report.cpp`
