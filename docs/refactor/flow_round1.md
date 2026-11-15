# 流程架构第 1 轮：SearchJob / Progress / Outcome / 取消

分支：`flow-runtime`，基于 `main` 3.13.8。
本文件只覆盖第 1 轮；第 2 轮（CacheSnapshot、Band LRU、GPU cap 截断、GPU tile 续扫、Banded 全局早停）等验收后再做。

## 1. SearchJob 状态机

```text
Idle
  │ start()
Starting
  │ worker begin
Running
  │ requestCancel()
Stopping
  │ runner returns
Finishing
  │ done callback returns
Finished
```

规则：

- `start()` 在 `Starting/Running/Stopping/Finishing` 时返回 false，不覆盖当前任务。
- `requestCancel()` 只做阶段一：置 cancel、进入 `Stopping`、立即返回，不 join。
- `waitFor(ms)` 是阶段二：有界等待；超时返回 false，任务仍在 `Stopping`。
- `wait()` 保留给析构/收尾使用。
- `runId` 每次 start 递增；陈旧回调由调用方按 runId 丢弃。
- worker 不捕获 `SearchJob*`，只捕获 `shared_ptr<Impl>`，避免任务对象析构后的悬挂访问。

实现：

```text
src/core/SearchJob.h
src/core/SearchJob.cpp
```

## 2. 统一 Progress 契约

```cpp
enum class SearchPhase {
    Preparing, Scanning, Matching, Finalizing, Done, Cancelled, Failed
};

struct SearchProgress {
    uint64_t sequence;
    double fraction;       // 0..1，单调
    SearchPhase phase;
    int64_t done, total;   // 0 = 引擎未公开
    double chunksPerSec, etaSec;
    bool terminal;
};
```

`ProgressContract`：

- 线程安全；
- fraction 不可倒退；
- Matching 之后不允许回到 Scanning；
- 终态幂等：Done/Cancelled/Failed 之后丢弃迟到进度；
- Done 强制 fraction=1；
- 回调在锁外执行，允许调用方查询状态；
- 旧引擎的 `ProgressFn` 在 SearchJob 内适配：`scanning ? Scanning : Matching/Finalizing`。

实现：

```text
src/core/SearchProgress.h
src/core/SearchProgress.cpp
```

## 3. 统一 SearchReport

`SearchOutcome` 保留为详细载荷，`SearchReport` 作为唯一结果语义：

```cpp
enum class SearchStatus { Ok, Cancelled, Failed };

struct SearchReport {
    SearchStatus status;
    uint64_t runId;
    int64_t hitsTotal;       // 截断前命中数
    int64_t hitsReturned;    // 实际返回条数
    bool gpuScanUsed, gpuMatchUsed, cacheUsed;
    bool chunked, banded, stoppedEarly;
    std::string fallbackReason, error;
    SearchProgress progress;
    SearchOutcome outcome;   // 兼容载荷：newCacheChunks 等
};
```

规则：

- `cancelled` 优先于 `ok`：即使引擎竞态返回成功，只要请求过取消，状态就是 `Cancelled`，结果清空。
- `hitsTotal` 与 `hitsReturned` 分开；显示条数不再等于命中总数。
- `outcome` 保留缓存构建/分带等旧字段，避免迁移期丢数据。

实现：

```text
src/core/SearchReport.h
src/core/SearchReport.cpp
```

## 4. 与 SearchTask / GUI 的接入

`SearchTask` 内部改为持有 `SearchJob`，外部 API 保持不变：

```text
start(options, legacy ProgressFn, legacy DoneFn)
cancel()
wait()
running()
```

新增：

```text
state()
waitFor(ms)
progressSnapshot()
lastReport()
```

- GUI 的旧进度回调由 SearchTask 从 `SearchProgress` 适配，行为不变。
- 任务完成仍把 `SearchOutcome` 交给原 DoneFn，缓存构建字段不丢。
- MainWindow 的 `closeEvent` 改为：

```text
cancel()
waitFor(5000)
超时 -> ignore close，窗口保留
```

避免无限期 join。

## 5. 验收测试

新增：

```text
tests/integration/search_job_test.cpp
build/slime_search_job_tests.exe
```

覆盖：

- 状态：Idle → Running → Finished；
- start 在运行中拒绝；
- Progress 单调、终态 Done；
- 取消立即返回，`waitFor(50)` 超时 false；
- 取消后的成功载荷被归一化为 Cancelled，结果清空；
- 空闲取消被拒绝；
- Done 回调只调用一次。

运行：

```text
build\slime_search_job_tests.exe
SEARCHJOB ALL PASS
```

回归：

```text
slime_tests.exe --unit                 通过
GUI 自检（GPU off）                    exit=0
```

## 6. 本轮不包含

```text
CacheSnapshot / 不可变缓存视图
Band 级缓存 LRU
GPU cap 溢出截断标记
GPU 失败从已完成 tile 续扫
Banded 全局早停
环形搜索并入 SearchJob
```

这些属于第 2 轮，等第 1 轮验收后再开始。

## 7. 已知边界

- 引擎内部仍使用旧的 `ProgressFn` 发布，SearchJob 在外层做契约收敛；
  后续可把引擎各路径改为直接发 `SearchProgress`。
- `SearchTask` 的 `DoneFn` 仍是旧签名，GUI 迁移到 `SearchReport` 可在下一轮做。
- `SearchJob` 析构仍会 cancel+join；GUI 关闭路径已使用有界等待，应用退出路径保留最终 join。
- 环形搜索未接入本状态机，仍使用独立 worker。
