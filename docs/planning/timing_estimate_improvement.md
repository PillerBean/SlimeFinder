# 耗时预测优化

日期：2026-11-23  
状态：已实装，未打包

## 原问题

旧 `estimateSearch`：

```text
只采样 minX/minZ 一个 1024×1024 角落
按单线程扫描/匹配速率线性外推
没有区分开关组合
显示范围仅 0.8×–1.35×
```

不同模式/算法/cache/stop/smart/GPU 组合的速度差异很大，所以预测经常严重偏离。

## 本次修改

### 1. 三个空间样本

```text
左上、中心、右下
每个 1024×1024
取 scan/match 速率中位数
```

避免单角落群系/缓存差异导致数倍偏差。

### 2. 同配置实测校准

新增校准键包含：

```text
version / mode / algorithm / gridSize / minDensity
pattern 尺寸与绿白格数 / directionLocked
stopAtTarget / smartRange / ignoreThreshold
useCache / GPU / buildMode
layer 参数 / sample 参数 / threads / targetCount
```

每次搜索成功完成后：

```text
SlimeEngine::recordActualTiming(info.options, out.stats)
```

记录：

```text
实测总耗时 / 请求面积（主要预测量）
scanRate / matchRate / fixed overhead
```

下次同配置估算优先使用校准值。

### 3. 跨进程持久化

```text
appDir/timing_calibration.tsv
```

启动时导入，搜索成功后写回。  
避免重启后重新变回未校准。

### 4. UI

```text
未校准：显示 0.50×–2.00× 范围 + “样本外推”
已校准：显示 0.85×–1.20× 范围 + “本机同配置上次实测校准”
```

## 实测

测试配置：

```text
16,384 × 16,384 chunks
密度 / 精确 / grid=5 / minDensity=0.3
```

| 阶段 | 预计 | 实际 | 误差 |
|---|---:|---:|---:|
| 首次（三样本） | 0.098s | 0.135s | -27% |
| 同配置校准后 | 0.133s | 0.135s | -1.5% |

这说明：

```text
首次仍受模型误差影响
同配置重复搜索的预测已接近实际
```

## 限制

```text
首次运行、未见过的新开关组合仍是样本外推
GPU 路径暂不记录（无法从 stats 判断是否真的用了 GPU）
温度/后台负载/驱动仍会造成波动
校准表按配置键匹配，不做机器学习
```

## 验证

```text
slime_timing_estimate_tests  通过
unit                          通过
GUI 自检                     通过
```

## 文件

```text
src/core/SlimeEngine.cpp/.h
src/gui/mainwindow/MainWindowServices.inc
src/gui/mainwindow/MainWindowSettingsUi.inc
src/gui/mainwindow/MainWindowSearchResults.inc
tests/integration/timing_estimate_test.cpp
```

## P0 追加（2026-11-23）

### 实际 GPU 路径校准

`recordActualTiming` 现在接收 `gpuScanUsed`，校准键区分：

```text
GPU 开关打开
实际用了 GPU，还是回退 CPU
```

估算优先查实际 GPU 键；找不到时再查 CPU 回退键。

### 预测/实际误差遥测

每次经过整图确认并成功完成的搜索写入：

```text
appDir/timing_predictions.tsv
```

字段：

```text
timestamp_ms / predicted_s / actual_s / ratio
gpu_enabled / gpu_used / cache / stop / smart
mode / algorithm / key
```

汇总脚本：

```powershell
python scripts/qa/timing_predictions_report.py
```

按模式/算法/GPU/cache/stop 分组输出：

```text
count / median ratio / p90 |log error| / max ratio
```

### 搜索中 ETA

进度回调现在用最近一段的 `done` 增量估算瞬时速度：

```text
instant = Δdone / Δtime
remaining = (total - done) / instant
smoothed = 0.65 × previous + 0.35 × remaining
```

阶段切换或 done 回落时重置，避免用“开跑至今平均速度”导致长时间偏大/偏小。

### 当前限制

```text
遥测只记录经过整图估算确认的搜索
首次/新组合仍使用三点样本
GPU 校准依赖驱动真实执行路径
```

### 提前停止首轮修正

普通样本仍关闭 `stopAtTarget`，避免样本被目标数截断。  
另外跑一组开启提前停止的三点样本；若样本确实提前结束，用：

```text
实际扫描区块 / 样本面积
```

作为全图扫描比例修正，夹在 2%–100%。  
这解决旧模型把“只扫一小部分”预测成“扫完整图”的主要偏差。

同配置第二次起仍优先使用实测校准。
