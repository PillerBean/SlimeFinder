# 缓存导入、结果 Y 与搜索页整改记录

日期：2026-11-28

## 已修复

### 1. 缓存导入完成后再次点击“开始导入”崩溃

根因：

```text
导入完成后 m_worker 仍 joinable
再次 startImport 直接赋值 std::thread
→ std::terminate
```

修复：

```text
startImport 前 join 已完成的 worker
导入完成后禁用“开始导入”
添加新文件时清除 m_hasResult，重新允许导入
```

### 2. 挂机点结果没有建议 Y

`AfkDialog` 表格新增：

```text
绝对 Y = afkBaseY + bestHeight
```

列数从 11 → 12，旧版/1.18+ 的基岩顶面基准均适用。

### 3. 环形扫描四档名称

统一为：

```text
撒点抽取（速度极快，结果一般够用）
预筛精修（速度一般，结果更好）
环带上界复核（速度一般偏下，结果逼近穷举级）
暴力遍布（速度最慢，结果最好）
```

同时更新状态文本和耗时确认文案。

### 4. 缓存地图比例

`CacheCoverageMap` 改为等比映射：

```text
scale = min(worldWidth→screenW, worldHeight→screenH)
居中留边，不再横向/纵向分别拉伸
```

鼠标坐标反算同步使用同一比例。

## 未修复：缓存加速与 k3

### 缓存没有加速普通搜索

当前缓存只接入：

```text
SlimeEngine 的分带缓存路径
且仅 Bedrock
```

需要检查：

```text
1. 实际是否走 cacheBandLoader
2. GPU 路径是否绕过缓存
3. 缓存 pack 是否覆盖本次 band
```

### 缓存没有加速 k3

k3 使用：

```text
AfkPlanner::plan / k3UpperBoundSeeds
```

但 `AfkPlanner` 只有 `SlimeChunkPredicate`，没有缓存句柄。

所以：

```text
k3 完全不读缓存
```

这不是 bug，是尚未接线。

## 搜索页重排规划

当前问题：

```text
“搜索模式”同时混入搜索方式和结果要求
最低密度、结果数量、模拟距离、图案分散在不同区域
```

建议分成三块：

```text
A. 搜索方式（怎么搜）
   撒点抽取
   预筛精修
   环带上界复核
   暴力遍布

B. 结果要求（搜什么）
   区域形状 / 半径 / 内半径
   最低密度
   目标数量
   模拟距离
   图案与方向锁定
   群系权重

C. 执行设置
   线程 / GPU / 内存 / 缓存
```

核心原则：

```text
搜索模式只决定采样/复核策略
最低密度等属于“结果要求”，不能放在模式下
```

需先做控件迁移和自检再改行为，避免一次改动过大。

## 下一步

```text
P0 缓存导入崩溃回归测试（真实 zip 连续导入两次）
P0 AfkDialog Y 列 GUI 断言
P1 缓存 band 命中率日志：是否真的命中
P1 k3 缓存接入设计
P2 搜索页 A/B/C 重排
P2 缓存地图截图验收
```

## 真实缓存测试（2026-11-28）

探针：`tools/bench/cache_real_probe.cpp`

缓存：部署目录 `cache`，1024 个 pack。

### 256×256 / band 256

```text
banded 31.1ms  hitPoints=65536  fillSkipped=1
plain   1.2ms
```

小范围下 band 初始化/加载开销大于缓存收益。

### 1024×1024 / band 1024

```text
banded 32.1ms  hitPoints=1048576  fillSkipped=1
plain  19.1ms
```

仍偏慢。

### 8192×8192 / band 8192（完整 pack）

```text
banded 165.8ms  hitPoints=67108864  fillSkipped=1
plain 1235.9ms
```

缓存约 7.5× 加速。

结论：

```text
缓存路径本身有效；
小范围/小 band 会被加载开销抵消；
用户感觉“无加速”更可能是范围与 pack/band 不匹配。
```

k3 仍不读缓存，需要规划器接口。
