# 环形全图实战实验协议

## 目标

在 Java、seed=0、全图范围内，用单次 ≤20 分钟的 GPU 实测，最大化环形结果的**加权覆盖 `coveredWeight`**。

以“方形密度预筛 + 1 格精扫”为基准，逐次调参寻找更好的方案。

## 固定参数

```text
Version: Java
Seed: 0
Map: X/Z chunks in [-1,875,000, 1,875,000]  (= ±30,000,000 blocks)
Ring outer radius: 128 blocks
Ring inner radius: 24 blocks
Target: best 1 (topN=1)
Min density: 0
Min separation: 24 (top1 irrelevant)
Center precision: 1 block
```

## 指标

主指标：

```text
coveredWeight (加权覆盖)
```

次指标：

```text
coveredBlocks
coveragePercent
耗时
GPU 设备 / 是否回退
```

## 时间

```text
单次硬上限：1200s
基准参考：210s
t < 210s：记录为“偏快”，仍参与质量比较，但标注可能偏近似
t > 1200s：超时失败
```

## 基准

```text
方形密度预筛（GPU prescan）
→ seedCandidates
→ 加权粗排 + 1 格精扫
→ top1 by coveredWeight
```

只跑一次基准。以后所有方案与这次基准比较。

## 执行规则

- 每次只跑一个 GPU 测试，不并行。
- 每次记录 run_id、方案、参数、耗时、coveredWeight、坐标。
- 方案参数可反复调整。
- 社区资料只作为参考，必须注明来源 URL，不作为本地结论。
- 工作留痕：`runs.csv`、`raw_logs/`、`summary.md`。

## 时间窗口

```text
网络时间开始：2026-11-18T23:30:20+08:00
网络时间结束：2026-11-19T05:30:20+08:00
```

中途不停止；构建/GPU 故障也记录。
