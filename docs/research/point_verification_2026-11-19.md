# 点验证记录：seed / 坐标 / 14700+ 有效格

日期：2026-11-19（最终修正版）

## 输入

```text
版本：Java
seed：-8180004378910677489
挂机点：(17415252, 28556492) 方块
环带：outer=128, inner=24
```

## 结论

**存在，已验证通过。**

精确 1 方块统计：

```text
coveredBlocks  = 14739
coveredWeight  = 14595.893165
coverage       = 29.678450%
coveredChunks  = 60
```

坐标：

```text
X = 17,415,252
Z = 28,556,492
```

种子在 signed 64-bit 范围内，坐标在 Java 世界边界内（Z < 30,000,000）。

## 与当前基准对比

```text
旧基准：blocks=14387, weight=14324.516, center=(3472965,-13446988)
新点：  blocks=14739, weight=14595.893, center=(17415252,28556492)

blocks +352
weight +271.377 (+1.89%)
```

## 验证方法

- `AfkPlanner::measureExact`：无加权，1 方块精度 → 14739。
- `AfkPlanner::measureStrictForCandidates`：Java 1.21.3、1:4 群系权重 → 14595.893。
- 局部 ±32 方块、步长4：该点仍是局部最优，未找到更高值。

## 说明

此前输入 `-81800043778910677489` 与 `Z=285,556,492` 不自洽，是错误版本；修正后的 seed/坐标已通过。

## 工具

`tools/bench/ring_fullmap_bench.cpp`：

```text
RING_POINT=1
RING_VERSION=java
RING_SEED=-8180004378910677489
RING_CX=17415252
RING_CZ=28556492
RING_RADIUS=128
RING_INNER=24
RING_WEIGHTED=1
```
