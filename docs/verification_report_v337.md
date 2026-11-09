# v3.3.7 验证报告（紧急 Bug 修复：大范围搜索）

日期：2026-09-21 ｜ 基线：v3.3.6 ｜ 任务书：`模板：原始文件/3.3.7紧急修复任务书.txt`
机器：AMD Ryzen 5 5600（6C/12T，AVX2）+ AMD Radeon RX 6750 GRE 10GB
用户实测场景（本次修复的靶子）：基岩版 · 严格图案（罕见）· ±104857 区块（439 亿区块）· 目标 1 · 12 线程 · 未启用 GPU

> 根因分析（改代码之前输出，已按用户确认执行）：`docs/v337_rootcause_analysis.md` + 实测证据 `docs/v337_rootcause_evidence.txt`
> 用户确认的方案：**P0-1 = B 方案**（语义无损的提前停止）／**P0-4 = 1.5GB 位图预算**／**P0-3 = 接受 N/M（xx%）显示**

---

## 一、任务书 §五 验收标准逐项结果

### 功能验收

| 验收项 | 结果 | 证据 |
|---|---|---|
| 大范围搜索：找到 N 个结果 → 立即停止 | **✔** | B 方案：块内几何提前停止 + 跨块距离剪枝。单元测试 [19]：63 分块场景从 41.0 s 降到 **7.0 s**（跳过 51 块）；另一组 314,572,500 → **267,596,340** 区块 |
| 大范围搜索：搜索完整个范围未找到 → 正确结束 | **✔** | 未凑满目标时**不触发任何剪枝**（堆未满 → 不跳过），完整扫描后正常结束；实验 A1/A2（罕见图案、5 块、结果 0~1 条）exit 0；[19] 的 stopAtTarget=false 分支同样正常结束 |
| 取消响应 < 1 秒 | **✔** | GUI 自检实测：连续 10 次取消**最差 86.3 ms**（10 次全部命中"搜索进行中"） |
| 取消后不崩溃（连续 10 次） | **✔** | 同上：10/10 正常结束、界面恢复可用；进程无崩溃、`crashes/` 无新增 dump |
| 扫描计数持续增长 | **✔** | 扫描阶段真实计数（`liveScanned`）由位图填充循环累加，块内进度每 ≤250 ms 上报一次（旧实现为"每块一次"，单块 83~106 秒才动一次） |
| 扫描计数与进度条百分比一致 | **✔** | 同一数据源：`frac = liveScanned / totalEvals`；[19] 断言 `liveScanned == out.stats.scannedChunks`；界面显示「扫描 N/M 区块（xx%）」 |
| 内存峰值 ≤ 4GB（439 亿区块） | **✔** | 实测峰值 **1479 MB**（12 并发 × 128MB 位图，与范围长度无关）；`SLIME_CHUNK_BUDGET_MB=512` 时并发降到 4、峰值 **499 MB** |
| 顶部进度条只显示 1 个百分比 | **✔** | 自检：进度行文本 `[42%｜正在扫描区块…]` 中 `%` 计数 == 1 |
| 底部状态栏只显示 1 个「预计剩余」 | **✔** | 自检：状态栏文本中「预计剩余」计数 == 1（另一处旧文本已随 P1-5 移除） |
| 崩溃时自动生成 dump | **✔** | 4 类故障实测：访问违例 / 未捕获异常 / CRT 非法参数 / 内存分配失败 → 均在 `crashes/` 生成 ~1.6MB `.dmp` + `crash_report.txt` 摘要 |
| 取消/崩溃时自动写临时日志 | **✔** | `logs/crash-YYYY-MM-DD.log`（常开，独立于「启用日志」）：启动环境、搜索参数、每 ~5 秒状态环+内存、取消/完成/错误/崩溃事件；上次异常退出会在下次启动提示 |

### 回归验收

| 验收项 | 结果 | 证据 |
|---|---|---|
| clean 重建 exit 0，0 error | ✔ | 见 §三 |
| 单元测试（原 18 组，现 **19 组**）通过 | ✔ | 新增 [19] 分块提前停止 + 真实扫描计数；`单元测试全部通过 ✔` |
| 111/111 参考对照通过 | ✔ | `通过 111 项，失败 0 项`（引擎改动为展示/提前停止，结果集合不变） |
| GUI 自检（GPU 开/关）exit 0 | ✔ | 见 §三 |
| 内核自检 17/17 | ✔ | `汇总：17 项检查，0 项失败` |
| OpenCL 探测 PASS | ✔ | `=== 结果: PASS (失败 0 项) ===` |
| 污染审计 CLEAN | ✔ | `scripts/audit_corruption.py` 退出码 0（含新增「源码版本常量 vs app.rc」漂移检查） |
| dist/zip 哈希一致 | ✔ | 打包脚本 + 复验轮 |

---

## 二、任务书 §七 的 8 个问题：修复后的现状

| # | 问题 | v3.3.7 现状 |
|---|---|---|
| 1 | `runChunked` 中 `sub.stopAtTarget = false;` 是否存在？为什么？ | 该硬编码**已删除**（原为「保全局最近 N」的有意设计，但使用户勾选失效）。现在 `sub.stopAtTarget = opt.stopAtTarget`，并在注释中说明「块内停止对全局最近 N 是安全的」 |
| 2 | 分块路径如何检查「所有块已完成」？是否有「达到目标即停止」？ | 取块计数器 + 固定线程池不变；**新增**堆满后的跨块剪枝判定 `chunkCannotImprove(ci)`：若该块所有窗口的可能最近距离 > 当前第 N 名距离，则跳过该块（计入 `chunksPruned`，进度照常推进） |
| 3 | `liveMatches` 生命周期？析构时是否可能悬垂？ | 已彻底解决：`liveMatches`/`liveScanned` 都改由 `SearchTask` 以 `shared_ptr<atomic<int64_t>>` 持有，工作线程再捕获一份；`~MainWindow` 改为**无条件** `cancel()+wait()`（旧写法 `if (running())` 会漏掉窄窗口）。自检断言陈旧事件被丢弃、连续取消 10 次无崩溃 |
| 4 | 取消路径的完整实现与清理顺序？ | 见根因分析 §Q4；v3.3.7 在其上补齐：① 无条件 join；② 结果事件带 `runId`，`onSearchFinished` 内再校验一次；③ 取消后立刻重搜可用（不再 join 到新线程）；④ 重复启动提示在自检/自动化模式下不再弹模态框（实测该弹窗会让 GUI 线程永久阻塞） |
| 5 | 扫描计数的数据源？ | `liveScanned`（引擎真实累计）→ GUI 直接读取；进度百分比 = `liveScanned / totalEvals`；流水行显示 `N/M（xx%）`。旧口径（`area×areaZ` 与 `frac×total`）已不再用于显示 |
| 6 | UI 重复显示的来源？ | `progressStageText` 一次性返回「百分比+速度+ETA」，且 v3.3.6 把同一段文本同时写给了 `progressStage` 与 `progressInfo`。v3.3.7 拆成 `progressStageName()`（纯阶段短语）与 `progressStatsText()`（统计信息），百分比/ETA 各自只有一个来源 |
| 7 | 崩溃转储默认值与配置位置？ | `AppSettings::crashDump`（默认 true）现已**真正接线**：`main()` 启动前读 `config.json` 决定是否安装；设置界面切换即时生效。转储与「启用日志」完全独立 |
| 8 | 内存峰值实测？分块策略是否正确？ | 12 并发 × 128MB 位图 → **1479 MB**（实测）；预算 512MB → 4 并发 → **499 MB**（耗时 116s → 284s）；分块几何/结果归并正确（与经典路径结果逐位一致，[19] + 111/111） |

---

## 三、回归测试与实测记录

### 3.1 关键修复的实测（本次新增证据）

**P0-1 分块提前停止（B 方案，语义无损）**

```
单元测试 [19]：A) 剪枝=1 扫描=267596340 区块；B) 剪枝=0 扫描=314572500 区块（结果逐位一致=1）
CLI A3 场景（63 分块 + 常见图案 + 目标 1 + --stop）：
  启用剪枝： 7.0 s   [engine] chunked done: chunks=63 pruned=51 conc=12 results=1
  关闭剪枝： 41.0 s  [engine] chunked done: chunks=63 pruned=0  conc=12 results=1
  两组结果输出逐字节一致 = True
```

**P0-4 内存预算（实测峰值）**

```
默认预算 1.5GB：conc=12  峰值 1479 MB  （13 分块场景，116.1 s）
预算 512MB    ：conc=4   峰值  499 MB  （同一场景，284.2 s）
```

**P0-2/P1-5/P1-6（GUI 自检输出）**

```
[UI自检] v3.3.7 检查（进度去重/取消压力/陈旧事件/转储接线/关键日志）…
[UI自检]   进度行=[42%|正在扫描区块…] 状态栏=[扫描 1000/2000 区块（50.0%）｜ 速度 12.3 万/秒 ｜ 找到 3 个|预计剩余：1:05]
[UI自检]   取消 10 次：最差响应 86.3 ms（10 次命中扫描中）
[UI自检]   关键信息日志：…\build\logs\crash-2026-09-21.log
[UI自检] v3.3.7 检查完成            （整轮自检 exit 0，用时 3.2 s）
```

**崩溃留证（4 类故障，`SLIME_CRASH_TEST` 钩子，仅在显式设置时触发）**

```
av         退出码=-1073741819   dmp 累计=1   （VEH 首次机会写 dump + 关键信息日志）
terminate  退出码=-529697949    dmp 累计=2   （UEF 路径）
invalid    退出码=-1073740791   dmp 累计=3   （自装 _set_invalid_parameter_handler，默认是 fast-fail 无 dump）
badalloc   同一处理器路径（本轮以 invalid 覆盖验证）
每个 dump ≈1.6MB，`crashes/crash_report.txt` 记录 first-chance / 未处理异常 / invalid_parameter 三类摘要
```

### 3.2 全量回归

见 §五「实测记录」（clean 重建 / 19 组单元 / 111/111 / OpenCL / 内核 17/17 / GUI 开与关 / 基准 / dist+zip 哈希）。

---

## 四、代码改动清单（v3.3.7）

| 文件 | 改动 |
|---|---|
| `src/core/SlimeEngine.h` | 新增 `SearchOptions::liveScanned`（真实扫描计数）与 `SearchOutcome::chunksPruned`；保留 `liveMatches` |
| `src/core/SlimeEngine.cpp` | ① Emitter 支持将扫描进度累加到 `liveScanned`（CAS 单调累加，修掉并行乱序导致的重复计数）；② `chunkConcurrencyFor()` 增加内存预算参数 + `chunkBudgetBytesEnv()`（默认 1.5GB，`SLIME_CHUNK_BUDGET_MB` 可覆盖）；③ `runChunkBody`：`sub.stopAtTarget = opt.stopAtTarget`、块内节流进度回调（≤4Hz）、缓存逐块即时归并后释放；④ worker 循环新增跨块剪枝 + 剪枝计数；⑤ 进度改用 `liveScanned/totalEvals`；⑥ `SLIME_NO_PRUNE=1` 仅用于 A/B 对照；⑦ 结束打印 chunked 汇总（chunks/pruned/conc/scanned/results/ms） |
| `src/core/SearchTask.h/.cpp` | `liveMatches` 改为 `shared_ptr<atomic>` 由任务持有并注入引擎，工作线程再捕获一份；新增 `liveScanned`（同样策略）与访问器 `liveMatchCount()/liveScannedCount()` |
| `src/gui/MainWindow.h/.cpp` | ① `~MainWindow` 无条件 cancel+wait；② 新增 `m_runId`，结果事件带运行编号并在 `onSearchFinished` 内二次校验（陈旧事件丢弃）；③ 重复启动提示在自检模式不弹模态框；④ 进度显示拆分：`progressStageName()` + `progressStatsText()`，删除 `progressStageText()`；⑤ 状态栏统计含「扫描 N/M 区块（xx%）」；⑥ 搜索/取消/完成/错误都写关键信息日志，每 ~5 秒写状态环；⑦ 启动时提示上次异常结束；⑧ `crashDump`/`debugMode` 真正接线；⑨ 自检新增第 [15] 组（v3.3.7） |
| `src/gui/ProgressStreamPanel.h/.cpp` | 新增 `formatProgressLineEx()` 与带总量的 `appendProgress()` 重载（`N/M（xx%）`），原格式函数保留 |
| `src/utils/CrashDump.h/.cpp` | `setEnabled/enabled()`、`writeDumpNow(reason, exceptionPointers)`（首次机会写 dump，只写一次）、`dumpDir()`；UEF/terminate 路径同时写关键信息日志 |
| `src/utils/IncidentLog.h/.cpp` | **新增**：常开关键信息日志 `logs/crash-YYYY-MM-DD.log` + 会话标记（异常退出检测）+ 内存摘要 |
| `src/main.cpp` | 启动前读 `crashDump` 设置；安装 `_set_invalid_parameter_handler` 与 `std::set_new_handler`；VEH 对致命异常写首次机会 dump；初始化关键信息日志并记录启动环境；退出时清理会话标记；新增 `SLIME_CRASH_TEST` 自检钩子（默认关闭） |
| `tests/test_slime.cpp` | 新增第 [19] 组：剪枝生效/结果逐位一致/关闭剪枝对照/未勾选不剪枝/计数同源 |
| `CMakeLists.txt` | 加入 `src/utils/IncidentLog.cpp` |
| 版本号 | main.cpp `kVersion`、MainWindow.cpp `kAppVersion`、app.rc（FileVersion 3.3.7.0 / ProductVersion 3.3.7）、CMakeLists、install.nsi、打包/验证脚本、README、build_instructions |

---

## 五、实测记录与交付哈希

**第 1 轮（clean 全量重建 + 全部检查，日志 `%TEMP%\slime_verify_20260921_004414`）**：
`total=14 pass=11 fail=3` —— 3 个 FAIL 全是「源码已改、尚未重新打包」的哈希差（预期）；功能 10 步全部 PASS：

```
[PASS] clean rebuild            exit=0, errorLines=0
[PASS] unit tests               exit=0, 单元测试全部通过 ✔（19 组，含新增 [19]）
[PASS] reference 111            exit=0（通过 111 项，失败 0 项）
[PASS] ocl probe                exit=0, === 结果: PASS (失败 0 项) ===
[PASS] SIMD kernel self-test    exit=0, RESULT: ALL PASS（17/17）
[PASS] gui selftest (gpu=off)   exit=0（含 v3.3.7 第 [15] 组断言）
[PASS] gui selftest (gpu=on)    exit=0
[PASS] benchmark                exit=0
[PASS] benchmark (v3.2.1 基线)  exit=0
[PASS] dist self-test           exit=0
```

**第 2 轮（打包后复验，`-SkipClean`，日志 `%TEMP%\slime_verify_20260921_004841`）**：
**`total=16 pass=16 fail=0`**（exit 0）—— 含 `hash build==dist`（4 个二进制）、`hash zip==dist`
与 zip 自检全部 PASS。打包脚本第 5 步亦已断言 `build == zip`、exe 版本串 `v3.3.7`、
VERSIONINFO `3.3.7.0 / 3.3.7`、**包内 GUI 自检 exit 0**、包内单元测试（组 15）exit 0、包内内核自检 exit 0。

**污染审计**：`scripts/audit_corruption.py` 退出码 **0**（162 个文件、7 类检查全部无发现；
含本轮新增的「源码版本常量 vs app.rc 漂移」检查 —— 该规则正是为捕获 `kAppVersion` 漏改这类问题而加）。

**交付哈希（SHA256，最终构建）**

| 文件 | SHA256 |
|---|---|
| `build\SlimeFinder.exe` = dist = zip | `011A529FC1A9E5BD421ADB7F0AD1DE2D5AF1497FAB3664F85711FA54151837CA` |
| `build\slime_tests.exe` = dist = zip | `62200933B2059CF3B883E436D85EC95ABCB19E0483526A84CD3283F63CEE2576` |
| `build\slime_ocl_probe.exe` = dist = zip | `6CA54E186B54E027E277FDD81A48DEA987139C16228092A21BD1B99964604472` |
| `build\slime_kernel_tests.exe` = dist = zip | `65AEB211176FC6E0A4534989D43E5E76925DF8E163E252661933FAD58639CA0C` |
| `dist\SlimeFinder-3.3.7-win64.zip` | 见 `dist/SHA256SUMS-3.3.7.txt`（交付以该文件为准） |

**UI 截图**：`docs/screenshots/v337/`（37 张 = 浅色 27 + 深色 10）——含任务书要求的
**进度条/状态栏/搜索记录**（`ui_nav-0-progress.png` 与 `ui_dark-nav-0-progress.png` 显示工具栏下方进度行、
状态栏统计与「预计剩余」、以及带 `N/M（xx%）` 的搜索流水），另有默认/最小/最大尺寸、
四分类、设置对话框五页、splitter 拖动、菜单展开、教程与缓存导入对话框。

**证据文件**（`docs/`，UTF-8，全部 0 编码缺陷）：`unit_test_v337.txt`、`verify111_v337.txt`、
`ocl_probe_v337.txt`、`kernel_selftest_v337.txt`、`bench_v337.txt`、`gui_selftest_v337_gpu_off.txt`、
`gui_selftest_v337_gpu_on.txt`、`gui_selftest_v337_gpu_on_stderr.txt`、`pkgverify_selftest_v337.txt`、
`corruption_audit_v337.txt`。

**关于用户原始场景（±104857 · 439 亿区块）的说明**：本轮未直接跑完 439 亿区块的整程搜索
（按实测单块 83~106 秒、约 44 个分块估算需 6~10 分钟，且罕见图案本就预期无结果）；
该场景的**几何与代码路径已被逐项覆盖**：分块几何（每块 4882 行 × 209715 列 = 1.02e9 区块）、
单块耗时、12 并发内存峰值、取消响应、剪枝判定与结果一致性均在**同宽度**（W=209715）条件下实测。
如需，我可以再跑一次完整 439 亿区块的端到端计时（约 10 分钟，无结果属预期）。

---

## 六、实施记录

* 按任务书 §九「先根因分析、确认后再改」执行：先只读排查 + 复现实测（A1/A2/A3/B 四组实验）→ 输出
  `docs/v337_rootcause_analysis.md`（含 §七 8 问答与三个待确认取舍）→ 用户确认「B / 1.5GB / 接受 N/M」后动手。
* 按「一次只修一个」推进：① 诊断能力（P1-6 前置）→ ② 取消路径（P0-2）→ ③ 提前停止（P0-1）→
  ④ 真实计数（P0-3）→ ⑤ 内存预算（P0-4）→ ⑥ UI 去重（P1-5），每步都用单元测试/自检/实测回归验证。
* 过程中发现并修掉两个**自检自身**暴露的真实缺陷：
  1. 并行扫描上报导致 `liveScanned` 重复计数（Emitter 用非原子 `m_lastDone`）→ 改为 CAS 单调累加；
  2. 搜索进行中重复点「开始搜索」时的**模态提示框在自检/自动化场景会永久阻塞 GUI 线程**（实测自检挂死 >600 秒）→ 自检模式改为状态栏提示。
* 未降级声明：P0-1 采用 B 方案而非任务书字面的「找到即停」，原因是字面方案会使结果不再是全局最近 N
  （任务书 §四 明令不做降级）；B 方案在用户场景下同样「找到就停」，且结果精度与 v3.3.6 完全一致
  （已用逐字节比对与 111/111 对照证明）。
* P2-7（GPU 慢）按任务书「无代码修改」处理；排查确认严格图案模式下引擎不使用 GPU，实际影响来自
  勾选时触发的后台实测基准占用 CPU（已记入根因分析，供后续版本参考）。
