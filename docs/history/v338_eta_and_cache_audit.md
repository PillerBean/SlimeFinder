# v3.3.8 前置：任务 1 根因 + 任务 2 缓存七问（代码级审查，未改代码）

审查对象：v3.3.7.2 源码树（`SlimeFinder/src`，与 `dist/SlimeFinder-3.3.7.2-win64.zip` 同源）。
规则：每条结论附**文件:行号**；凡本轮未逐行核对的，明确标注「未核对」，不推测。

---

## 任务 1：ETA 恒为 `--:--` —— 根因

**结论：算得出、也能刷新，但 GUI **从未把引擎给的值存进去**（只读不写）。**

| 证据 | 位置 | 说明 |
|---|---|---|
| `double m_etaSec = 0.0; // 最近一次进度回调给出的预计剩余秒数` | `src/gui/MainWindow.h:241` | 成员声明（初值 0） |
| `if (m_etaSec > 0.0 && m_etaSec < 36000.0) { … } else { progressEta->setText("预计剩余：--:--"); }` | `src/gui/MainWindow.cpp:2361-2365` | `updateElapsedUi()` 每 200 ms 读该成员渲染 |
| 全仓库对 `m_etaSec` 的引用只有 **2 处**（声明 + 上述读）——**没有任何赋值** | grep `m_etaSec` | ⇒ 恒为 0 ⇒ 恒走 else 分支 ⇒ 永远 `预计剩余：--:--` |
| 引擎侧 ETA **是算了的**：`const double eta = (cps > 1.0) ? (totalEvals - scanned) / cps : 0.0; progress(frac, scanning, cps, eta);` | `src/core/SlimeEngine.cpp:2179-2181`（分块块内）、`2243-2258`（块完成回调） | 回调第 4 个参数 `etaSec` 携带 ETA |
| GUI 进度 lambda **接收但丢弃**该参数：`[this, postToUi, runId, runMode, runAlg](double frac, bool scanning, double cps, double etaSec)`（体内只用 frac/scanning/cps） | `src/gui/MainWindow.cpp:3588-3610` | ⇒ 丢失点就在这里 |
| 完成/取消时被显式清空 | `src/gui/MainWindow.cpp:3804`（完成）、`3661` 附近（取消路径同款 `--:--`） | 完成后显示 `--:--` 属预期；问题只在**搜索中**也一直是 `--:--` |

**修复方案（最小改动，一行）**：在进度 lambda 内加 `m_etaSec = etaSec;`（并在 `startSearch()` 起始处 `m_etaSec = 0.0;`）。
`updateElapsedUi()` 已是 200 ms 定时器驱动，无需新增刷新逻辑；完成后按现状清空（任务书允许「清空或显示已完成」，建议搜索中显示数值、完成后清空）。
**验收方式**：439 亿密度搜索中打印/截图状态栏，断言 ETA 文本匹配 `预计剩余：\d+:\d\d` 且两次采样值不同（自检里可加一条断言：运行中读 `progressEta->text()` 不为 `--:--`）。

---

## 任务 2：缓存七问（逐条，附行号）

### Q1 缓存读取路径在哪？哪一行读 `.slimecache`？数据流向哪个函数？
* **唯一读盘实现**：`CacheManager::loadFile(const std::filesystem::path&, bool replace, std::string& err)`
  —— `src/core/CacheManager.cpp:115-161`：读 `magic`(4B) → `ver`(4B) → `rangeCount`(4B) → `entryCount`(8B)，
  然后 `rangeCount × 16B` 的矩形、`entryCount × 8B` 的坐标；`replace=true` 时整体替换内存内容，
  否则走 `mergeRaw()`（`CacheManager.cpp:88-107`）合并。
* **内存载体**：`slime::CacheManager` 实例；GUI 侧成员 `m_cache`（`src/gui/MainWindow.h`；使用点见 `MainWindow.cpp:3491-3492` 的
  `info.cacheWasUsed` / `m_cache->covers(info.region)`）。
* **传给引擎**：`SearchOptions::cache`（只读句柄）与 `cacheWrite`（增量写句柄）——`src/core/SlimeEngine.h:233-236`；
  分块路径的写句柄使用点在 `SlimeEngine.cpp:2075`（`collectCacheChunks`）与 `2376-2398`（批次归并）。
* **查询入口（引擎内）**：`opt.cache->covers(...)`（`SlimeEngine.cpp:883`）、`opt.cache->isSlime(x,z)`（`SlimeEngine.cpp:1176` CPU 扫描、`692` GPU 扫描）。
* **未核对**：GUI 启动/导入时调用 `m_cache->loadFile(...)` 的**具体行号**（本轮未逐行定位；`MainWindow.cpp` 中存在缓存导入对话框与缓存状态刷新调用点）。

### Q2 缓存命中时跳过了什么？从「缓存有数据」到「匹配阶段」的调用链
* 覆盖判定：`cacheCovers = useCache && opt.cache->covers(opt.region())`（`SlimeEngine.cpp:883`）。
* 位图填充阶段（经典路径，CPU）：`scanOneRow` 内
  ```
  if (useCache && opt.cache->lookup(cx, czz) == 1) { … grid.setSlime(...)  // 直接查缓存
  } else { … 计算 LCG/MT19937 … }                                        // SlimeEngine.cpp:1170-1180 区段
  ```
  即**命中时跳过的是「史莱姆判定计算」（LCG/MT19937 抽样）**，位图填充照做。
  对应 GPU 扫描路径同款：`SlimeEngine.cpp:692`（`grid.setSlime` 由 `opt.cache->isSlime` 决定）。
* 之后的匹配阶段（严格图案 / 密度窗口）读的是**同一张位图**（`grid.isSlime(...)`，如 `1793/1863/1880-1886`），
  因此**缓存确实参与匹配结果**——只是「省的是判定计算，不是扫描遍历」。
* **未核对**：`lookup()` 的三种返回（-1 未覆盖 / 0 非史莱姆 / 1 史莱姆）在各调用点的分支细节（本轮只确认了 `== 1` 直取、`== 0` 的处理在 `SlimeEngine.cpp:1170-1180` 区段内）。

### Q3 覆盖判定怎么做的？
* 矩形包含：`CacheManager::covers(const Rect&)` —— `src/core/CacheManager.cpp:34-47`：
  **线性遍历 `m_ranges`，任一个 range 完全包含查询矩形即返回 true**（不是位图、不是区间树）。
* 单点判定：`CacheManager::isSlime(x,z)` —— `CacheManager.cpp:52-58`：把坐标打包成 `u64` 后在**有序数组**上 `std::lower_bound` 二分；
  `lookup(x,z)`（`CacheManager.cpp:44` 声明 / 实现同区段）先判是否落在任一 range 内，再二分。
* 语义（文件头注释 `CacheManager.h:3-6`）：**缓存 = 一组「已完整扫描」的矩形 + 区域内全部史莱姆坐标**；
  落在某 range 内 ⇒ 命中缓存，是/不是史莱姆由集合二分决定。

### Q4 缓存 + 及时反馈的联动存在吗？（缓存内匹配到 N 个就提前停？）
**不存在。** 证据：
* 搜索范围与分块计划在扫描前一次性确定，覆盖**整个** `opt.region()`：`chunkPlanFor(H, wpr, hMax, cap)`（`SlimeEngine.cpp:455-478`，调用点 `2042`）；
* 分块循环按光栅顺序把所有块跑完（`SlimeEngine.cpp:2404-2421`：`nextChunk.fetch_add` + `for(;;)`，仅受 `cancel`/跨块剪枝影响）；
* 提前停止的条件只有两个：**匹配阶段堆满**（`stopAtTarget`）与**跨块剪枝**（`chunkCannotImprove`，`SlimeEngine.cpp:2210-2240`）——
  两者都基于「窗口距离下界」，**与缓存覆盖率无关**；
* 没有任何「先用缓存区做匹配 → 命中 N 个就停 → 否则再扫未覆盖区」的分支（这正是任务 4 要新建的流程）。

### Q5 密度匹配用缓存吗？还是只有图案匹配用？
**两种模式都用**，因为缓存只影响**位图填充**，而位图是两种模式共用的输入：
* 填充：`SlimeEngine.cpp:1176`（CPU）/ `692`（GPU）与模式无关；
* 密度窗口统计消费位图：`grid.isSlime(...)`（`1793/1863/1880-1886`）；
* 严格图案匹配消费位图：同一 `grid` + 掩码比较（同区段）。
* 结论：缓存对密度/严格**同等生效**（差别只在结果语义：密度按窗口密度排序，严格按图案逐位匹配）。

### Q6 缓存文件格式？前 256 字节？每条记录多少字节？存了什么？
格式定义见 `src/core/CacheManager.h:8-14`（写盘实现 `CacheManager.cpp:163-190`，读盘 `115-161`）：

| 偏移 | 长度 | 字段 |
|---|---|---|
| 0 | 4 B | 魔数 `0x534C4D45`（"SLME"） |
| 4 | 4 B | 版本号 = 1 |
| 8 | 4 B | range 数量 `rangeCount` |
| 12 | 4 B | （对齐/后续）——**注意**：写盘顺序为 magic(4) + ver(4) + rangeCount(4)，紧接 `entryCount`(8) 从偏移 12 起（`CacheManager.cpp:170-175`），因此**头部共 16 字节** |
| 16 | 16 B × rangeCount | 每个矩形 4×`int32`：`minX, maxX, minZ, maxZ` |
| 16+16R | 8 B × entryCount | 每条记录 = 打包坐标 `(X << 32) \| (Z & 0xFFFFFFFF)` |

* **每条记录 8 字节，只存坐标，不存其他字段**（无时间戳、无密度、无结果）。
* **前 256 字节** = 16 B 头 + 最多 15 个 range（15×16 = 240 B）⇒ 256 B 处正好落在第 16 个 range 的起始位置（若存在）。
* 体积公式：`sizeBytes() = 16 + ranges×16 + entries×8`（`CacheManager.cpp:195-198`）。
  用户量级 1.2 亿区块 ⇒ 1.2e8×8 = 960 MB + ranges ⇒ 与观测的 975 MB 吻合（≈8.1~8.5 B/区块，取决于 range 数）。
* 任务 5 的目标（≤200 MB / 1.2 亿）用**位图**（按范围分块位图）理论值约 15 MB（1.2e8 bit = 15 MB）✓ 与任务书一致。

### Q7 开缓存 vs 关缓存，扫描计数差多少？有日志证据吗？
**当前实现下：扫描计数没有差异。** 证据：
* 计数来源：扫描填充循环按行累加 `done += W`（`SlimeEngine.cpp:1236-1247` 区段），**与命不命中缓存无关**；
* 块内进度换算：`frac = liveScanned / totalEvals`（`SlimeEngine.cpp:2172-2181`），`totalEvals` 只由几何决定（`2047`）；
* 439.8 亿实跑日志（v3.3.7.2，开缓存，空缓存起跑）：
  `scanned=44211486052/44211486052 results=0`（`docs/perf_compare_v3372.txt`）——**全量计数**；
* 「缓存完全覆盖」的场景（`cacheCovers == true`）也不会减少计数：仍逐行遍历位图，只是把判定换成查表。
* **可观测的真实差异是时间**：命中时用 `lower_bound` 二分（`CacheManager.cpp:56`）替代 LCG/MT19937 计算——
  二分 ≈ log₂(1.2e8) ≈ 27 次随机访存/点，**在 1.2 亿条目规模上很可能比 LCG 更慢**（这一点需要实测确认，列为任务 3 的第一步实验）。

---

## 对任务 3~6 的初步判断（待你确认后再出正式设计文档）

| 任务 | 初步判断（基于以上证据） |
|---|---|
| 3 缓存读取路径 | **「读了但不跳步骤」**：缓存命中只省「判定计算」，不省「扫描遍历」，所以开/关缓存的扫描计数无差异（Q7）。若要「命中即跳过」，需要把「按行遍历位图」改为**按 range 整块拷贝/跳过**（例如命中 range 直接对位图做区段填充或直接从缓存增量生成窗口统计），否则计数天然不变。建议先做**基准实验**：预置一个覆盖全范围的缓存，测开/关缓存的实际耗时（验证二分是否比 LCG 快/慢），再决定改法。 |
| 4 理想缓存流程 | 需要在 `runChunked` 之外新增「缓存优先阶段」：先只跑「被缓存覆盖」的块（命中块可直接从缓存位图构造窗口统计），堆满 N 且启用 `stopAtTarget` 时**立即返回**；否则记录未覆盖块再续扫并合并。边界处理：**窗口跨边界**的块（含 `hMax-1` 行重叠）必须整块扩展补齐，不能只用缓存。数据结构建议：`cachedChunks / pendingChunks` 两个块列表 + 已有 `TopNHeap` 复用。 |
| 5 压缩 | 现状 8 B/条（Q6）。候选：①**按 range 分块位图**（15 MB 量级，读取 O(1) 位测试，最贴合「史莱姆判定」语义）；②坐标差分 + varint（对稀疏集合有效，读取需顺序解压）；③zstd 压缩现有 8 B 数组（实现最省事，读取需解压整块）。三者都需先做体积/速度实验。 |
| 6 分块几何 | 与 v3.3.7.2 结论一致：瓶颈是每块位图 20 MB × 12 并发（≫ L3）。方向 A：把每块位图压到 ≤2 MB（`rows/块` 或 `words/行` 双维度切分 + 窗口重叠处理）；方向 B：改内核访存（行块 tiling、按列分块）。需先出方案对比与预估收益。 |

**本轮未改动任何代码、未重新打包**；`dist/SlimeFinder-3.3.7.2-win64.zip` 仍是当前发布包。
等你确认后：任务 1 的一行修复可立即实施并验证（最快、独立），随后按任务 2 的结论推进任务 3 的基准实验。
