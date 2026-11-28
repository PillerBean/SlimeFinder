# 史莱姆群系刷怪率历史检查

日期：2026-11-28

## 结论

对当前支持的 Bedrock 1.18–1.21.60 范围：

```text
未发现群系 slime 权重表发生版本性变化。
Java 版仍使用 Wiki 的 Spawn chance 表；
Bedrock 版除河流/冻河等明确不生成群系外，权重统一为 1.0。
```

因此 `SpawnWeights.cpp` 的当前数据不需要按版本拆分权重表。

## 当前数据来源

- Minecraft Wiki，Slime → Spawning：
  <https://minecraft.wiki/w/Slime>
- 中文 Minecraft Wiki，史莱姆：
  <https://zh.minecraft.wiki/w/史莱姆>
- Bedrock 河流 Bug：
  <https://mojira.dev/MCPE-161245>

## 已知版本差异

```text
Java：
  按群系其它敌对生物权重决定 slime 占比
  沼泽/红树林沼泽有额外地表生成
  slime chunk 在 Y<40 生成

Bedrock：
  群系 slime 权重基本统一
  river / frozen_river 不生成
  河流地下任意 Y 也受影响（MCPE-161245）
```

## 历史机制变化

与当前模型直接相关的历史变化：

```text
1.5      沼泽生成受月相影响
1.10     史莱姆区块 Y 限制调整为 Y<40
1.18     群系变为三维；地下群系与地表可不同
MCPE-161245  基岩版河流群系不生成史莱姆
```

这些变化不改变“同一群系内 slime 权重表”的取值，但会影响：

```text
- 查询群系时应使用哪个 Y
- Bedrock 河流过滤的条件
- 1.18 前后群系生成算法选择
```

## 应用内结论

```text
SpawnWeights 保持单表；
Bedrock 河流/冻河权重 0 已正确；
真正的缺口是 Bedrock 群系查询，而不是权重表过期。
```

## 待验证

```text
- 用 Bedrock Dedicated Server 实测 river / frozen_river
- 确认过滤应使用地表 Y 还是刷怪层 Y
- 旧版本（<1.18）需要另一套群系库，当前未接入
```


