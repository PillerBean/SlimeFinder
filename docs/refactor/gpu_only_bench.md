# GPU-only 基准（Phase 17）

日期：2026-11-15
设备：`gfx1031` / 18 CU / 2189 MHz / 10224 MiB
工具：`tools/refactor/gpu_only_bench.cpp`

## 1. 指标含义

```text
kernel    OpenCL event profiling 的 device kernel 时间（不可用时为 0）
counter   回读命中计数器（包含等待 GPU 完成）
hits      回读命中坐标
select    CPU 侧 keepCandidates 选择/构造
total     一次 GPU 会话调用的 host wall time
wgSize    localSize / maxWorkGroupSize
waves     workGroups / computeUnits
busy      kernel / total
```

## 2. 结果

| case | total | device kernel | counter | hits | select | hits | WG | waves | busy |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Java density 512 | 0.83 | n/a | 0.14 | 0.00 | 0.58 | 256/586 | 1.00 | 1.5 | — |
| Java density 1024 | 0.38 | n/a | 0.09 | 0.00 | 0.21 | 256/2273 | 1.00 | 4.7 | — |
| Java density 2048 | 0.49 | 0.08 | 0.16 | 0.00 | 0.24 | 256/9520 | 1.00 | 17.5 | 16% |
| Bedrock density 512 | 0.70 | n/a | 0.43 | 0.00 | 0.19 | 256/647 | 1.00 | 1.5 | — |
| Bedrock density 1024 | 1.13 | n/a | 0.82 | 0.00 | 0.22 | 256/2448 | 1.00 | 4.7 | — |
| Bedrock density 2048 | 3.29 | **2.84** | 2.93 | 0.00 | 0.27 | 256/9776 | 1.00 | 17.5 | **86%** |
| Java pattern 1024 | 0.28 | 0.01 | 0.10 | 0.08 | 0.00 | 113/113 | 1.00 | 4.7 | 2% |
| Java pattern 2048 | 0.33 | 0.00 | 0.15 | 0.07 | 0.00 | 436/436 | 1.00 | 16.5 | — |
| Bedrock pattern 1024 | 1.10 | 0.00 | 0.94 | 0.08 | 0.00 | 101/101 | 1.00 | 4.7 | — |
| Bedrock pattern 2048 | 3.04 | 0.01 | 2.74 | 0.18 | 0.00 | 410/410 | 1.00 | 16.5 | — |

## 3. 结论

### Java density

- 总时间 0.38~0.83 ms；
- `select`（keepCandidates 选择/构造）占比明显；
- 设备端不是主要瓶颈，优化重点在 **回读/选择/结果构造**。

### Bedrock density 2048

- total 3.29 ms，device kernel 2.84 ms，busy 86%；
- 这是当前最明确的**内核计算瓶颈**；
- 下一步优先优化 Bedrock 密度内核本身。

### Pattern

- device kernel 时间几乎为 0；
- `counter`/`hits` 回读占主导；
- 优化方向是 **回读延迟/合并**，不是内核算术。

### 占用率

当前所有 case：

```text
localSize = 256
maxWorkGroupSize = 256
wgSize = 1.00
waves = 1.5 ~ 17.5
```

说明工作组大小已用满设备上限；真正占用率仍取决于寄存器和每 CU 常驻工作组，
当前还没有拿到硬件 occupancy counter，只能先用 waves 作为代理。

## 4. 设备时间可用性问题

OpenCL event profiling 只在部分 case 返回有效值：

```text
Bedrock density 2048：有效
Java density 2048：有效
其余多数 case：event profiling 返回不可用，显示 n/a
```

这不是 0 时间，而是 profiling 信息不可用。
下一步要么修 event 查询（wait/finish 顺序），要么改用设备端计时缓冲区。

## 5. 第 2 步尝试：Bedrock 密度内核

尝试把密度内核里的 Bedrock 逐位生成改成：

```text
bedrock_pair：一次两条 MT 链
bedrock_quad：一次四条 MT 链
```

结果（Bedrock density 2048 device kernel）：

```text
原始 scalar：约 2.84 ms
bedrock_pair：约 2.73~2.94 ms
bedrock_quad：约 2.95 ms
```

没有稳定收益，register 压力/占用率抵消了 ILP，已全部回退。

结论：

```text
Bedrock 密度内核已经是 MT19937 计算受限
简单增加每线程链数不能突破
```

## 6. 第 2 步突破：8 条显式 MT 链

pair/quad 没有收益，但把 8 条链**显式展开**（不用数组索引）有效：

```text
Bedrock density 2048 device kernel
  scalar       约 2.84 ms
  pair/quad    约 2.73~2.95 ms（无稳定收益）
  8-chain oct  约 2.61 ms      （约 8%）
```

同一 helper 用在 pattern 内核：

```text
Bedrock pattern 2048 counter 等待
  scalar       约 2.74 ms
  8-chain oct  约 2.45~2.49 ms（约 9%）
```

关键点：

```text
数组 s[4]/s[8] + 内层 j 循环会被编译器/占用率吃掉
显式 s0..s7 才能让编译器排开 8 条独立递推
```

## 6.1 第 2 步结论

```text
窗口统计：内核已按列滑窗，没有明显冗余
命中筛选/回读压缩：对 Bedrock 2048 收益有限
Bedrock MT 递推：真正瓶颈
8 链显式展开：目前唯一稳定收益
```

## 7. 第 3 步：pattern local size

测试 `SLIME_GPU_PAT_LOCAL = 64 / 128 / 256`，pattern 2×2：

| local | Java 1024 | Java 2048 | Bedrock 1024 | Bedrock 2048 |
|---:|---:|---:|---:|---:|
| 64 | 0.31 | 0.40 | 3.95 | 6.05 |
| 128 | 0.26 | 0.31 | 1.17 | 3.25 |
| 256 | 0.28 | 0.33 | 1.12 | 3.00 |

结论：

```text
local=256 对 Bedrock 最好
local=128 对 Java 略好但差距在噪声内
默认保持 256
```

pattern 内核的 Task 循环已改为 `get_local_size(0)`，并保留环境变量作为 A/B 钩子；
默认行为不变。

## 8. 第 3 步结论

```text
local size：256 已是当前最优
lane/chain：pattern Bedrock 仍受 MT 递推限制
Java pattern：kernel 很小，counter/hits 回读占主导
```

下一步若继续，只能重构 pattern 内核的 MT 计算方式，
而不是继续调 local size。


