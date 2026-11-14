# SlimeFinder v3.12.3 区块判定速度

测试日期：2026-11-14  
测试对象：v3.12.3 源码构建的 `slime_chunk_bench`  
测试内容：**只测区块判定**，不测密度/图案匹配。  
结果：连续运行 3 次，表中取中位数。

## 测试环境

```text
CPU：AMD Ryzen 5 5600 6C/12T
GPU：AMD Radeon RX 6750 GRE 10GB（gfx1031，18 CU）
线程：12
```

## 方法

- CPU：`SlimeEngine::fillSlimeRowJava` / `fillSlimeRowBedrock`，按行并行，12 线程；
- GPU：`GpuScan::scanRectWords`，包含 GPU 判定和结果回读；
- 每档尺寸预热后测量，程序内多次取较优值；
- 整轮测试重复 3 次，最终取 3 次中位数；
- GPU 结果与 CPU 逐块对比：Java、Bedrock 均通过；
- 不计入搜索范围、结果筛选、密度窗口、图案匹配。

## 结果（3 次中位数）

| 平台 | 后端 | 区块数 | 耗时 | 判定速度 |
|---|---|---:|---:|---:|
| Java | CPU | 16,777,216（4096²） | 6.13 ms | **27.39 亿/s** |
| Java | CPU | 33,554,432（8192×4096） | 12.14 ms | **27.64 亿/s** |
| Java | GPU | 16,777,216 | 0.59 ms | **285.57 亿/s** |
| Java | GPU | 33,554,432 | 0.86 ms | **391.31 亿/s** |
| Bedrock | CPU | 16,777,216 | 47.80 ms | **3.51 亿/s** |
| Bedrock | CPU | 33,554,432 | 99.35 ms | **3.38 亿/s** |
| Bedrock | GPU | 16,777,216 | 8.65 ms | **19.41 亿/s** |
| Bedrock | GPU | 33,554,432 | 16.70 ms | **20.10 亿/s** |

## 对比

以 33,554,432 区块为例：

```text
Java    CPU 27.64 亿/s  →  GPU 391.31 亿/s   约 14.2 倍
Bedrock CPU  3.38 亿/s  →  GPU  20.10 亿/s   约 5.9 倍
```

以 16,777,216 区块为例：

```text
Java    CPU 27.39 亿/s  →  GPU 285.57 亿/s   约 10.4 倍
Bedrock CPU  3.51 亿/s  →  GPU  19.41 亿/s   约 5.5 倍
```

小尺寸 GPU 固定开销更明显：

```text
Java    4.19M：CPU 31.15 亿/s，GPU 110.73 亿/s
Bedrock 4.19M：CPU  3.45 亿/s，GPU  16.75 亿/s
```

## 稳定性

3 次运行中：

- Java CPU 33M：27.13 ~ 27.70 亿/s；
- Java GPU 33M：374.24 ~ 394.43 亿/s；
- Bedrock CPU 33M：3.36 ~ 3.53 亿/s；
- Bedrock GPU 33M：20.09 ~ 20.19 亿/s。

GPU 比 CPU 稳定；CPU 受频率和后台负载影响，存在约 5%~10% 波动。

## 说明

- Java 判定路径明显快于 Bedrock；Bedrock 每个区块需要初始化 MT19937 状态，单块成本更高；
- GPU 数据包含结果回读，因此是端到端判定吞吐，不是纯内核时间；
- CPU 使用 12 线程；
- 以上均为原始区块判定速度，不是搜索匹配速度。
