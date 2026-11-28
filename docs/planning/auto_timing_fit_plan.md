# 计划：自动拟合耗时函数（预测不同设备/方案搭配的耗时）

> 状态：**仅规划，本轮不实现**（用户 2026-11-28 提出）。默认开启，放在设置里；
> 训练数据只来自使用者本机，不上传任何数据。

## 1. 目标

在搜索开始前给出“预计耗时”并给出可信区间，而不是靠速度基准写死公式：

- 输入：设备指纹（CPU 型号/物理核数/内存档位/GPU 名称与可用性）、搜索方案
  （模式/算法/窗口大小/范围/目标数量/缓存命中情况/环形档位）、扫描量（区块数或抽样点数）。
- 输出：预计耗时（秒）+ 置信区间 + 预计扫描速度；用于：
  - 开始前提示（替换目前写死的 `预计剩余` 线性估计）；
  - 搜索中动态修正 ETA；
  - 自动建议方案（“这个范围用分层更快”）。

## 2. 数据来源（只来自使用者自身）

- 每次搜索/构建结束后记录一条**本地样本**：

```text
timestamp | appVersion | mode | algorithm | areaChunks | scanChunks | targetCount |
cacheHit% | gpuUsed | threads | cpuModel | physicalCores | ramClass | gpuName |
phaseMs {prescan, match, refine, sort} | totalMs | cancelled
```

- 存放在程序目录 `timing_samples.tsv`（JSONL 亦可），与现有 `timing_calibration.tsv` 同目录。
- 只保留本机、最近 N 条（例如 2000 条）或按 (设备指纹, 方案签名) 聚合；**不上传**，
  匿名统计开关与本功能无关。
- 已有基础：`SlimeEngine::importTimingCalibration` / `timing_calibration.tsv`
  （同配置实测速率表）已存在，可复用其加载/持久化路径。

## 3. 模型建议（先简单、可解释，再考虑复杂）

按阶段拆成 `T_total = T_fixed + T_prescan + T_match + T_refine + T_sort`：

1. **冷启动阶段**：每设备每算法一个基线（首轮实测后写入，类 Welford 在线均值）。
2. **扫描量线性项**：`T = a * scanChunks + b`，按（设备指纹, 算法, GPU/CPU, 线程档）分组
   做带遗忘因子的最小二乘/中位数斜率更新；数据不足时回落到全局默认表（现有写死数值）。
3. **非线性修正**：目标数量大时精修数上升（`T_refine ≈ c * min(target, cap)`）；
   大范围缓存命中/未命中切换乘系数。
4. **区间**：用残差的分位数（P10/P90）给区间；样本 < 3 时标“估计，误差可能较大”。
5. 可选：按 `log1p(scanChunks)` 做特征；或小型决策树（仍本地训练）。

## 4. 设置项（设置 → 高级）

- `settings.timingFitEnabled`（默认 **开**）：自动拟合耗时函数。
- `settings.timingFitShareData`：**不存在**——不联网、不上传。
- “查看/清除本地耗时样本”按钮（隐私可见）；“重置拟合模型”。
- 关闭后回落到现有固定预测公式。

## 5. 代码落点（预估）

- `src/core/TimingModel.h/.cpp`（Qt 无关）：样本结构、特征提取、在线拟合、预测+区间。
- `timing_calibration.tsv` → 扩展为样本文件（保留旧格式兼容）。
- `MainWindowSearchResults.inc` / `MainWindowRingMap.inc`：搜索前预测、结束后 `observeSample`。
- `SettingsDialog`：开关 + 清除样本按钮；`AppSettings::timingFitEnabled`。
- 自检：给定合成样本，预测值随扫描量单调、关闭开关后回落到固定值；不依赖真实计时（避免 flaky）。
- 注意：预测不能反过来影响搜索决策（只显示提示），除非用户显式选择“自动选最快方案”。

## 6. 风险

- 计时噪声大（CPU 占用、缓存状态、GPU 驱动、首轮冷启动）→ 用中位数/分位数、至少 3 样本才给区间。
- 同一设备不同方案的样本稀疏 → 回落到“同设备同算法”再回落全局默认。
- 用户可能误以为预测是承诺 → UI 标注“基于历史样本的估计”。
- 文件增长与隐私 → 只存本机、可清除、条数上限。
