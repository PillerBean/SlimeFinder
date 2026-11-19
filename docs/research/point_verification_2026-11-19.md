# 点验证记录：seed / 坐标 / 14700+ 有效格

日期：2026-11-19  
输入：

```text
版本：Java
seed：-81800043778910677489
挂机点：(17415252, 285556492)
环带：默认 outer=128, inner=24
目标：验证是否存在 coveredBlocks / coveredWeight 14700+ 的结果
```

## 结论先行

**当前输入不能作为一个有效的 Java seed + 世界坐标组合。**

1. seed 为 20 位十进制整数：

```text
-81,800,043,778,910,677,489
```

超出 Java/Minecraft 的 signed 64-bit 范围：

```text
int64 min = -9,223,372,036,854,775,808
int64 max = +9,223,372,036,854,775,807
```

也超出 unsigned 64-bit 范围：

```text
2^64 = 18,446,744,073,709,551,616
```

因此它不能直接作为 Java 世界 seed。

2. Z 坐标：

```text
285,556,492 方块
```

超出 Java 世界边界：

```text
±30,000,000 方块
```

除非是模组/改边界世界，或者坐标单位不是方块。

## 已尝试的解释

| seed 解释 | 坐标 | coveredBlocks | coveredWeight |
|---|---|---:|---:|
| signed 64-bit 回绕 `-8013067484072471025` | 原坐标 | 3915 | 3909.793 |
| 去掉最后一位 `-8180004377891067748` | 原坐标 | 4944 | 4877.932 |
| 去掉第一位 `-1800043778910677489` | 原坐标 | 4941 | 4892.659 |
| Java `String.hashCode` `762593276` | 原坐标 | 3737 | 3700.438 |
| 最佳测试组合 | 坐标/16 或去尾数 | 7037 | 6951.062 |

没有任何组合接近 14700。

## 附加测试

- 基准点 1 方块/4 方块局部：没有提升。
- 给定坐标附近 ±512 方块局部最大：

```text
约 7589–8454 coveredBlocks
约 7443–8410 coveredWeight
```

仍远低于 14700。

- 如果半径改为 256，环带覆盖会自然变大（约 17k–20k），但这不是默认 128 参数下的结果，不能直接与该 14700 说法比较。

## 需要确认

1. seed 的准确 signed 64-bit 值（游戏内 `/seed` 或社区工具原始输出）。
2. 坐标单位是方块还是区块；Z 是否多写了一位/两位。
3. 该 14700+ 使用的环带参数（outer/inner）。
4. 是 `coveredBlocks` 还是 `coveredWeight`。
5. 是否来自模组/改世界边界的世界。

## 工具

`tools/bench/ring_fullmap_bench.cpp` 新增 `RING_POINT=1` 点验证模式：

```text
RING_POINT=1
RING_SEED=<signed int64>
RING_CX=<block x>
RING_CZ=<block z>
RING_RADIUS=128
RING_INNER=24
RING_WEIGHTED=1
```

输出 `coveredBlocks / coveredWeight / coveragePercent / coveredChunks`。
