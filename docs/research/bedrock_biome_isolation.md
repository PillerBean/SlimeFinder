# Bedrock 与 Java 群系算法隔离及版本表

## 1. 使用的社区项目

### 基岩版群系

- 项目：[FragrantResult186/cubiomes-bedrock](https://github.com/FragrantResult186/cubiomes-bedrock)
- 许可：MIT
- 本地路径：`third_party/cubiomes-bedrock/`
- 用途：按种子生成 Minecraft Bedrock Edition 群系
- 支持版本：`1.18`、`1.19`、`1.20`、`1.21`、`1.21.50`、`1.21.60`、`26.20`、`26.30`

### Java 版群系

- 项目：[Cubitect/cubiomes](https://github.com/Cubitect/cubiomes)
- 许可：MIT
- 本地路径：`third_party/cubiomes/`
- 用途：Java 版群系地图与权重

## 2. 隔离方式

```text
slime_core / SlimeFinder.exe
  └─ cubiomes（Java）

slime_bedrock_biome_query.exe
  └─ cubiomes_bedrock（Bedrock）
```

两个库**不链接到同一个可执行文件**。

这样做的原因：

```text
cubiomes 与 cubiomes-bedrock 使用相同的公开符号名：
setupGenerator / applySeed / getBiomeAt / genBiomes ...
```

直接链接会符号冲突。当前用独立查询工具隔离：

```text
build/slime_bedrock_biome_query.exe <seed> <version> <x> <z> <y>
```

示例：

```text
BEDROCK_BIOME seed=0 version=1.21.60 x=0 z=0 y=63 id=7 name=river
```

## 3. 版本表

Bedrock 版本以 `cubiomes-bedrock` 的 `MC_*` 常量为准：

| UI 标签 | cubiomes-bedrock |
|---|---|
| 1.18 | MC_1_18 |
| 1.19 | MC_1_19 |
| 1.20 | MC_1_20 |
| 1.21 | MC_1_21 |
| 1.21.50 | MC_1_21_50 |
| 1.21.60 | MC_1_21_60 |
| 26.20 | MC_26_20 |
| 26.30 | MC_26_30 |

## 4. 尚未完成

```text
- 应用内 Bedrock 版本选择器
- 应用内调用查询工具
- Bedrock 结果按 river/frozen_river 过滤
- 打包时携带 helper
```

当前已完成的是“算法隔离 + 可执行验证”，还没有改搜索结果。
