# Bedrock 群系准确性测试

日期：2026-11-28  
脚本：`scripts/qa/bedrock_biome_accuracy.ps1`

## 测试目标

验证：

```text
1. 独立已知河流样本
2. 版本差异确实生效
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

## 2. 版本差异

同一种子、同一坐标：

```text
1.16    → birch_forest
1.17    → birch_forest
1.17.30 → birch_forest
1.18    → river
1.20    → river
1.21    → river
1.21.60 → river
```

说明版本选择不能忽略。

## 3. 基础样本

```text
seed0 1.21.60 (0,0,63)       → river
seed0 1.21.60 (104,104,63)   → plains
seed0 1.16    (0,0,63)       → swamp
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
- 1.16/1.17 的期望值来自同一开源库，不是第二来源
- 没有 Bedrock Dedicated Server 逐区块对照
- 过滤按区块中心 + Y=63，跨边界仍可能误判
```

## 结论

```text
当前实现至少能复现一个真实 Bedrock 河流报告，
版本选择、批量查询和过滤行为均可重复。
还不能宣称“全版本全坐标已证明准确”。
```
