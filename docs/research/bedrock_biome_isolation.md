# Bedrock 与 Java 群系算法隔离及版本表

## 1. 使用的社区项目

### 基岩版群系

- 项目：[FragrantResult186/cubiomes-bedrock](https://github.com/FragrantResult186/cubiomes-bedrock)
- 许可：MIT
- 本地路径：`third_party/cubiomes-bedrock/`
- 用途：按种子生成 Minecraft Bedrock Edition 群系
- 支持版本：`1.0`、`1.1`、`1.2`、`1.4`～`1.14`、`1.16`、`1.17`、`1.17.30`（旧版分层）、`1.18`～`26.60`

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

## 3. 压缩后的版本表

连续版本若共用同一群系算法分支，UI 合并为区间；
查询使用组内代表版本。

| UI 标签 | 查询代表 | 算法组 |
|---|---|---|
| 1.0 | 1.0 | MC_1_0 |
| 1.1 | 1.1 | MC_1_1 |
| 1.2 | 1.2 | MC_1_2 |
| 1.4-1.6 | 1.4 | MC_1_4 分支 |
| 1.7-1.13 | 1.7 | MC_1_7 分支 |
| 1.14 | 1.14 | 竹林层引入 |
| 1.16-1.17.30 | 1.16 | 旧版分层 |
| 1.18 | 1.18 | btree18 |
| 1.19 | 1.19 | btree19 |
| 1.20-1.21 | 1.20 | btree20 |
| 1.21.50 | 1.21.50 | btree21wd |
| 1.21.60 | 1.21.60 | btree215 |
| 26.20 | 26.20 | btree262 |
| 26.30 | 26.30 | btree263 |
| 26.40-26.50 | 26.40 | btree264 |
| 26.60 | 26.60 | btree266 |

压缩依据：

```text
1.18+：BiomeTree 选择阈值
1.18 前：setupLayerStack / 层分支阈值
```

## 4. 尚未完成

```text
- 应用内 Bedrock 版本选择器（已加入顶部栏）
- 应用内调用查询工具
- Bedrock 结果按 river/frozen_river 过滤
- 打包时携带 helper
```

当前已完成的是“算法隔离 + 可执行验证”，还没有改搜索结果。



## 5. 1.18 前版本为何未开放

新版上游 1x1 哈希基准：

``text
1.18-1.20: 与上游哈希一致
1.16/1.17/1.13/1.12/...: Java 哈希基准不适用（正常）
``r

因此这些旧版 Bedrock 枚举虽然存在，但当前生成结果不可信，未加入 UI。




