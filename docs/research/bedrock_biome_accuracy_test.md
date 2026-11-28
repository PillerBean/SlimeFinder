# Bedrock 群系准确性测试

日期：2026-11-28  
脚本：`scripts/qa/bedrock_biome_accuracy.ps1`

## 测试目标

验证：

```text
1. 独立已知河流样本
2. 1.18+ 上游哈希基准
3. 单点与批量查询一致
4. 重复查询稳定
5. 河流过滤行为
```

## 1. 独立样本

来自 MCPE-161245 报告的 Bedrock 河流坐标：

```text
seed = 2229010591266423951
x = -2984
z = 4033
y = 74
```

查询结果：

```text
version 1.21.60 → river
```

与 Bug 报告中的“河流群系”一致。

## 2. 版本范围与哈希基准

当前开放：

```text
1.18, 1.19, 1.20, 1.21, 1.21.50, 1.21.60,
26.20, 26.30, 26.40, 26.50, 26.60
```

上游 1x1 哈希基准：

```text
1.18 OK
1.19 OK
1.20 OK
```

1.18 前（1.16/1.17/1.17.30）哈希不一致，
现作为旧版分层生成器开放；Java 哈希基准不适用，但仍缺独立 Bedrock 对照。

## 3. 基础样本

```text
seed0 1.21.60 (0,0,63)       → river
seed0 1.21.60 (104,104,63)   → plains
seed0 26.60   (0,0,63)       → river
```

## 4. 批量一致性

批量：

```text
8 8 63       → river
104 104 63   → plains
```

与单点查询一致。

## 5. 重复性

同一查询连续 3 次结果一致。

## 6. 河流过滤测试

GUI 自检直接调用 `BedrockRiverFilter`：

```text
river 区块   → 移除
plains 区块  → 保留
executed=1
removed=1
left=1
```

## 结果

```text
BEDROCK ACCURACY OK
```

## 局限

```text
- 只有一个独立 Bug 报告坐标
- 1.18 前版本已开放；Java 哈希基准不适用
- 没有 Bedrock Dedicated Server 逐区块对照
- 过滤按区块中心 + Y=63，跨边界仍可能误判
```

## 结论

```text
当前实现至少能复现一个真实 Bedrock 河流报告，
版本选择、批量查询和过滤行为均可重复。
还不能宣称“全版本全坐标已证明准确”。
```

## 7. 上游 1x1 哈希基准（关键）

使用上游 `tests.c` 的 b6 期望哈希，对照本库：

```text
1.18  OK
1.19  OK
1.20  OK

1.16  N/A（Java 基准不适用）
1.13  N/A（Java 基准不适用）
1.12  N/A（Java 基准不适用）
1.9   N/A（Java 基准不适用）
1.7   N/A（Java 基准不适用）
1.6   N/A（Java 基准不适用）
1.2   N/A（Java 基准不适用）
1.1   N/A（Java 基准不适用）
1.0   N/A（Java 基准不适用）
B1.8  N/A（Java 基准不适用）
```

结论：

```text
当前 cubiomes-bedrock 的 1.18+ 路径与上游哈希一致；
1.18 前路径与同一基准不一致。
```

因此：

```text
1.16/1.17/1.17.30 使用旧版分层生成器
不作为河流过滤版本
```

工具：`tools/bench/bedrock_hash_check.cpp`




