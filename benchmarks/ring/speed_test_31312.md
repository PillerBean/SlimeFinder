# SlimeFinder 3.13.12 全面速度测试

日期：2026-11-24  
机器：AMD Ryzen 5 5600（6C/12T），Radeon RX 6750 GRE 10GB，gfx1031

## 1. 普通模式 16384²（268,435,456 chunks）

目标数量 200000，stop=false，线程 auto。

| 模式 | 设备 | 总耗时 | 吞吐 | 扫描 / 匹配 |
|---|---|---:|---:|---:|
| Java 严格图案 2×2 | CPU | 61.9 ms | 43.4 亿/s | 42.5 / 17.7 ms |
| Java 严格图案 2×2 | GPU 开关 | 61.2 ms | 43.8 亿/s | 42.7 / 17.3 ms |
| Java 密度 exact | CPU | 213.1 ms | 12.6 亿/s | 43.2 / 168.5 ms |
| Java 密度 exact | GPU | 54.7 ms | 49.1 亿/s | 25.9 / 28.3 ms |
| Java 密度 layered | CPU | 275.3 ms | 9.8 亿/s | 42.3 / 231.5 ms |
| Java 密度 sample | CPU | 131.7 ms | 20.4 亿/s | 14.5 / 115.8 ms |
| Java 密度 sample-exact | CPU | 105.6 ms | 25.4 亿/s | 89.6 / 14.7 ms |
| Java 密度 brute | CPU | 874.4 ms | 3.1 亿/s | 64.5 / 808.4 ms |
| Bedrock 严格图案 2×2 | CPU | 813.8 ms | 3.3 亿/s | 794.6 / 17.8 ms |
| Bedrock 密度 exact | CPU | 888.5 ms | 3.0 亿/s | 707.3 / 179.7 ms |
| Bedrock 密度 exact | GPU | 195.4 ms | 13.7 亿/s | 166.2 / 28.7 ms |
| Bedrock 密度 layered | CPU | 938.9 ms | 2.9 亿/s | 700.8 / 236.6 ms |
| Bedrock 密度 sample | CPU | 141.4 ms | 19.0 亿/s | 24.7 / 115.2 ms |
| Bedrock 密度 sample-exact | CPU | 2253.0 ms | 1.2 亿/s | 2231.6 / 20.0 ms |
| Bedrock 密度 brute | CPU | 1638.5 ms | 1.6 亿/s | 819.8 / 817.1 ms |

结论：

```text
Java 密度 exact：GPU 约 3.9× CPU
Bedrock 密度 exact：GPU 约 4.5× CPU
严格图案：GPU 路径未启用，无加速
layered 比 exact 慢约 1.3×
sample 比 exact 快约 1.6×
brute 比 exact 慢约 4.1×
Bedrock sample-exact 极慢（约 10.6× exact）
```

## 2. 开关组合矩阵

`est_s` 为当前 3.13.12 首轮估算（已加基线修正），`actual_s` 为实测。

| case | 估算 | 实测 | 实测/估算 |
|---|---:|---:|---:|
| 4096 exact CPU | 0.0110 | 0.0162 | 1.48 |
| 4096 pattern CPU | 0.0057 | 0.0225 | 3.99 |
| 4096 stop10 CPU | 0.0049 | 0.0065 | 1.34 |
| 4096 smart CPU | 0.0157 | 0.0172 | 1.10 |
| 4096 exact GPU | 0.0066 | 0.0081 | 1.23 |
| 4096 pattern GPU | 0.0055 | 0.0053 | 0.97 |
| 4096 stop10 GPU | 0.0012 | 0.0013 | 1.11 |
| 4096 smart GPU | 0.0037 | 0.0082 | 2.23 |
| 4096 layered CPU | 0.0240 | 0.0212 | 0.89 |
| 4096 sample CPU | 0.0076 | 0.0104 | 1.37 |
| 4096 brute CPU | 0.0723 | 0.0530 | 0.73 |
| 16384 exact CPU | 0.2502 | 0.2085 | 0.83 |
| 16384 pattern CPU | 0.0670 | 0.0585 | 0.87 |
| 16384 stop10 CPU | 0.0692 | 0.0503 | 0.73 |
| 16384 smart CPU | 0.2370 | 0.2087 | 0.88 |
| 16384 exact GPU | 0.0641 | 0.0537 | 0.84 |
| 16384 pattern GPU | 0.0684 | 0.0583 | 0.85 |
| 16384 stop10 GPU | 0.0204 | 0.0041 | 0.20 |
| 16384 smart GPU | 0.0756 | 0.0719 | 0.95 |
| 16384 layered CPU | 0.2875 | 0.2709 | 0.94 |
| 16384 sample CPU | 0.1003 | 0.1251 | 1.25 |
| 16384 brute CPU | 0.6993 | 0.6903 | 0.99 |

大部分组合落在 0.7–1.5×。异常：

```text
4096 pattern CPU：实测 22.5 ms 明显高于同规模历史值，疑似当次系统噪声
4096 smart GPU：估算仍偏乐观
16384 stop10 GPU：估算偏悲观；GPU 路径提前结束后返回的数量语义需单独核对
```

## 3. 线程扩展（4096²，Java 密度 exact）

| 线程 | 实测 |
|---|---:|
| 1 | 29.5 ms |
| 4 | 15.9 ms |
| auto（12 逻辑核） | 16.1 ms |

结论：

```text
1 → 4 线程约 1.85×
4 → auto 不再提升；该规模受内存/匹配阶段限制
```

## 4. 环形扫描

三真实种子历史实测：

| 模式 | 单种子耗时 | 结果 |
|---|---:|---:|
| 快速粗扫 | 1.28–1.50 s | 10,977–11,302 blocks |
| 方形预筛+精修 | 217–220 s | 14,739 / 14,387 / 14,742 |
| k=3 候选级 B&B | 217.7–219.8 s | 与方形基线相同；候选 4–28 / 4000 |
| 字面 36 次全图卷积 | 约 2.2 小时（外推） | 不现实 |

结论：

```text
环形预筛仍是 217–220s 瓶颈
k=3 上界+B&B 只多约 0.2s
没有全图 36 卷积实现，210s 目标未达到
```

## 5. 耗时预测

首次首轮（未校准）大部分已从旧模型的 3–8× 低估收敛到 0.7–1.5×。  
同配置第二次实测校准：

```text
普通全扫描：预计 0.111s，实际 0.112s
提前停止：  预计 0.080s，实际 0.081s
```

说明：

```text
首轮仍有离群（pattern/smart GPU/GPU stop）
GPU stop 的数量语义需要单独审计
首次系数来自本机，换机器后由 timing_calibration.tsv 自校准
```

## 6. 复现

```powershell
# 普通模式矩阵
$env:SLIME_BENCH_SIDE='16384'
.\build\slime_mode_bench.exe

# 开关组合矩阵
.\build\slime_search_speed_matrix.exe
```

产物：

```text
benchmarks/ring/mode_speed_16384_31312.log
benchmarks/ring/search_speed_matrix_corrected.tsv
```
