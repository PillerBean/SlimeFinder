# 旧版流程审计

审计对象：main 工作树 `D:\DeepSeek harness\史莱姆区块查找器\SlimeFinder`，HEAD `d199e4e`。
`version.json` 为 3.12.3；源码 CMake 显示 3.13.7 开发线。
本次为只读审计，未修改 main；所有行号均指该 HEAD。

---

## 0. 结论先行

旧版不是“一条搜索流程”，而是 **7 条并存流程**：

```text
1. runImpl 经典单次
2. runChunked 分块
3. runBanded 分带 + 懒加载缓存
4. run() 顶层的 GPU 图案流式快路径
5. MainWindow::runRingSearch 环形搜索（独立 std::thread + 嵌套 QEventLoop）
6. 缓存构建模式（GUI 分段 + runChunked + 每段落盘）
7. estimateSearch / crossCheckExact 估算与精确对照
```

每条都有自己的：

```text
进度分母
取消粒度
缓存读取方式
GPU 回退方式
totalFound 口径
结果合并/截断规则
```

所以旧版的问题不是“某个函数慢”，而是 **流程不变量没有被统一**。
只修单点会不断出现“这个路径修了、那个路径没修”的回归。

---

## 1. P0：正确性 / 崩溃 / 数据丢失

### P0-1 构建模式工作线程无锁遍历 `cache->ranges()`，与归并线程并发改写

位置：

```text
src/core/SlimeEngine.cpp:2532-2539
```

`scanOneRow` 里：

```cpp
for (const Rect& rc : opt.cache->ranges()) { ... }
```

没有持 `cacheReadMu`。同一函数另外两处读缓存都加了锁：

```text
2358-2359  fillFromCacheRow
2564-2565  cached segment isSlime
```

但归并线程会在：

```text
4159  mergeRaw
4176  pruneToFit
4239-4240  merger 持 cacheMu
```

改写 `m_ranges` / `m_bitmaps`，`ensureRange` 会 `push_back` 导致重分配。

影响：

```text
迭代器/引用失效 → 0xC0000005 / 堆损坏
或读到“range 已登记、位图未写”的中间态 → 假阴性
```

这正是 `4283-4286` 注释声称“已修掉”的同类问题，实际只修了 `covers()` 和 `isSlime()`，漏了最热的 `ranges()` 遍历。

### P0-2 构建保存：每次分段全量重写 pack，删旧 local，且启动不水合

位置：

```text
src/gui/mainwindow/MainWindowSearchResults.inc:868-872
src/cache/CacheStore.cpp:451-466
src/cache/CacheStore.cpp:400-423
src/gui/mainwindow/MainWindow.cpp:836
src/gui/mainwindow/MainWindow.cpp:1067
```

事实：

- 每段结束在 **GUI 线程** `saveV3Cache()`；
- `replaceLocalFromCacheManager` 先删除全部 local pack，再从整个 `m_cache` 重写；
- `writeFromCacheManager` 把 range 扩到 8192 tile 并逐点补边；
- 启动时 `m_cache = make_unique<CacheManager>()`，只尝试加载旧 monolithic 文件，**不从 CacheStore pack 水合**；
- `CacheStore::fillCacheManager` 存在但全仓无调用者。

影响：

```text
保存耗时随段数平方增长
大缓存构建周期性整窗卡死
重启后构建“续传”失效
上一会话的 local pack 被静默删除 = 缓存数据丢失
同一 tile 被多个相邻 range 重复写成重叠 pack
```

### P0-3 GPU 回读/清零返回码被忽略，失败仍返回 true

位置：

```text
src/gpu/GpuSession.cpp:674-677   图案 counter 回读
src/gpu/GpuSession.cpp:687-698   图案 hits 回读
src/gpu/GpuSession.cpp:1140-1149 密度 counter/hist 回读
src/gpu/GpuSession.cpp:1166/1191 密度 hits 回读
src/gpu/GpuSession.cpp:1294-1297 先填 out 再因超限返回 false
```

这些 `clEnqueueReadBuffer` 的 `cl_int` 都没有检查。  
任一失败时 `total` 保持 0 或部分值，函数仍 `return true`。

影响：

```text
静默漏结果 / totalFound=0
不会回退 CPU
截断被当完整结果
```

这是最危险的一类错误：结果看起来“成功”，但少了数据。

### P0-4 环形搜索：异常 terminate / 关窗不取消 / 嵌套事件循环

位置：

```text
src/gui/mainwindow/MainWindowRingMap.inc:210-214  worker [&] 捕获
src/gui/mainwindow/MainWindowRingMap.inc:215-261  try 只包住 plan
src/gui/mainwindow/MainWindowRingMap.inc:264-313  严格重算在 try 外
src/gui/mainwindow/MainWindowRingMap.inc:315-325  QEventLoop + join
src/gui/mainwindow/MainWindow.cpp:1248-1263       closeEvent 只处理 m_task
```

问题：

- `measureStrictForCandidates` / `stable_sort` / `erase` 抛异常 → 冲出线程 → `std::terminate`；
- `ringDone` 只在正常路径置位，异常后等待循环永远不退出；
- `closeEvent` 完全不知道环形搜索在跑，窗口关闭后进程仍被 worker 拖住；
- worker 捕获 `[&]` 栈变量，`MainWindow` 若析构即为 UAF；
- 没有 runId，环形结果会覆盖新搜索结果。

### P0-5 CacheStore 与搜索线程共享且无锁，运行中仍可导入/清空缓存

位置：

```text
src/gui/mainwindow/MainWindowSearchResults.inc:248-267  worker 调用 packs()/fill
src/cache/CacheStore.h:51                               packs() 返回 const 引用
src/cache/CacheStore.h:55-63                            addPack/removePack/clearAll
src/cache/CacheStore.cpp                               无互斥
src/gui/mainwindow/MainWindowInteraction.inc:486-508   终端 cache import/clear 无 running 判断
src/gui/mainwindow/MainWindowServices.inc:985-990      GUI 线程 merge + save
```

影响：

```text
worker 遍历 m_packs 时 GUI 改 vector → 重分配 → 引用失效
缓存索引/结果半写
随机崩溃/错误结果
```

### P0-6 “最优候选”与“密度 0 快路径”把取消当成功

位置：

```text
src/core/SlimeEngine.cpp:1789-1790  选择阶段取消只 break
src/core/SlimeEngine.cpp:1922       out.ok = true
src/core/SlimeEngine.cpp:1609       填充取消只 break
src/core/SlimeEngine.cpp:1629       out.ok = true
```

`runBestDensity` 取消后输出部分 TopN 并标记成功；  
`runZeroThresholdFast` 取消后用半张空位图算密度，结果 density=0，`totalFound` 仍按完整量上报。

与 `SearchTask.h`“取消不保存部分结果”的契约冲突。

### P0-7 GPU 行带匹配失败后整区重发，再失败才 CPU

位置：

```text
src/core/SlimeEngine.cpp:2125-2128  某带失败 bandOk=false
src/core/SlimeEngine.cpp:2192-2205  成功带结果被跳过
src/core/SlimeEngine.cpp:2216       再发一次全量 matchDensity
src/core/SlimeEngine.cpp:2275-2281  再失败才 CPU
```

最坏工作量：

```text
已完成的带 + 全量 GPU + CPU 全量 = 3 倍
```

并且进度从已爬到的带比例回落到 0，GUI 直接 `setValue` 倒退。

### P0-8 构建“达到容量上限即停止”没有实现

位置：

```text
src/core/SlimeEngine.cpp:4170 / 4458  只置 budgetStop
src/core/SlimeEngine.cpp:4306-4320    其他 chunk 不检查
src/core/SlimeEngine.cpp:4630-4670    worker 循环不检查
src/core/SlimeEngine.h:360-361        头文件写“立即停止”
```

GUI 依赖 `out.hitCacheBudget` 结束后续 band，但当前 band 会继续扫到自然结束。

---

## 2. P1：流程语义 / 取消 / 进度 / 回退

### 2.1 totalFound 有至少 7 种口径

位置：

```text
经典 CPU          3942  earlyStop ? results.size() : 命中总数
GPU 单派发        2249  gpuTotalFound 否则 gpuResults.size()
GPU 行带          2198  各带 partTotal 之和
分块              4707  各块之和，剪掉的块不计
最优候选          1921  = 评估锚点数
密度 0 快路径     1628  = 全部锚点数（未评估也算）
Banded            5089  = 截断后 results.size()，5045 累计值被丢弃
```

影响：

```text
“找到 N 个”的含义随路径跳变
进度行 / 结果行 / 精确对照互相不一致
Banded 的 totalFound 永远 ≤ target
```

### 2.2 stopAtTarget / smartRange 在 Banded 被静默禁用

位置：

```text
src/core/SlimeEngine.cpp:5015-5016
```

基岩版 + 缓存是普通搜索默认路径，用户勾“达到即停”后：

```text
仍然扫完整区，最后才截断
“由近及远”只剩最终排序
没有任何 UI 提示
```

### 2.3 cacheFirst 的 stopAtTarget 明确不保证全局最近 N

位置：

```text
src/core/SlimeEngine.cpp:4637-4641  注释：不保证全局最近 N
src/core/SlimeEngine.cpp:4729       仍置 stoppedEarly
src/core/SlimeEngine.h:320          头文件却写“数学保证全局最近 N”
```

同一个开关，行为取决于有没有缓存覆盖块。

### 2.4 GPU 扫描失败后 CPU 从头重扫

位置：

```text
src/core/SlimeEngine.cpp:1107-1110 / 1146-1149  失败返回 0
src/core/SlimeEngine.cpp:2393-2405                CPU 全量
```

已经 `integrateTile` 的瓦片仍在位图里，但 CPU 不续扫。  
最后一块才失败时，GPU 时间几乎全白费。

### 2.5 GPU 图案一个变体失败，整区 CPU 重扫

位置：

```text
src/core/SlimeEngine.cpp:1427-1461
src/core/SlimeEngine.cpp:1467
```

3/4 变体完成后失败，已完成的 GPU 结果全部丢弃；  
`out = SearchOutcome()` 在 1940 又把回退原因清空。

### 2.6 带内 liveTotal 被覆盖，N/M 可 >100%

位置：

```text
src/core/SlimeEngine.cpp:5005-5016  bandOpt 未清 liveScanned/liveTotal
src/core/SlimeEngine.cpp:2323-2325  每带 store 本带总量
src/core/SlimeEngine.cpp:3990       分块 store totalEvals
src/gui/mainwindow/MainWindowInteraction.inc:773-777
```

第一带后状态栏可出现“扫描 2,000,000 / 1,050,000（190%）”。

### 2.7 分带路径丢失 GPU/剪枝/回退统计

位置：

```text
src/core/SlimeEngine.cpp:5038-5049
src/core/SlimeEngine.cpp:5086-5103
```

只聚合 scanned/windows/scanMs/matchMs/totalMs；  
`gpuScanUsed`、`gpuMatchUsed`、`gpuFallbackReason`、`windowsSkipped`、`hitCacheBudget` 等未聚合。  
GUI 的 GPU 使用面板在真走了 GPU 时仍可能显示“×”。

### 2.8 分带任一带失败 → 整次失败，已完成带全丢

位置：

```text
src/core/SlimeEngine.cpp:5033-5036
src/core/SlimeEngine.cpp:5070-5073
```

单带缓存 I/O 抖动就会让整次搜索失败，没有“关缓存重试该带”的降级。

### 2.9 分块进度三套口径，可能倒退

位置：

```text
src/core/SlimeEngine.cpp:4528-4544  完成块：liveScanned/totalEvals
src/core/SlimeEngine.cpp:4647-4651  缓存够数：doneSkip/nChunks
src/core/SlimeEngine.cpp:4657-4666  剪枝：又回 liveScanned/totalEvals
```

切换分支时分形值可从 0.5 掉到 0.21。

### 2.10 进度回调实际是多线程并发调用

位置：

```text
src/core/SlimeEngine.cpp:2499 / 2503  sampleTask + OpenMP
src/core/SlimeEngine.cpp:2695 / 2701  scanTask + OpenMP
src/core/SlimeEngine.cpp:4356-4377    chunk subProgress
src/core/SlimeEngine.cpp:498-505      Emitter 直接在调用线程执行 cb
```

`SearchTask.h:4-6` 只写“工作线程内调用”，没有说明可并发、乱序。  
GUI 用 queued invoke 暂时安全，但同一帧内低分位事件可能后到覆盖高分位。

### 2.11 取消检查有 4 类不可取消段

位置：

```text
src/core/SlimeEngine.cpp:4999  cacheBandLoader（读盘可数十秒）
src/core/SlimeEngine.cpp:4159/4176 mergeRaw/pruneToFit（单次 1.1~1.8s）
src/core/SlimeEngine.cpp:4209-4211 enqueueMerge 背压只 wait_for(20ms)，不看 cancel
src/core/SlimeEngine.cpp:4685-4689 取消后仍 flushCacheBatch 再检查
```

GUI 在 `MainWindow.cpp:1137-1138`、`MainWindowSearchResults.inc:858` 直接 `m_task.wait()`，所以取消/关窗可冻结 UI 到这些段结束。

### 2.12 分块/最优候选/GPU 的 scanMs+matchMs 可≈2×totalMs

位置：

```text
src/core/SlimeEngine.cpp:2194-2201  GPU 行带：scan=match=total
src/core/SlimeEngine.cpp:2229-2265  GPU 单派发：scan=match=total
src/core/SlimeEngine.cpp:4714-4715  分块：scan=total，match=0
src/core/SlimeEngine.cpp:1926-1927  最优候选：scan=match=total
```

性能归因会被引到错误阶段；`search.summary` 也失真。

### 2.13 GPU 行带匹配丢 rank，并列裁决与 CPU 不同

位置：

```text
src/core/SlimeEngine.cpp:2153-2157  Scored s 未填 rank，默认 0
src/core/SlimeEngine.cpp:2244       单派发填 rank
src/core/SlimeEngine.cpp:3076-3078  CPU 填 rank
src/core/SlimeEngine.cpp:2130-2145  行带按 (d2,minZ,minX) 排序
```

等距命中在不同路径给出不同“最近 N”。

### 2.14 分块/分带每块重复初始化与重复日志

位置：

```text
src/core/SlimeEngine.cpp:3955-4013  每分块 validate/plan/grid
src/core/SlimeEngine.cpp:2410-2418  每分块重分配 xTailTable
src/core/SlimeEngine.cpp:1289-1309  每带新建 CpuSampler
src/core/SlimeEngine.cpp:1467-1490  每带一条 search.summary
src/core/SlimeEngine.cpp:5027       每带 run()
```

Banded 169 带就是几百条 summary 和固定毫秒开销。

### 2.15 缓存覆盖判定 O(chunks×R) / O(H×R)，且只认单 range 完整覆盖

位置：

```text
src/core/CacheManager.cpp:97-104
src/core/SlimeEngine.cpp:4300
src/core/SlimeEngine.cpp:4562-4584
src/core/SlimeEngine.cpp:4962-4965
```

多个相邻 range 拼成完整覆盖时仍判“未覆盖”，导致整块重算/重登记。  
Banded 的 `cacheBandMayCover` 只看“有交集”，不是“已覆盖”。

### 2.16 `CacheManager::findRange` 的 `thread_local lastIdx` 跨实例复用

位置：

```text
src/core/CacheManager.cpp:105-118
src/core/CacheManager.cpp:190
src/core/CacheManager.cpp:519
```

`lastIdx` 只校验“某个 range 含该点”，没有对象标识；  
Banded 每带新建 CacheManager 且 range 可重叠时，可能取到错误 range。

### 2.17 `addPackedWords` 先建 range 再校验字数，失败留下空壳

位置：

```text
src/core/CacheManager.cpp:202-205
```

失败后 range 已登记，位图全 0；  
后续 `covers()=true` 但全判非史莱姆。

### 2.18 缓存文件头校验不足，可触发超大分配

位置：

```text
src/core/CacheManager.cpp:374  outlierCount 允许到 2^40
src/core/CacheManager.cpp:446  reserve(min(count, 8TB))
src/core/CacheManager.cpp:385-387 单 range 允许 2^33 位
src/core/CacheManager.cpp:154  bits.assign
```

损坏/恶意文件或内存紧张时 `bad_alloc` 穿过 `noexcept` 引擎 → terminate。

### 2.19 构建模式缓存锁粒度过粗

位置：

```text
src/core/SlimeEngine.cpp:4159-4186  mergeRaw/prune 全程持锁
src/core/SlimeEngine.cpp:4239-4240  merger 持锁
src/core/SlimeEngine.cpp:2357-2378  每行加锁拷贝
src/core/SlimeEngine.cpp:2564-2570  每个 cached 段持锁逐点 isSlime
```

归并持锁可达 0.1~1s，worker 在锁上排队；  
读侧每行/每段加锁，长段锁内循环可达整行。

### 2.20 Banded 缓存准备重复解压、空带也加载、失败静默

位置：

```text
src/core/SlimeEngine.cpp:4962-5003
src/cache/CacheStore.cpp:560-663
src/gui/mainwindow/MainWindowSearchResults.inc:249-266
```

同一个 8192 pack 会被相邻 band+halo 重复解压最多 4 次；  
无交集的 band 也走 loader；加载失败返回空 cache 继续算，没有可见原因。

### 2.21 GPU 命中上限溢出 = 整段 CPU 重算

位置：

```text
src/gpu/GpuSession.cpp:715-718
src/gpu/GpuSession.cpp:1294-1297
src/core/SlimeEngine.cpp:1355
src/core/SlimeEngine.cpp:2063-2065
```

超 4M/8M 命中时 GPU 已扫完、命中已写出，却整体丢弃回 CPU。

### 2.22 GPU 适配器静默拒绝且不写 err

位置：

```text
src/gpu/GpuScanAdapter.cpp:51-55   图案 >32 行/列直接 false
src/gpu/GpuScanAdapter.cpp:84-88   gridSize>15 或超 int 直接 false
src/gpu/GpuScanAdapter.cpp:53-54/86-87  int64 锚点算成 int
```

引擎只看到 false，用户只看到“GPU 勾了却没用”，没有回退原因。

### 2.23 GPU 每次调用新建大缓冲，不看显存

位置：

```text
src/gpu/GpuSession.cpp:906-917 / 1052-1058
src/gpu/GpuSession.cpp:581-620
```

4M 命中约 40MB，8M 约 72MB/次；低显存设备容易失败回退。

### 2.24 `runBestDensity` 完全绕过缓存

位置：

```text
src/core/SlimeEngine.cpp:1988-1994
src/core/SlimeEngine.cpp:1654-1660
src/core/SlimeEngine.cpp:5027
```

Banded + ignoreThreshold 必走 `runBestDensity`，band loader 加载的缓存位图一次也不用。

### 2.25 每次 GPU 调用都重新派发，无法复用已缓存位图

位置：

```text
src/core/SlimeEngine.cpp:2393-2405
src/core/SlimeEngine.cpp:2045-2049
```

缓存 + GPU 密度/图案条件互斥，Banded 内层 `useCache=true` 直接禁用 GPU。

---

## 3. GUI / 状态机问题

### G-1 `wait()` / `cancel+wait` 直接在 GUI 线程

位置：

```text
src/core/SearchTask.cpp:12-17 / 64-67
src/gui/mainwindow/MainWindowSearchResults.inc:858/1091/1107/1298
src/gui/mainwindow/MainWindow.cpp:1250-1252
```

正常路径多为空操作，但取消延迟、worker 销毁大位图/GPU 资源、closeEvent 时会冻结窗口。

### G-2 构建每段在 GUI 线程同步落盘

位置：

```text
src/gui/mainwindow/MainWindowSearchResults.inc:858-873
src/gui/mainwindow/MainWindowServices.inc:781-795
```

每段 20~60 秒，缓存大时单次保存数秒，期间窗口“未响应”。

### G-3 进度硬编码“扫描 75%”，匹配阶段速度/ETA=0

位置：

```text
src/core/SlimeEngine.cpp:185-188
src/core/SlimeEngine.cpp:475-519
src/core/SlimeEngine.cpp:2796 / 2894
src/gui/mainwindow/MainWindowSearchResults.inc:453
```

匹配慢时卡 75%，完成后跳 100%；匹配阶段状态栏 ETA 永远“--:--”。

### G-4 构建进度分母取实时 UI 范围

位置：

```text
src/gui/mainwindow/MainWindowSearchResults.inc:519-525 / 806-817
src/gui/mainwindow/MainWindowServices.inc:205-226
```

运行中改半径 → 百分比跳变/倒退；实际任务仍用旧 `m_buildBands`。

### G-5 取消/错误收尾不完整

位置：

```text
src/gui/mainwindow/MainWindowSearchResults.inc:1072-1108
```

不停 200ms 定时器、不复位阶段标签/进度条/busy 区间；  
取消后工具栏仍可能显示“搜索中/匹配中”。

### G-6 非精确算法结束后在 GUI 线程同步跑一次精确对照

位置：

```text
src/gui/mainwindow/MainWindowServices.inc:513-535
src/gui/mainwindow/MainWindowSearchResults.inc:1238-1243 / 1730
```

搜索“完成”后界面冻结数百毫秒到数秒；  
对照用的 `omp` 复制了 `liveScanned/liveTotal/liveMatches` 指针，会污染计数。

### G-7 反馈面板过滤 Success，错误也不进反馈

位置：

```text
src/gui/panels/FeedbackPanel.cpp:47-61
src/gui/mainwindow/MainWindowInteraction.inc:152-166
src/gui/mainwindow/MainWindowSearchResults.inc:1094-1108 / 1243 / 1253
```

成功搜索永远不进反馈；错误只有模态框，没有反馈条目；  
`severeAdded` 没有连接。

### G-8 运行中仍可改关键参数，或修改设置触发探测/重启

位置：

```text
src/gui/mainwindow/MainWindowServices.inc:205-226
src/gui/mainwindow/MainWindowBuildUi.inc:1958-2002
src/gui/mainwindow/MainWindowSettingsUi.inc:107-115
src/gui/mainwindow/MainWindowConfig.inc:288-319
```

运行中改半径/图案/设置页：任务用旧快照，UI 显示新值；  
GPU 首次探测同步初始化 OpenCL；autoRestart 可重启整个程序。

### G-9 环形搜索完全绕过统一运行态

位置：

```text
src/gui/mainwindow/MainWindowRingMap.inc:188 / 210 / 315-326
src/gui/mainwindow/MainWindow.cpp:1248-1263
```

`m_running` 不置位、无 runId、无 m_task、closeEvent 不取消；  
关闭窗口后进程被 worker 拖住。

### G-10 缓存上限/构建预算不落盘

位置：

```text
src/gui/mainwindow/MainWindowConfig.inc:115-118
src/gui/mainwindow/MainWindowSearchResults.inc:673-699 / 741
```

`writeConfigFile` 回读旧 `cache.maxSizeMB`，不写 `m_settings.cacheMaxMB`；  
“提高上限”按钮实际不生效。

---

## 4. 结构性根因

### R-1 没有统一 Plan / Outcome

流程由 `if/else + 环境变量 + GUI 状态` 驱动；  
`SearchOutcome` 字段被不同路径用不同含义填。  
新增一个路径就会复制一套进度/取消/统计逻辑。

### R-2 没有不可变快照

缓存、配置、参数、GPU 适配器都在运行中被 GUI 线程修改。  
旧版用 `cacheReadMu` 局部补锁，但漏点很多，且锁粒度造成吞吐塌陷。

### R-3 没有统一回退协议

GPU/缓存/算法失败后，各路径分别选择：

```text
重扫整区
丢弃部分结果
静默成功
重置 out 清掉原因
```

没有“已完成的哪些工作可复用”的状态。

### R-4 进度没有契约

各路径自定 0/0.5/0.75/1.0；  
分母有区域区块数、计划评估量、当前带总量、UI 当前范围四种；  
回调可多线程并发，顺序不保证。

### R-5 取消没有终态

取消只置 atomic；  
不可取消段、GUI join、取消当成功、进度无终态，四个问题叠在一起。

### R-6 GUI 拥有流程编排

分段构建、环形搜索、精确对照、缓存探测都在 MainWindow 里；  
引擎只负责一次 `run`，无法保证跨段一致性。

### R-7 缓存有两套表示

`m_cache`（monolithic CacheManager）和 `m_cacheStore`（pack 索引）。  
普通搜索用 pack 懒加载，构建用 m_cache，保存时整体重写，启动不水合。

---

## 5. 流程优化必须先定义的不变量

在动代码前，建议先把下面 8 条写成验收条件：

```text
I-1 单任务状态机：Idle / Starting / Running / Stopping / Finishing
I-2 单结果入口：applyOutcome(outcome, info, runId)
I-3 运行期输入不可变：参数/缓存/配置用快照
I-4 进度单调：0..1，单一工作量分母，阶段只做文字
I-5 取消终态：cancelled 必须 ok=false，且不得产出部分结果
I-6 回退不重扫：已完成工作可复用，失败原因可见
I-7 totalFound 单语义：命中总数 ≠ 显示条数
I-8 缓存写入原子：不可变快照 + 临时目录 + rename
```

---

## 6. 建议的修复顺序

### 第 0 批：先止血（不改变架构）

```text
1. 构建缓存读竞态：ranges 快照 / 禁止运行中改缓存
2. 构建保存：停止每段全量重写；至少改成增量追加 + 后台线程
3. GPU 回读返回码：失败必须 false，禁止静默成功
4. 取消当成功：best/zero 路径统一 out.cancelled
5. 环形搜索：closeEvent 取消 + worker 全量 try/catch
6. CacheStore 运行期只读：导入/清空 gate 到任务结束
```

### 第 1 批：统一流程模型

```text
7. SearchJob 状态机（普通/环形/构建共用）
8. 统一 Progress 契约 + 单调包装
9. 统一 SearchOutcome：hitsTotal / results / fallback / phases
10. 统一取消：两阶段 cancel + 有界等待 + 取消终态
```

### 第 2 批：缓存与 GPU

```text
11. CacheSnapshot（不可变行/范围索引）
12. Band 级缓存 LRU，避免重复解压
13. GPU cap 溢出：截断标记 + 部分结果，不整段重扫
14. GPU 扫描失败：从已完成 tile 续扫
15. Banded 全局早停（恢复 stopAtTarget/smartRange）
```

### 第 3 批：UI 呈现

```text
16. 单一 finishRunUi(success/cancel/error)
17. 反馈面板错误/成功都可见
18. 构建进度固定快照分母 + ETA
19. 设置页运行期只读
20. 环形纳入统一结果入口
```

---

## 7. 审计边界

本次是静态审计，没有运行复现所有 P1；  
P0 均给出可定位代码路径，建议先写最小回归：

```text
构建模式多线程 + 缓存写：TSAN/ASAN 或高频压测
GPU 回读失败注入：模拟 clEnqueueReadBuffer 返回错误
环形搜索异常/关窗：注入 bad_alloc、关闭窗口
取消 best/zero：取消后断言 out.cancelled
Banded：断言 liveTotal 不被带覆盖、totalFound 语义
```

源码位置均以 main `d199e4e` 为准；新分支修复时不要直接照搬行号。
