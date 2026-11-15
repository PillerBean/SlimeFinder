# SlimeFinder 阶段耗时测量（当前工作树，2026-11-15）

> 目的：把“判定/扫描”和“匹配/结果”拆开看，避免只看总耗时。
> 测量对象是当前 3.13.6 工作树编译出的基准程序；不是旧版安装包。
> 基准程序打印的版本横幅有历史遗留（mode bench 显示 3.13.0，raw bench 显示 3.12.3），
> 但二进制均于 2026-11-15 从当前工作树重新构建。

## 机器与参数

```text
CPU：AMD Ryzen 5 5600 6C / 12T
GPU：AMD Radeon RX 6750 GRE 10GB，gfx1031，18 CU
系统：Windows
线程：自动（12）
```

通用参数：

```text
区域：2048×2048 = 4,194,304 区块
Java 种子：12345
图案：2×2 全绿，方向锁定
密度：g=5，阈值 0.3
stopAtTarget：false
useCache：false
判定位图复用：第一轮测量显式关闭
基准：每种模式跑 2 次，取较快一次；GPU 数据同时看暖机后的第二次
```

命令：

```bat
set SLIME_REUSE_BITMAP=0
set SLIME_BENCH_DEFAULT_THRESHOLD=1
set SLIME_GPU_PATTERN=1
set SLIME_BENCH_TARGET=1
set SLIME_BENCH_SIDE=2048
set SLIME_DEBUG=1
set SLIME_PROFILE=1
build\slime_mode_bench.exe
```

## 1. 原始判定阶段（不含匹配）

`build\slime_chunk_bench.exe` 的结果：

| 版本 | 后端 | 区域 | 耗时 | 吞吐 |
|---|---|---|---:|---:|
| Java | CPU 行扫描 | 2048×2048 | **1.433 ms** | 29.27 亿区块/s |
| Java | GPU 打包字 | 2048×2048 | **0.382 ms** | 109.94 亿区块/s |
| Bedrock | CPU 行扫描 | 2048×2048 | **12.496 ms** | 3.36 亿区块/s |
| Bedrock | GPU 打包字 | 2048×2048 | **2.528 ms** | 16.59 亿区块/s |
| Java | CPU 行扫描 | 4096×4096 | 6.389 ms | 26.26 亿区块/s |
| Java | GPU 打包字 | 4096×4096 | 0.575 ms | 291.88 亿区块/s |
| Bedrock | CPU 行扫描 | 4096×4096 | 50.572 ms | 3.32 亿区块/s |
| Bedrock | GPU 打包字 | 4096×4096 | 8.607 ms | 19.49 亿区块/s |

说明：这一张表只测判定，不含图案/密度匹配、结果入堆、进度回调和位图分配。

## 2. 默认目标数（target=1）的分阶段总表

2048×2048，完整搜索：

| 模式 | 后端 | 总耗时 | 扫描/判定 | 匹配/结果 | 其他 | 吞吐 |
|---|---|---:|---:|---:|---:|---:|
| Java 图案 | CPU | 2.27 ms | 1.03 ms | 1.13 ms | 0.11 ms | 18.48 亿/s |
| Java 图案 | GPU | **0.48 ms** | 融合 | 融合 | 融合 | **87.20 亿/s** |
| Java 密度 exact | CPU | 2.36 ms | 1.07 ms | 1.19 ms | 0.10 ms | 17.76 亿/s |
| Java 密度 exact | GPU | **0.77 ms** | 融合 | 融合 | 融合 | **54.78 亿/s** |
| Java 密度 layered | CPU | 3.60 ms | 1.03 ms | 2.42 ms | 0.15 ms | 11.65 亿/s |
| Java 密度 sample | CPU | 1.39 ms | 0.41 ms | 0.91 ms | 0.07 ms | 30.14 亿/s |
| Java 密度 sample-exact | CPU | 3.33 ms | 2.02 ms | 1.23 ms | 0.08 ms | 12.58 亿/s |
| Java 密度 brute | CPU | 12.15 ms | 1.07 ms | 10.97 ms | 0.11 ms | 3.45 亿/s |
| Bedrock 图案 | CPU | 17.99 ms | 16.63 ms | 1.25 ms | 0.11 ms | 2.33 亿/s |
| Bedrock 图案 | GPU | **3.05 ms** | 融合 | 融合 | 融合 | **13.76 亿/s** |
| Bedrock 密度 exact | CPU | 18.45 ms | 17.16 ms | 1.17 ms | 0.12 ms | 2.27 亿/s |
| Bedrock 密度 exact | GPU | **3.96 ms** | 融合 | 融合 | 融合 | **10.59 亿/s** |
| Bedrock 密度 layered | CPU | 19.49 ms | 17.00 ms | 2.37 ms | 0.12 ms | 2.15 亿/s |
| Bedrock 密度 sample | CPU | 1.64 ms | 0.47 ms | 1.09 ms | 0.08 ms | 25.63 亿/s |
| Bedrock 密度 sample-exact | CPU | 35.07 ms | 33.73 ms | 1.32 ms | 0.02 ms | 1.20 亿/s |
| Bedrock 密度 brute | CPU | 27.63 ms | 16.94 ms | 10.57 ms | 0.12 ms | 1.52 亿/s |

阶段占比：

```text
Java 图案 CPU：判定 45%，匹配 50%，其他 5%
Java 密度 CPU：判定 45%，匹配 50%，其他 5%
Bedrock 图案 CPU：判定 92%，匹配 7%，其他 1%
Bedrock 密度 CPU：判定 93%，匹配 6%，其他 1%
```

`scan / match` 对 GPU 行不能用：GPU 图案/密度是全融合路径，引擎把整段耗时记在 scan，
所以模式表里 GPU 的 match 显示为 0；GPU 内部阶段见第 4 节。

## 3. 高目标数（target=200000）的阶段变化

2048×2048，完整搜索：

| 模式 | 后端 | 总耗时 | 扫描/判定 | 匹配/结果 |
|---|---|---:|---:|---:|
| Java 图案 | CPU | 2.77 ms | 1.10 ms | 1.57 ms |
| Java 图案 | GPU | **0.63 ms** | 融合 | 融合 |
| Java 密度 exact | CPU | 5.07 ms | 1.16 ms | 3.81 ms |
| Java 密度 exact | GPU | **3.03 ms** | 融合 | 融合 |
| Java 密度 layered | CPU | 6.15 ms | 1.10 ms | 4.99 ms |
| Java 密度 brute | CPU | 13.81 ms | 1.12 ms | 12.52 ms |
| Bedrock 图案 | CPU | 17.92 ms | 16.29 ms | 1.50 ms |
| Bedrock 图案 | GPU | **3.20 ms** | 融合 | 融合 |
| Bedrock 密度 exact | CPU | 19.84 ms | 15.86 ms | 3.75 ms |
| Bedrock 密度 exact | GPU | **5.87 ms** | 融合 | 融合 |
| Bedrock 密度 layered | CPU | 21.23 ms | 15.86 ms | 5.14 ms |
| Bedrock 密度 brute | CPU | 29.97 ms | 16.97 ms | 12.79 ms |

结论：

- Java 模式在 target 从 1 提到 200k 后，匹配/结果从约 1.1~1.2 ms 增到 1.6~3.8 ms，说明结果收集和入堆开始显著；
- Bedrock 因为判定占绝对大头，target 提高对总耗时影响较小（图案 17.99→17.92 ms，密度 18.45→19.84 ms）；
- GPU 密度在 target=200k 时出现约 0.4~0.5 ms 的“结果转换+入堆”额外开销。

## 4. GPU 内部阶段

### 4.1 GPU 密度匹配（8192×8192，target=1，暖机第二次）

> 注：本节内部日志与上面的模式表来自不同轮次，存在约 0.5 ms 级波动；
> 总耗时以上面的模式表为准，本节用来看各子阶段的比例。

| 版本 | matchDensity 总耗时 | 派发/内核入队 | counter 回读 | hits 回读 | 结果转换+入堆 |
|---|---:|---:|---:|---:|---:|
| Java | 3.57 ms | 0.21 ms | 1.42 ms | 2.49 ms | 0.00 ms |
| Bedrock | 39.57 ms | 0.23 ms | 37.39 ms | 38.53 ms | 0.00 ms |

说明：

- `counter read` / `hits read` 是驱动日志口径，包含等待内核完成和回读，**不是互斥分段**；
- 以 `matchDensity total` 为准；
- Bedrock 的 GPU 密度路径主要耗在等待/回读，内核入队本身只有约 0.2 ms 的 CPU 侧时间；
- Java 的命中回读明显更快。

2048×2048、target=200000 时：

```text
Java 密度 GPU：
  matchDensity 0.91 ms
  结果转换+入堆 0.51 ms

Bedrock 密度 GPU：
  matchDensity 4.04 ms
  结果转换+入堆 0.41 ms
```

### 4.2 GPU 图案流式（8192×8192，target=1）

| 版本 | 总计 | 分配/清零等准备 | 回读 | 剩余（内核/等待/收尾） |
|---|---:|---:|---:|---:|
| Java | 1.15 ms | 约 0.25 ms | 0.11 ms | 约 0.79 ms |
| Bedrock | 35.90 ms | 约 0.24 ms | 0.15 ms | 约 35.5 ms |

2048×2048 时：

```text
Java 图案 GPU 暖机：总计 0.49 ms
  （计数清零 0.23 ms、回读 0.08 ms、其余约 0.18 ms）

Bedrock 图案 GPU 暖机：总计 2.94~3.00 ms
  （计数清零约 0.12~0.16 ms、回读约 0.09~0.10 ms、其余为主要内核时间）
```

这说明小范围 Java 图案有明显固定开销（清零/准备约 0.25 ms）；范围放大后内核占主导，
Bedrock 图案在 GPU 上仍是内核主导。

## 5. 位图复用后的重复搜索阶段

2048×2048，target=1，CPU 精确路径，同版本/种子/范围，无缓存：

| 模式 | 首次总耗时 | 复用后总耗时 | 首次扫描 | 复用后扫描 | 匹配仍为 |
|---|---:|---:|---:|---:|---:|
| Java 图案 | 2.27 ms | **1.17 ms** | 1.03 ms | **0.14 ms** | 0.96 ms |
| Java 密度 | 2.36 ms | **1.36 ms** | 1.07 ms | **0.16 ms** | 1.11 ms |
| Bedrock 图案 | 17.99 ms | **1.20 ms** | 16.63 ms | **0.13 ms** | 1.01 ms |
| Bedrock 密度 | 18.45 ms | **1.32 ms** | 17.16 ms | **0.14 ms** | 1.09 ms |

说明：

- 复用只跳过判定/扫描，匹配仍按新图案/阈值重跑；
- GPU 流式路径不使用这张 CPU 位图，因此 GPU 行不享受这项收益；
- 这是 3.13.5/3.13.6 的优化重点：改参数重复搜索时，Bedrock 的判定阶段几乎消失。

## 6. 大范围趋势（8192×8192，target=1）

| 模式 | 后端 | 总耗时 | 扫描/判定 | 匹配/结果 | 吞吐 |
|---|---|---:|---:|---:|---:|
| Java 图案 | CPU | 14.41 ms | 10.92 ms | 3.03 ms | 46.56 亿/s |
| Java 图案 | GPU | **1.25 ms** | 融合 | 融合 | **535.33 亿/s** |
| Java 密度 exact | CPU | 18.92 ms | 10.59 ms | 8.01 ms | 35.46 亿/s |
| Java 密度 exact | GPU | **3.09 ms** | 融合 | 融合 | **217.03 亿/s** |
| Bedrock 图案 | CPU | 201.30 ms | 197.54 ms | 3.42 ms | 3.33 亿/s |
| Bedrock 图案 | GPU | **35.58 ms** | 融合 | 融合 | **18.86 亿/s** |
| Bedrock 密度 exact | CPU | 205.30 ms | 196.81 ms | 8.16 ms | 3.27 亿/s |
| Bedrock 密度 exact | GPU | **39.63 ms** | 融合 | 融合 | **16.94 亿/s** |

阶段占比：

```text
Java 图案 CPU：判定 76%，匹配 21%，其他 3%
Java 密度 CPU：判定 56%，匹配 42%，其他 2%
Bedrock 图案 CPU：判定 98%，匹配 2%
Bedrock 密度 CPU：判定 96%，匹配 4%
```

## 7. 怎么看这些数字

1. **Java 判定很快，匹配开始变得重要**：2048² 时判定约 1 ms，target=1 匹配也约 1.1 ms；范围到 8192² 后匹配增长到 3~8 ms。
2. **Bedrock 仍然由判定主导**：CPU 路径 2048² 判定 16.6~17.2 ms，匹配只有 1.2 ms；GPU 判定降到 2.5~3 ms 级别，因此 Bedrock 的 GPU 总收益最大。
3. **GPU 密度不是“内核慢”**：Bedrock 的 `dispatch+kernel` 只有约 0.2 ms CPU 入队时间，真正耗时在等待完成和命中回读；Java 回读成本明显更低。
4. **target 影响结果收集**：target=1 时结果转换几乎为 0；target=200k 时 Java/Bedrock GPU 结果转换约 0.4~0.5 ms。
5. **位图复用改变瓶颈**：重复搜索时判定降到 0.13~0.16 ms，CPU 侧主要成本变成匹配；Bedrock 首次搜索的 17~18 ms 判定在复用后基本消失。
6. **小范围 GPU 图案有固定开销**：2048² Java 图案的计数清零/准备约 0.25 ms，占 GPU 总时间接近一半；大范围时内核占主导。

## 8. 限制

- 单机测量，未独占 CPU/GPU，后台程序和驱动调度会带来约 5~15% 波动；
- 每种模式只跑 2 次取较优，未做统计置信区间；
- `slideWindowMs` 是跨线程累加的线程时间，不是墙钟，不能直接与总耗时相加；
- GPU 的 counter/hits 回读日志包含等待，不是严格的互斥阶段；
- 本报告没有覆盖缓存命中搜索、提前停止、智能范围、旋转图案的独立计时。
