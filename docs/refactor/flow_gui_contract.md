# GUI 契约迁移（flow-gui-contract）

分支：`flow-gui-contract`，基于 `flow-runtime`。

## 迁移内容

新增 `SearchTask::startReport`：

```text
SearchProgressSink  → 进度
ReportDoneFn        → SearchReport 完成回调
```

GUI 的两条启动路径都改为：

```text
普通搜索   startSearch → m_task.startReport
缓存构建   startBuildBand → m_task.startReport
```

完成回调：

```text
onSearchFinishedReport(report, info, runId)
  → m_lastReport = report
  → onSearchFinished(report.outcome, ...) 兼容现有结果渲染
```

进度回调：

```text
const SearchProgress& progress
  fraction / phase / chunksPerSec / etaSec
```

构建模式也按 `SearchPhase::Preparing/Scanning` 判断扫描阶段。

## 兼容边界

`SearchReport::outcome` 仍保留，用于：

```text
结果列表
缓存构建统计
已有自检断言
```

这次迁移把**入口和完成通知**切到新契约；结果渲染仍消费 `outcome`，
避免一次性重写所有结果/缓存/地图代码。

## 验证

```text
slime_search_job_tests   ALL PASS
slime_cache_band_tests   OK
GUI 自检（GPU off）     exit=0
```

## 发布命名

```text
flow-gui-contract-SlimeFinder-3.13.9
```

环形搜索不在本分支，见 `flow-ring-job`。
