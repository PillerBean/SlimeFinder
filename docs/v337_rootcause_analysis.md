# v3.3.7 根因分析报告（先分析、待确认后再改代码）

日期：2026-09-20 ｜ 目标版本：v3.3.7（紧急 Bug 修复）｜ 基线：v3.3.6
任务书：`模板：原始文件/3.3.7紧急修复任务书.txt`
用户场景：基岩版 · 严格图案（罕见）· 范围 ±104857（总 439 亿区块）· 目标 1 · 12 线程 · 未启用 GPU

> 本文档为**只读排查结论**（未改动任何代码）。任务书要求先输出根因分析、由用户确认后再动手，
> 因此 §六 给出候选修复方案（含取舍），请确认后我再逐项实施与验证。
> 实测证据：`docs/v337_rootcause_evidence.txt`（用 `build\slime_tests.exe` 复现，只读运行，576 行原始输出）。

## 〇、实测数据总表（复现用户场景，全部只读运行）

| 实验 | 场景 | 总耗时 | 内存峰值 | 块完成情况 |
|---|---|---|---|---|
| A1 | 严格图案 · 5 块（每块 4882 行 × 209715 列）· 目标 1 · **不勾**及时反馈 | 92.85 s | 507 MB | 全部 5 块扫完（块完成间隔 83.10 s + 0.23/0.02/0.15 s） |
| A2 | 同上 · **勾选**及时反馈（`--stop`） | 92.79 s | 507 MB | **同样全部扫完**（间隔 83.22 s + 0.27/0.10/0.05 s）→ 勾选与不勾选**完全等价** |
| A3 | 严格图案 · **64 块**（`SLIME_CHUNK_WORDS=1048576`）· **常见图案**（1×1 绿）· 目标 1 · 勾选 | 40.67 s | 103 MB | **全部 64 块扫完**（逐个记录到 63 个完成间隔）；若提前停止生效，第一波 12 块（≈7 s）即应结束 |
| B | 密度 g=5 · 13 块 · 12 线程并发 | 106.64 s | **1482 MB** | 全部 13 块（12 块并发在飞）→ 与 12 × 16M words × 8B = 1.5 GB 理论值吻合 |

补充观察：单块位图填充速率为 **≈1.1×10⁷ 区块/秒/线程**（319 行块 ≈6 s、4882 行块 ≈83~106 s），
即用户 439 亿区块在 12 线程下约 **6~10 分钟**量级（纯扫描），期间进度只在块边界更新（见 P0-3）。

---

## 一、任务书 §七 的 8 个问题（逐条回答）

### Q1. `runChunked` 中 `sub.stopAtTarget = false;` 是否存在？为什么这么设置？

**存在。** `src/core/SlimeEngine.cpp:2078`：

```cpp
sub.smartRange = false;   // 分块路径按光栅顺序（结果与顺序无关，仅影响“智能”早停）
sub.stopAtTarget = false; // 整图分块不提前停止（保证全局最近 N；UI 文案注明）
sub.threads = 1;          // 块内单线程，并行由块间承担（内存有界）
```

**为什么这么设置（设计意图，可从代码复原）**：经典路径的 `stopAtTarget` **不是**「找到 N 个就停」，
而是一个**几何提前停止**：堆满 N 个后取当前第 N 名的距离 `nth = heap.worstDist()`，并计算「剩余行区域
到起点的最近可能距离 L」，当 `nth <= L` 时停止（`SlimeEngine.cpp:1927-1942`）——这样停止后结果**仍是
全局最近 N 个**。

分块路径为了同样的「全局最近 N」语义，把每个块的结果按 (距离, minX, minZ) 归并（`:2022-2033`、
`:2106-2112`、`:2188-2190`），而**跨块的提前停止需要全局堆 + 各块距离下界**，v3.1.1/v3.1.2 实现时分块
路径选择了「不做提前停止」，并把这一点写进了注释，同时把 UI 文案改成「整图分块不提前停止」。

**结论**：这是**有意的设计决策**，但它使用户勾选「达到目标数量即停止」在 439 亿这类必走分块的范围里
**完全失效**——与用户预期直接冲突，属于必须修的 Bug（§三 P0-1）。

### Q2. 分块路径如何检查「所有块已完成」？是否有「达到目标数量即停止」的检查？

* 「所有块完成」靠**取块计数器 + 固定线程池**：`nextChunk.fetch_add(1)`，`ci >= nChunks` 时 worker 退出
  （`:2149-2164`），全部 `join` 后统一归并结果。
* **没有任何「达到目标数量即停止」的检查**：worker 循环里只有两个 break 条件——`ci >= nChunks`
  与 `cancel`（`:2157-2158`）；`totalFound` 只在块完成后累加（`:2113`）且**从未被用于停止判断**。
* 逐块内部：`sub.stopAtTarget = false`（Q1），且**引擎的提前停止判定被显式禁用**（`:1770` 的
  `slideOk` 与 `:1927` 的 `opt.stopAtTarget` 条件都要求该标志为真）。

**实测验证（`docs/v337_rootcause_evidence.txt` 实验 A1/A2/A3）**：
* 实验 A2（严格图案、5 块、目标 1、**带 `--stop`**）：仍然把 **5 块全部扫完**才结束（92.79 s）；
  而实验 A1（同样的范围与图案、**不带 `--stop`**）为 92.85 s —— 两者**完全等价**，
  证明该选项在分块路径上「勾与不勾一个样」。
* 实验 A3（64 块、**常见图案 1×1**、目标 1、带 `--stop`）：**64 块全部扫完**（40.67 s，逐块完成记录可见）；
  若标志生效，第一波 12 块（≈7 s）就已命中目标，应立刻停止 → 实际多算了 52 块、多花约 33 s
  （范围越大差距越悬殊；用户 439 亿场景是 44 块 × ~1.02e9 区块/块）。
* 作为对照，经典路径（小范围、非分块）的 `--stop` 会按几何判据提前结束——即同一选项在两条路径上
  行为不一致。

### Q3. `liveMatches` 的生命周期如何管理？MainWindow 析构时搜索线程是否可能访问悬垂指针？

`liveMatches` 是 **v3.3.6 我新加的实时命中计数**（`SearchOptions::liveMatches`，`SlimeEngine.h:212`），
GUI 侧是 `MainWindow` 的成员 `std::atomic<int64_t> m_liveMatches{0}`，在 `startSearch()` 里
`opt.liveMatches = &m_liveMatches`（`MainWindow.cpp:3398`），引擎在 `recordMatchAt` 中以 relaxed 原子
自增（`SlimeEngine.cpp:1410-1413`）。

生命周期审查结论：

1. **正常路径安全**：`SearchTask` 是 `MainWindow` 的成员，`~SearchTask()` 会 `cancel()+wait()`；
   而且 `~MainWindow()` **函数体**里已经先做了 `if (m_task.running()) { cancel(); wait(); }`
   （`MainWindow.cpp:530-536`），成员析构发生在函数体之后，因此正常析构时线程已 join。
2. **存在一个真实的窄窗口（竞态 #1）**：`SearchTask` 线程先 `m_running.store(false)`，**之后**才调用
   `fn(out)`（`SearchTask.cpp:25-26`）。如果用户恰在此时关闭窗口，`~MainWindow()` 函数体看到
   `running()==false` → **不 join**；线程随后仍会执行 `fn(out)`（其中捕获了 `this` 并调用
   `QMetaObject::invokeMethod(this, …)`），而 MainWindow 的成员正在被销毁 → **UB / 可能崩溃**。
3. **由此派生的原子悬垂（竞态 #2）**：`MainWindow.h` 中 `m_liveMatches` 声明在 `m_task` **之后**，
   成员析构顺序是「后声明者先析构」→ `m_liveMatches` 会**先于** `m_task`（以及 `~SearchTask` 的 join）
   被销毁；在上述窗口里引擎若仍在跑，`liveMatches->fetch_add` 就会写已析构的原子对象 → UB。
   单独看正常路径不会触发，但它与竞态 #1 组合即可构成「取消后崩溃」的候选原因。
4. **结论**：任务书猜测「MainWindow 析构时搜索线程可能访问悬垂指针」**成立**（在竞态窗口内），
   修复方向建议按任务书：把计数改成 `std::shared_ptr<std::atomic<int64_t>>`，由 `SearchTask` 持有，
   引擎只拿裸指针，生命周期与线程绑定；同时把 `~MainWindow()` 的 join 改成**无条件**执行。

另外排查了其它「取消后崩溃」候选，**已排除**：
* GPU 提供者悬垂：`ensureGpuScanOnce()` 只在首次调用时创建，成功后**不会** `reset()`（`:683-698`），
  且搜索期间 GPU 复选框被 `setInputsEnabled(false)` 禁用（`MainWindow.cpp:2622`）→ 不成立。
* `g_anchorMaxAZ` 泄漏：`runChunkBody` 在 `runImpl` 返回后立即恢复 `INT64_MAX`（`:2088`），
  且 `thread_local` 每线程独立；只有 `runImpl` 抛异常才会跳过恢复，而 `runImpl/runChunked/run`
  都是 **`noexcept`**（`SlimeEngine.h:290/309/314`）→ 抛异常会直接 `std::terminate`（见 Q7）。
* 取消时误置 `anyError`：`runChunkBody` 在检查 `so.ok` 之前先判 `cancel`（`:2094`），取消的块不会污染
  错误状态；`runChunked` 也先判 `cancel` 再判 `anyError`（`:2166-2175`）→ 任务书猜测 #1 不成立。

### Q4. 取消路径的完整实现：从用户点击到线程退出，资源清理顺序是什么？

```
[GUI 线程] 用户点「取消」
 1. MainWindow::cancelSearch()            MainWindow.cpp:3505
      m_task.cancel();                     → SearchTask::m_cancel = true（atomic）
      m_w->cancel->setEnabled(false); setStatus("正在取消…"); Logger::info(...)
[引擎工作线程]（1~12 个分块 worker，外加可能的块内 OpenMP 串行区）
 2. 各检查点观测到 cancel：
      · 取新块前（SlimeEngine.cpp:2158）、块体返回后（:2094）
      · 扫描阶段：每 16384 列 + 每行（:1104/1114/1131/1149/1163）
      · 匹配阶段：逐行 / 每窗口块（:1592/1606/1617/1656/1665/1681/1720/1730/1749/1821/1866/1894）
 3. 块体提前返回：先把 thread_local g_anchorMaxAZ 复位（:2088），再 return（:2094）
 4. 线程池 join 全部 worker（:2163）
 5. runChunked 观测 cancel → out.cancelled = true，直接返回（:2166-2170）
      —— 注意：**取消时不归并 cachePieces、不写缓存**（有意为之，避免半成品缓存）
[搜索线程（SearchTask 的 std::thread）]
 6. engine.run(...) 返回 → m_running = false（SearchTask.cpp:25）→ fn(out) 投递队列事件（:26）→ 线程结束
[GUI 线程]
 7. onSearchFinished(cancelled)           MainWindow.cpp:3554
      m_running=false → setRunningUi(false)（停 500ms 流水定时器 / 恢复工具栏）
      setInputsEnabled(true)；进度归零；文字「已取消」+ 流水追加「已取消（已扫描 N 区块）」
      updateNavBadges()；随后 m_task.wait()（此时线程通常已结束，仅做 join 收尾）
 8. 资源释放在各层返回时发生：每块体内部的 128MB 位图 std::vector 随块体返回释放；
      best / cachePieces / 线程对象随 runChunked 返回释放
```

**发现的两个状态机缺陷（与取消直接相关，建议一并修）**：

* **竞态 #3（陈旧结果事件）**：`onSearchFinished` 里调用了 `m_task.wait()`。正常情况下该事件由搜索
  线程自己投递、线程已退出，join 立即返回；但若用户**取消后立刻再次点「开始搜索」**，`startSearch()`
  → `m_task.start()` 会先 `cancel(); wait();` 收尾旧线程并**启动新线程**；此时**排队的旧
  `done(cancelled)` 事件**才在 GUI 线程执行 → 它会 `m_task.wait()` **join 到新搜索**（GUI 冻结整个
  新搜索过程），并把 `m_running`、输入可用性等状态错置。视觉表现就是「取消后再搜索 → 卡住/异常」。
  修复建议：给每次运行分配 `runId`（或 `shared_ptr<RunToken>`），事件里带 id，非当前 id 直接丢弃；
  并且**不要**在处理结果回调里 `wait()`。
* **取消响应时长**：扫描/匹配检查点足够密（每 16384 区块或每行），理论响应 ≪ 1 s；但块体开头有一次
  **128MB 位图的分配 + 清零**（12 线程并发时共约 1.5GB 触碰），内存紧张时这一步可能耗时数百毫秒到
  数秒，且期间无 cancel 检查点 → 属「取消响应」验收的风险点，建议在分配前加一次 cancel 短路
  （§六 P0-2 修复项）。

### Q5. 扫描计数的数据源：`m_streamScanned` 如何计算？与引擎 `scannedChunks` 的关系？

* GUI：`m_streamScanned = frac × m_runTotalChunks`（`MainWindow.cpp:3492`），其中
  `m_runTotalChunks = area × areaZ`（用户场景 = 439 亿），`frac` 来自引擎进度回调。
* 引擎（分块路径）：`frac = chunksDone / nChunks`，**只在每块体结束时上报一次**
  （`SlimeEngine.cpp:2135-2146`），`runImpl` 被以 `progress=nullptr` 调用（`:2087`）→ **块内没有任何进度**；
  最终 `out.stats.scannedChunks = totalEvals = plan.totalDataEvals × W`（`:2192`）。
* 因此存在**两层偏差**：
  1. **粒度偏差（主因）**：一个块体 = `dataRowsCap × W` ≈ 4882 × 209715 ≈ **10.2 亿区块**，
     实测**单块耗时 83~106 秒**（证据文件实验 A1/A2/B）→ 进度条/计数**每 1~2 分钟才跳一次**；
     而 500ms 流水定时器仍按节拍输出，于是出现「连续几十行都是同一个 57 亿」的「停滞」现象。
     `57.15 亿 / 439 亿 = 13.0%` —— 与用户观察完全吻合（≈ 5~6 块完成，44 块计划）。
  2. **口径偏差**：`area×areaZ` 忽略了块间重叠（每块多扫 `hMax-1` 行）；引擎真实口径是
     `totalDataEvals × W`。hMax=3、44 块时差 ~0.04%，但图案行数/网格较大时（hMax 可达 64/512）
     重叠会到百分之几。
* 另外，`progress` 里的 `cps` 也是用 `totalEvals` 与「已完成后块数」推算的（`:2140-2143`），
  在块内同样长时间不变 → 显示的速度与 ETA 也会「冻结」。

### Q6. UI 重复显示的来源：progressStage、progressInfo、progressPercent、progressEta 分别设置什么？

根因是 **v3.3.6 我接入进度行时把同一段文本写给了两个标签**（`MainWindow.cpp:3482-3483`）：

```cpp
if (m_w->progressStage) m_w->progressStage->setText(stageText);
if (m_w->progressInfo)  m_w->progressInfo->setText(stageText);   // ← 重复源
```

而 `stageText = progressStageText(0, pct, cps, etaSec)` 本身**包含百分比与预计剩余**
（`MainWindow.cpp:256-259`）：

```
"正在扫描区块… %1% ｜ 速度 %2 万/秒 ｜ 预计剩余 %3"
```

叠加 `progressPercent`（"15%"）与状态栏 `progressEta`（"预计剩余：…"）后：

| 位置 | 内容 | 重复情况 |
|---|---|---|
| 进度行 `progressPercent` | `15%` | 第 1 个百分比 |
| 进度行 `progressStage` | `正在扫描区块… 15% ｜ 速度 … ｜ 预计剩余 15:07` | 第 2 个百分比 + 第 1 个预计剩余 |
| 状态栏 `progressInfo` | 同一段 `stageText` | **第 3 个百分比 + 第 2 个预计剩余** |
| 状态栏 `progressEta` | `预计剩余：15:07` | 第 3 个预计剩余 |

→ 与用户现象「顶部 3 个百分比、底部 2 个预计剩余」逐条对应（用户截图里的 `---` 是 ETA 为 0/超限时的
`--:--` 占位，与有效值同时存在）。

### Q7. 崩溃转储的默认值是什么？在哪里配置？

* **安装位置**：`src/main.cpp:187` `slime::CrashDump::install(base / "crashes")` —— **无条件安装**，
  即“默认开启”在行为上已成立。`CrashDump::install()` 同时设置
  `SetUnhandledExceptionFilter(exceptionFilter)` 与 `std::set_terminate(terminateHandler)`
  （`CrashDump.cpp:102-110`），两条路径都会写 `crashes/SlimeFinder_<时间戳>.dmp` 与
  `crashes/crash_report.txt`。
* **设置项是「死设置」**：`AppSettings::crashDump`（默认 true）只在设置界面/`config.json` 读写
  （`SettingsDialog.cpp:482/662/711/754`、`MainWindow.cpp:662/1667`），**没有任何代码消费它**
  —— 既不能关闭转储，也不能在运行期切换。任务书「崩溃转储独立于普通日志、默认开启」需要把它真正接线。
* **为什么这次没有 dump（关键）**：既然 terminate/UEF 两条路径都会写 dump，而用户既没有 dump 也没有
  `crash_report.txt`，说明崩溃**绕过了所有注册的处理入口**，最可能是以下两类（Windows 上不产生
  可捕获异常）：
  1. **fast-fail**：堆损坏（`STATUS_HEAP_CORRUPTION 0xC0000374`）、`__fastfail`、
     CRT 非法参数（默认 `_invalid_parameter` 处理）→ 直接终止进程，**不调用 UEF、不调用 terminate**；
  2. **线程栈溢出**（`0xC00000FD`）：VEH 虽被调用但栈已不可用，写 dump 往往失败。
  另外 `main.cpp:166` 的 `AddVectoredExceptionHandler(1, crashVeH)` **只打印、不写 dump**
  （返回 `EXCEPTION_CONTINUE_SEARCH`），因此首次机会（first-chance）阶段没有留下转储。
  → 这解释了「窗口直接消失、无 dump、无日志」。修复见 §六 P0-2/P1-6。

### Q8. 内存峰值实测多少？分块策略是否正确工作？

* **分块策略工作正常**（几何、并发、结果归并都正确）：以用户同样的宽度（W=209715）实测
  13 块 × 12 并发，退出码 0、结果一致；`SLIME_DEBUG=1` 可见每块 `rows=4882`、`wpr=3277`、
  `nthreads=1`，与 `chunkPlanFor()` 的公式一致。
* **内存峰值实测**（无缓存、12 块并发、默认 128MB/块）：**1482 MB**（实验 B，证据文件），
  与「12 × 16,000,000 words × 8B = 1.5GB」的理论值吻合 → 任务书 P0-4 的推断成立。
  （对照：实验 A 只有 5 块在飞 → 峰值 507 MB；实验 A3 用 1M words 小位图 → 103 MB，
  可见峰值与「并发数 × 单块位图」严格成正比。）
* 用户观察到的 **2~3GB** 需要用**基岩 + 使用缓存**路径解释（代码推算，尚未实测）：
  `collectCacheChunks` 会把块位图缩到 2.5M words（≈20MB），但每块返回的
  `so.newCacheChunks` 约为「本块行数 × 宽度 × 命中率」，762 行 × 209715 × ~10% ≈ **16M 条目 ≈ 128MB/块**，
  12 块在飞 ≈ 1.5GB；再叠加 `cachePieces` 最多保留 `cacheBudget`（默认 1GB）→ 合计约 **2.3~3GB**，
  与用户「2~3GB」吻合。
  （若需要，我可以用一个**临时**（不进仓库）的小 harness 精确复测这条路径的峰值。）
* 另外两个放大因素：① 每块都重新分配/清零 128MB（`std::vector` 首次触碰时提交物理页），
  12 块并发就是 1.5GB 的瞬时提交，属「内存压力 + 无 cancel 检查点」的叠加风险；
  ② 结果集 `best`、`so.results`、`cachePieces` 都在原有峰值上叠加。

---

## 二、P0-1 根因（及时反馈死循环）

**根因**：分块路径**三重缺失**——`sub.stopAtTarget` 被硬编码为 `false`（`:2078`）；worker 取块循环没有
「已达目标」判断（`:2155-2160`）；`totalFound` 只统计不消费（`:2113`）。439 亿区块必然走分块路径
（`gridWords = 687M words > kClassicWordsLimit = 64M`，`:820-823`），因此勾选完全失效。

**用户现象的完整解释**：找不到结果时引擎会一直扫到范围结束（这是正确语义，但耗时 ≈ 全量扫描）；
期间进度每块才动一次（P0-3），于是表现为「不停 + 计数不动」。

**实测**：实验 A2（5 块 94.09 s）、A3（64 块，`--stop` 仍全扫）。

**修复候选（需用户选择，涉及语义）**：

* **方案 A（任务书字面要求）**：删除硬编码，worker 每完成一块检查 `totalFound >= targetCount`，
  置 `stopRequested` 原子标志让 worker 不再取新块；块内恢复 `sub.stopAtTarget = opt.stopAtTarget`。
  → 「找到 N 个立即停」字面成立；**代价**：返回的 N 个是「最先找到的 N 个」，**不再是全局最近 N 个**
  （与经典路径的几何停止语义不一致，属于精度降级，任务书 §四 明令「不做降级处理」）。
* **方案 B（推荐：语义无损地实现「找到就停」）**：块内保留几何提前停止（`sub.stopAtTarget = true`），
  并在**跨块**增加剪枝：每块开跑前计算「起点到该块矩形的最小可能距离」`distToRect(start, chunkRect)`，
  若全局堆已满且 `distToRect >= 全局第 N 名距离`，则**跳过该块**（仅记进度，不扫描）。
  这在数学上与经典路径的提前停止等价（**结果仍是全局最近 N**），且用户场景（起点在中心、命中点
  在中心附近）通常在第一波之后就停止剩余所有块；找不到结果时仍会扫完整个范围（这是语义必然）。
* **方案 C**：方案 B + 一个显式开关「达到目标数量即停止（结果可能不是全局最近 N）」，把方案 A 的
  行为作为可选项交给用户。

---

## 三、P0-2 根因（取消后崩溃）

**已确认的缺陷（按可信度排序）**：

1. **`~MainWindow` 的 join 不是无条件**（竞态 #1/#2，见 Q3）：`if (m_task.running())` 会漏掉
   「线程已置 running=false、尚未执行 done 回调」的窗口；配合 `m_liveMatches` 的析构顺序问题，
   构成真实 UB。→ 建议：无条件 `cancel()+wait()`；`liveMatches` 改为 `shared_ptr` 由 SearchTask 持有；
   done 回调改用 `QPointer<MainWindow>` 守卫。
2. **取消后立即重搜的状态机缺陷**（竞态 #3，见 Q4）：陈旧 `done(cancelled)` 事件会 join 到新搜索
   并错置状态。→ 建议：`runId`/`RunToken` + 结果回调不再 `wait()`。
3. **崩溃不可诊断**（无 dump、无日志，见 Q7）：需按 P1-6 加固（first-chance 写 dump、
   `_set_invalid_parameter_handler`、`std::set_new_handler`、常开的关键信息日志）。
4. **未排除的可能原因**：并行分块路径（12 线程）中的内存损坏类故障——静态审阅未发现明显竞争
   （`best`/`cachePieces`/计数都在锁或原子保护下），但要**定位**必须靠加固后的 dump 与压力复现
   （连续取消 10 次 + 约 1.5GB 内存压力）。

**取消响应**：检查点足够密（每 16384 区块 / 每行），理论 ≪1 s；风险点在块体开头的 128MB
分配+清零期间无检查点（建议加 cancel 短路 + 位图缓冲复用）。

---

## 四、P0-3 根因（扫描计数停滞）

见 Q5：**块内零进度上报**是主因（单块 93~106 s，实测），`area×areaZ` 与引擎 `totalDataEvals×W`
的口径差是次因，500ms 流水定时器把「块级跳变」渲染成「长时间不动的数字」。

**修复候选**：
1. 引擎内新增 `std::atomic<int64_t>* liveScanned`（真实已扫描区块数），由位图填充循环按行累加，
   并让分块路径的进度回调改为「全局 =（已完成块 evals + 当前块已完成行 × W）/ totalEvals」；
2. 块内每 N 行（如 64 行或 ~200ms 节流）回调一次进度，匹配阶段按行回调 → 进度条与计数连续增长；
3. GUI 的 `m_streamScanned` 直接取引擎的 `liveScanned`（消除重叠口径差），并在流水行里同时显示
   「扫描 N/M 区块（xx%）」；
4. 顺带把 `progressStageText` 里重复的百分比/ETA 拆掉（P1-5）。

---

## 五、P0-4 根因（内存占用过大）

实测 1479 MB（12 × 128MB 位图，无缓存）；缓存路径推算 2.3~3GB（Q8）。

**修复候选（core 层，保持结果不变）**：
1. **按内存预算决定并发**：`conc = clamp(min(threads, kChunkThreads), 1, budget / perChunkBytes)`，
   `budget` 取「可用物理/提交内存的一部分」（例如 ≤1.5GB 用于位图），并支持
   `SLIME_CHUNK_BUDGET_MB` / `SLIME_CHUNK_THREADS` 覆盖（测试用）；
2. **位图缓冲复用**：worker `thread_local` 复用一块位图，只清零实际使用的行 → 去掉 12 次 128MB 的
   反复分配/提交（同时显著降低 OOM/崩溃概率，直接服务 P0-2）；
3. **缓存路径改为「逐块即时归并 + 立即释放」**：现在 `cachePieces` 最多保留 1GB 才裁剪
   （`SlimeEngine.cpp:2119-2132`），改为每块在锁内直接 `mergeRegion` 后立刻释放该块条目向量，
   把峰值压到「并发数 × 单块条目」，预计把缓存路径峰值从 ~2.5GB 降到 ~1.5GB 以内；
4. 可选：大位图用 `VirtualAlloc(MEM_RESERVE|MEM_COMMIT)` + 仅触碰实际使用的行，减少提交尖峰；
5. 性能日志记录峰值内存，作为回归指标（写进 `docs/performance-*.log`）。

验收对应：≤4GB（当前无缓存 1.48GB 已达标；缓存路径按 3 修复后达标）。

---

## 六、P1-5 / P1-6 / P2-7 根因与修复候选

* **P1-5（UI 重复显示）**：Q6 已给出精确来源。修复：`progressStage` 只放阶段短语
  （「正在扫描区块…」「正在匹配图案…」「正在排序结果…」）；百分比只由 `progressPercent` 显示；
  ETA 只由状态栏 `progressEta` 显示；`progressInfo` 改放「已扫描/找到/速度」等**不重复**的信息；
  同步更新自检断言（v3.3.6 第 [14] 组里与进度行相关的断言）。
* **P1-6（日志/崩溃转储）**：Q7 已定位（dump 已装但设置是死设置、崩溃绕过处理器、没有关键信息日志）。
  修复：①`crashDump` 接线（main 启动前读取；设置里切换即时生效，默认开）；②新增
  `utils/IncidentLog`（始终可用，独立于 LogBus）写 `logs/crash-YYYY-MM-DD.log`：启动环境、搜索参数、
  每 N 秒一条状态环（扫描数/进度/内存）、取消/错误/terminate/VEH 事件与最后状态；③若上次会话存在
  未确认的 crash 日志，启动时在反馈栏 + 状态栏提示（最小 UI 改动）。
* **P2-7（GPU 慢一倍）**：代码层确认**严格图案模式下引擎根本不使用 GPU**
  （`gpuMatchWanted` 要求 `opt.mode == Mode::Density`，`SlimeEngine.cpp:844-848`），所以「启用 GPU 反而慢」
  在严格模式下不是 GPU 计算造成的。排查到两个真实机制：
  ① 勾选 GPU 会触发**后台实测基准**（`onGpuCheckChanged` → `startGpuBenchmark()`），它会用 12 线程跑
  CPU 扫描，若紧接着开始搜索就会**抢占 CPU**；② GPU 状态查询/检测本身也占用时间。
  → 按任务书 P2「无代码修改」处理，但建议（可放到 v3.3.8）：搜索开始时取消/跳过基准、或仅在密度模式
  且用户主动点「重新测量」时才跑。

---

## 七、修复顺序与验证计划（按任务书 §九：一次只修一个，修一个验一个）

| 顺序 | 修复项 | 关键验证 |
|---|---|---|
| 1 | P1-6 前置：dump 加固 + 关键信息日志（先有诊断能力） | 人为触发 terminate/非法参数 → `crashes/*.dmp` + `logs/crash-*.log` 均生成；`crashDump` 开关真实生效 |
| 2 | P0-2：`~MainWindow` 无条件 join、`liveMatches` 改 shared_ptr、结果事件带 runId、取消后立即重搜不再卡/错置 | GUI 自检新增：连续取消 10 次（均在块体中途）+ 取消后立刻重搜 + 关闭窗口竞态；exit 0；无 dump |
| 3 | P0-1：按选定方案（A/B/C）实现提前停止 | 单元测试：分块 + 目标 N（常见图案）→ 结果数达标且**扫描块数明显少于总块数**；A3 场景耗时从 40.67 s（64 块全扫）降到 ≈7 s（1 波 + 剪枝）；未找到时正常结束 |
| 4 | P0-3：真实扫描计数 + 块内进度回调 | 实测：计数单调增长、每 ≤1s 更新、与百分比一致、完成时接近 totalEvals；GUI 流水行样例 |
| 5 | P0-4：内存预算并发 + 位图复用 + 缓存即时归并 | 439 亿同构场景峰值 ≤4GB（用实验 B/C 同口径复测并留证据）；结果与 v3.3.6 逐位一致（111/111 + 分块对照） |
| 6 | P1-5：UI 去重 | GUI 自检断言：进度行仅 1 个百分比、状态栏仅 1 个「预计剩余」；截图 |
| 7 | 全量回归 + 打包 | clean 重建 0 error、单元 18 组、111/111、内核 17/17、OpenCL PASS、GUI 自检开/关 exit 0、污染审计 CLEAN、dist/zip 哈希一致、`docs/verification_report_v337.md`、截图（进度条/状态栏/搜索记录）、`dist/SlimeFinder-3.3.7-win64.zip` + `SHA256SUMS-3.3.7.txt` |

**需要你先确认的三件事**：

1. **P0-1 选哪个方案**：A（字面「找到即停」，结果不再是全局最近 N）／**B（推荐，语义无损的提前停止）**／
   C（B + 可选开关提供 A 的行为）？
2. **P0-4 的内存预算上限**：任务书验收是「≤4GB」；我建议把**位图并发预算**压到 1.5GB
   （12 线程 → 仍保持 12 并发但位图 128MB×12=1.5GB，符合验收），并另外把缓存路径峰值压到 ≤2GB。
   是否需要更激进（例如 ≤1GB，代价是并发降到 8 → 速度约降 30%）？
3. **P0-3 的显示口径**：流水行的「扫描 N 区块」改为引擎真实计数后，数值会比现在略大（含块间重叠行）；
   是否接受在行里同时显示「N/M（xx%）」以完全对齐进度条？
