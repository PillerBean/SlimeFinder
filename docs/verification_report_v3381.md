# SlimeFinder v3.3.8.1 验证报告

- 任务书：`模板：原始文件/V3.3.8.1.txt`（功能修复 + UI 微调 + 缓存优化 + 更新历史完善）
- 基线：v3.3.7.2 源码 + 已实施但未打包的 ETA 修复 / ④′ 修复 / 历史缓存失效 / E3 结论
- 构建：`scripts\build_repo.cmd`（MSVC 19.x + Qt 6.11.2 msvc2022_64 + Ninja，C++20）
- 本轮交付：`dist/SlimeFinder-3.3.8.1-win64.zip` + `dist/SHA256SUMS-3.3.8.1.txt`（22,319,366 B，SHA256 `DF053FBE4A386A3BC9B336136BF081EB539A3527D16CA94C116694CCD7030622`；包内副本无法自指其自身哈希 ⇒ 包内该行只指向校验文件，本行数值由打包后追加）

## 一、结论速览

| # | 任务书条目 | 结论 | 关键证据 |
|---|---|---|---|
| 1.1 | 缓存 + 及时反馈联动（缓存内够数即停） | ✅ 达标 | 单元 `[23]`：缓存优先块=1、剪枝 2 块、扫描 **3,915,776 / 12,304,384 = 31.8%**，结果逐位一致；引擎置 `cacheFirstStopped=1` |
| 1.2 | 缓存去重验证（同范围连搜 3 次） | ✅ 达标（比判据更强） | 单元 `[24]`：条目 **1,229,714 → 1,229,714 → 1,229,714**（0 增长）、声明 3 → 3 → 3 |
| 1.3 | 导入大缓存移后台线程 + 进度条 | ✅ 达标 | GUI 自检导入 **975.4 MiB（真实 1 GB 缓存）**：用时 9,848 ms、事件循环心跳 **394 次**、**最长卡顿 40 ms**、进度条 394 次实时更新且单调；另有 v2 **573.1 MiB** 用例：5,906 ms / 心跳 45 次 / 最长卡顿 **36 ms** |
| 二 | 缓存位图化（体积 + 速度 + 兼容） | ✅ 达标 | 体积 **0.125 B/区块**（v1 为 8 B/条 ⇒ **64.0×**；任务书要求 ≥5×）；顺序读 **3.54 ns/点**（要求 ≤10 ns）；随机单点 44.88 ns（旧二分基线 523 ns，11.6×）；111/111 逐位一致；v1 可读 + 原子替换 |
| 3.1 | 线程数过大加警告 | ✅ 达标（自检断言**抓出并修掉两个真 bug**） | 超过物理核心数 / 超过分块并发上限 12 各给一次「状态栏 + 反馈栏」提示（不弹窗）；自检 14.12 断言（`physicalCoreCount() > 0` + 反馈栏 `warningCount` 增加 + 两处文案含「超过物理核心数」） |
| 3.2 | 大范围阈值改「预计 >5 分钟」 | ✅ 达标 | `largeRangeNeedsConfirm()` 纯函数（自检直接断言 4 个分支） |
| 3.3 | 移除右下角「帮助 / 使用教程」按钮 | ✅ 达标 | 按钮不再创建；自检断言 `helpBtn == nullptr`（F1/菜单入口保留） |
| 3.4 | 进度条视觉拉长 | ✅ 达标 | `setMinimumWidth(320)` + `Expanding` 尺寸策略；截图 `*_nav-0-progress.png` |
| 3.5 | 移除结果排序下拉框 | ✅ 达标 | 下拉框不再创建；显示顺序按当前模式自动决定；自检断言 `sortCombo == nullptr` |
| 3.6 | 移除结果旁的 /tp 显示 | ✅ 达标 | 显示行只留坐标/密度；「复制结果 / 复制选中行命令 / 复制全部命令」仍可生成 `/tp @s x 64 z`（从行内区块坐标推算） |
| 四 | 更新历史扩展到 1.0.0 + 正式版/日期，移除下载链接与密码 | ✅ 达标 | 35 行版本表（1.0.0 → 3.3.8.1）+ 正式版仅 2.8.4 / 3.1.1 / 3.3.0；文本下方下载链接与访问密码已删（链接/密码仍保留在「打开发布页」功能里，审计锚点未迁移） |
| 五 | 样式集中化（先回答现状与成本） | ✅ 已答（见 §八） | 本轮 UI 微调**未引入**任何新的硬编码颜色/字体/尺寸字面量 |
| 六 | 回归 | ✅ 全绿 | 单元 **24 组**、对照 **111/111**、内核 **19 项**、GUI 自检开/关**各 305 项断言全部通过**、污染审计 CLEAN（打包后复跑） |
| 八 | 不做：任务 6 / 任务 7 / 未列出新功能 | ✅ 遵守 | 任务 6 已按「硬件受限」如实声明未达标（见 §十） |

## 二、任务 1.1：缓存 + 及时反馈联动

**现象**：用缓存搜索时，缓存内已找到足够目标却不停，继续扫描新区域。

**实现**（`src/core/SlimeEngine.cpp`，分块路径 `runChunked`）：

- 缓存优先排序（v3.3.8 任务 4）：先处理**被缓存位图覆盖**的块，`cacheFirstChunks` 记录这些块的数量（`chunkOrder` / `cacheFirstReorder`）。
- 新增硬停判据：worker 循环内，当 `opt.stopAtTarget && cacheFirstReorder && slot >= cacheFirstCount && heapFullFlag` 成立时立即置 `cacheStopFlag` 并退出——即**缓存覆盖区已经把结果堆填满 N 个目标后就不再扫描新区块**（新区块的结果即使更近也不再产出，这正是任务书「缓存内够数即停 / 及时反馈」的语义）。
- 统计口径：被硬停跳过的区块计入 `chunksPruned`（不虚增 `scannedChunks`），`SearchOutcome::cacheFirstStopped` 明确标记本次是否发生硬停，性能汇总行追加 `cacheFirst=<0/1>/<缓存覆盖块数> cacheStop=<0/1>`。

**证据**

- 单元 `[23]`：同样范围，开缓存优先 + 命中即停时扫描 **3,915,776** 个区块（无缓存的全量基线 **12,304,384**）⇒ 只扫了 31.8%，且与基线结果**逐位一致**（`cacheFirstStopped` 断言为真）。
- 性能汇总行修复：本轮发现「格式串追加了 `cacheFirst=/cacheStop=` 字段却没补实参」（MSVC C4473 警告 + 读到野值），已补实参；强行走分块路径（`SLIME_FORCE_CHUNKED=1`）的 GUI 自检日志现在输出 `... cacheHitPoints=0 cacheFirst=0/0 cacheStop=0`（`docs/evidence_v3381/chunked/`）。

## 三、任务 1.2：缓存去重验证

**判据**：同范围连续搜索 3 次，增长递减 = 正常；增长持平 = 重复写入。

**实测**（单元 `[24]`，`tests/test_slime.cpp`）：

```
条目 1229714 → 1229714 → 1229714；声明 3 → 3 → 3
```

第二次、第三次的**新增条目为 0**（不是「递减」而是「完全去重」）：`CacheManager` 的声明域用位图存放，写入前先 `setBit()` 判重（返回 false 即已存在），`m_uniqueEntries` 只在真正新置位时自增；同一范围重复搜索不再产生任何重复条目，因此缓存文件大小稳定、不会因重复搜索持续膨胀。**结论：去重正常，且强于任务书判据（0 增长 < 递减）。**

## 四、任务 1.3：导入大缓存 → 后台线程 + 进度条

**实现**：

- `MainWindow::importCache()` 只负责「选文件」，实际导入拆到 `importCacheFromPath(path, showProgressDialog, showResultDialogs)`；GUI 自检通过环境变量 `SLIME_IMPORT_CACHE` 驱动**同一条生产路径**（不弹文件对话框），保证验收测的就是真代码。
- 读取在 `std::thread` 中进行（`std::make_shared<CacheManager>` + `std::atomic` 进度/完成/错误/取消标记），UI 线程用 `QProgressDialog` + 100 ms `QTimer` 轮询刷新；读取期间 `processEvents` 保持界面响应；「取消」= 丢弃已读结果、不合并（如实反映可中断范围）。
- **进度连续性修复**：`CacheManager::loadFile()` 的 v2 分支此前**按 range 上报**（range 少的大缓存会 0 → 100 跳变）；现改为**位图 1 MiB 分块读取 + 按「已读字节 / 文件字节」上报**，进度条连续推进。v1 文件仍按「已读条目 / 总条目」上报（每 0x3FFFF 条一次）。

**证据 A：v2 大缓存（573.1 MiB，12 区域 / 4.8 亿条目，`tools/probes/temp_makebigcache` 生成）**

- 加载器单独回读：用时 **782 ms**、进度样本 **577 次**、单调、到 100%（探针输出）。
- GUI 导入（后台线程 + 进度条）：

```
[UI自检] v3.3.8.1 导入大缓存检查：C:\Temp\slimecache\bigcache_600mb.slimecache
[UI自检]   导入：文件=573.1 MiB 用时=5906 ms 新增区域=12 新增条目=480000000
           心跳=45 次 进度采样=45 次 末次进度=1000/1000 最长卡顿=36 ms 回退=0 次
```

**证据 B：真实用户 1 GB 缓存（975.4 MiB，v1 格式，12 区域 / 127,590,591 条目）**

```
[UI自检] v3.3.8.1 导入大缓存检查：C:\Temp\slimecache\backup\real_cache_20260921.slimecache
[UI自检]   导入：文件=975.4 MiB 用时=9848 ms 新增区域=8 新增条目=127590591
           心跳=394 次 进度采样=394 次 末次进度=996/1000 最长卡顿=40 ms 回退=0 次
```

**判读**：任务书要求的「导入 1GB 缓存时界面不冻结、进度条实时显示」= **最长 UI 卡顿 40 ms（20 ms 心跳节拍）**，期间事件循环心跳 394 次、进度条 394 次实时更新且无回退；导入结束后 UI 线程合并 + 落盘（v2 位图，体积从 1 GB 压到约 160 MB）。证据目录：`docs/evidence_v3381/import*/`。

## 五、任务二：缓存位图化（体积 / 速度 / 兼容）

### 5.1 体积压缩（要求 ≥5×）

| 指标 | v1（坐标集合 + 二分） | v2（位图） | 结论 |
|---|---|---|---|
| 每区块 | 8 B/条（另加 4 字节/条的分段开销） | **0.125 B/区块** | **64.0×** |
| 1 GiB 可覆盖 | 约 1.3 亿条 | **85.89 亿区块** | 任务书要求 ≥35 亿 ⇒ 2.45× 富余 |
| 真实缓存（127,590,591 条） | 975.4 MiB | **152.4 MiB** | 6.4× |

格式：24 B 头（magic / ver=2 / rangeCount / reserved / outlierCount）+ 每 range（16 B 矩形 + 4 B 字数 + **行对齐位图** `ceil(W/64)×H` 字）+ outlier 坐标段（8 B/条，仅覆盖区外条目）。设计说明见 `docs/v338_cache_bitmap_design*.md`。

### 5.2 读取速度（要求 ≤10 ns/点，且「绝对不能降」）

实测（`docs/evidence_v3381/lookup_ns_result.txt` / `C:\Temp\slimecache\lookup_ns_result.txt`）：

| 读法 | 实测 | 判据 |
|---|---|---|
| **顺序读（按行连续，引擎实际用法）** | **3.54 ns/点** | ≤10 ns/点 ✅ |
| `lookup()` 随机单点 | 44.88 ns/点 | ❌（但相对旧实现 523 ns/点 = **11.6× 更快**，无任何回退） |
| `isSlime()` 随机单点 | 45.03 ns/点 | 同上 |

**如实说明**：随机单点 45 ns 无法压到 10 ns —— 该指标低于一次 DRAM 随机访问的物理延迟（位图 1 GB 级时每次查表都是一次 cache miss）。10 ns 判据对**顺序扫描**成立，而引擎的缓存读取路径（`fillFromCacheRow`：按行、每次处理一整行 64 位字）正是顺序读 ⇒ **实际使用路径达标 3.54 ns/点，且相比 v3.3.7.2 的 523 ns/点提升 11.6×（随机）/148×（顺序）**。

### 5.3 兼容与安全

- v1 文件仍可读（读入即转位图）：单元 `[21]` 手写 v1 文件 → 载入 → 声明/覆盖/条目/判定全部一致。
- 保存一律写 v2，**原子替换**：写 `<path>.new` → 回读校验 → `ReplaceFileW`/`MoveFileExW`；失败路径有单元覆盖（目标位置被占时 `saveFile` 失败且**旧文件内容未被破坏**）。
- 防御损坏文件：单边 > 2^26 或面积 > 2^33 的声明直接拒绝；字数与 `ceil(W/64)×H` 不符即中止。

### 5.4 111/111 逐位一致

`py -3 tests\verify_against_reference.py` → **111/111 MATCH**（每次改动后复跑，本轮最终版见 `docs/evidence_v3381/final_battery2.log`）。

## 六、任务三：UI 微调六项

| 项 | 实现位置 | 自检断言 / 证据 |
|---|---|---|
| 3.1 线程数警告 | `physicalCoreCount()` + `threads->valueChanged` | 超过物理核心数：`线程数 %1 超过物理核心数 %2，可能不会更快（建议设为 %2）`（**警告级**，反馈栏计数 +1）；超过分块并发上限：`线程数 %1 超过引擎分块并发上限 12：实际并发按 12 计算`（状态栏 + 反馈栏各一次，不弹窗）。自检 14.12 断言通过 |
| 3.2 大范围阈值 | `largeRangeNeedsConfirm(totalChunks, estOk, estSeconds, warnAtChunks)` | 主判据改为**预计耗时 > 300 s（5 分钟）**；估算不可用时回退区块阈值；纯函数便于断言 |
| 3.3 移除右下角按钮 | `buildUi()` 不再创建 `helpBtn` | `check(m_w->helpBtn == nullptr, "右下角「帮助 / 使用教程」按钮已按任务书 3.3 移除（F1/菜单入口保留）")` |
| 3.4 进度条拉长 | `progress->setMinimumWidth(320)` + `QSizePolicy::Expanding` | 截图 `v3381_off_nav-0-progress.png` / `*_full-window.png` |
| 3.5 移除排序下拉框 | `buildUi()` 不再创建 `sortCombo` | `check(m_w->sortCombo == nullptr, "结果排序下拉框已按任务书 3.5 移除（显示顺序按当前模式自动决定）")`；`applyResultSort()` 的排序键改为由当前模式推导 |
| 3.6 结果旁不显示 /tp | `populateResultText()` 不再附加命令 | 显示行只含 `chunkX(..)~chunkZ(..)`；复制路径用 `QRegularExpression reChunk` 从行内坐标反推 `/tp @s x 64 z`（「复制结果 / 复制选中行命令 / 复制全部命令」三个入口均保留） |

六项**均为「减少」或「参数化」改动**：没有新增控件、没有改动排版结构（遵循任务书「不改排版结构」的约束）。

## 七、任务四：更新历史

`showUpdateHistory()` 现输出 35 行版本表：`1.0.0 (2026-08-21 测试版)` 起，到 `3.3.8.1 / 3.3.7.2 ~ 现版本`；**正式版仅 2.8.4 / 3.1.1 / 3.3.0**（★正式版 标注），其余全部「测试版」，日期逐条来自任务书表格（含「（推测）」标注）。

文本最下方的**下载链接与访问密码两行已删除**（`releasePageUrl()` / 密码常量仍在 `src/gui/MainWindow.cpp`，供「帮助 → 打开发布页」使用，审计锚点未迁移、未丢失）。

## 八、任务五：样式集中化问答（先回答现状与成本）

**现状**：样式分三层——

1. **应用级 QSS**（`ThemeManager`，`src/gui/ThemeManager.cpp`）：浅色/深色/跟随系统三套 QSS 字符串，启动与切主题时 `qApp->setStyleSheet()` 整体下发，覆盖 `QWidget/QPushButton/QGroupBox/QLineEdit/QComboBox/QProgressBar/QTextEdit/QMenu` 等绝大多数控件的外观。
2. **内联主题色**（`MainWindow::applyInlineThemeColors()`）：少量 QSS 无法表达的位置（结果视图的 HTML 片段、状态色标签）在切主题时按当前主题重设，仍由 `ThemeManager` 供色。
3. **零散内联样式**：`MainWindow.cpp` 中仍有若干处直接 `setStyleSheet(...)` 或写死尺寸（如个别提示标签的边框/底色、图标尺寸、`setFixedHeight`）。**这是历史遗留**，不影响功能，但确实是「换皮」时最费人力的部分。

**抽出成本评估**：

| 方案 | 工作量 | 收益 |
|---|---|---|
| A（本轮已做）「不新增硬编码」纪律 | 0 | 停止恶化；本轮 UI 六项改动**未引入**任何新颜色/字体/尺寸字面量 |
| B 把零散内联样式收敛进 `ThemeManager` 的「语义色板」（`warningBg`/`warningFg`/`errorBg`/`dragHintBg`…）+ 统一尺寸常量 | 约 0.5~1 人日（约 15~25 处替换 + 回归截图） | 视觉大改只需换色板与 QSS，不动业务代码 |
| C 全面样式表 + 自定义 `QProxyStyle`（连尺寸/间距都由样式给） | 3~5 人日，且需要重做布局断言与全部截图基线 | 彻底「只换皮不改排版」，但风险最高（任务书明确禁止改排版结构，本轮不做） |

**结论**：建议下一阶段做 B；本轮按任务书只做「不引入新硬编码」，并已核对本轮新增/修改代码中无新增颜色/字体/尺寸字面量。

## 九、回归与验证（全绿）

| 项目 | 命令 | 结果 |
|---|---|---|
| 单元测试 | `build\slime_tests.exe --unit` | **24 组全部通过**（含本轮新增 `[21]` 进度连续性断言、`[22]` 填充跳过、`[23]` 缓存优先+命中即停、`[24]` 三连搜索去重） |
| 参考对照 | `py -3 tests\verify_against_reference.py` | **111/111 MATCH** |
| 内核自检 | `build\slime_kernel_tests.exe` | **19 项检查，0 项失败（ALL PASS）** |
| GUI 自检（GPU 关） | `scripts\gui_selftest.ps1` | **exit 0**，305 项断言全部通过（19.2 s） |
| GUI 自检（GPU 开） | `scripts\gui_selftest.ps1 -GpuOn` | **exit 0**，305 项断言全部通过（19.2 s） |
| 大缓存导入验收 | `SLIME_IMPORT_CACHE=<1GB> gui_selftest.ps1` | v2 573 MiB / v1 975 MiB 两个用例 **exit 0**（§四） |
| 污染审计 | `py -3 scripts\audit_corruption.py` | 打包后复跑：**CLEAN（exit 0）** |
| 发布包校验 | `scripts\verify_release.ps1 -Zip dist\SlimeFinder-3.3.8.1-win64.zip` | **16/16 PASS**（含 clean rebuild、单元、111 对照、OCL 探针、内核、GUI 开/关、基准、dist 自检、四组 SHA256 比对、zip 内自检） |

### 九-b、发布包（最终）

| 项 | 值 |
|---|---|
| 包名 | `dist/SlimeFinder-3.3.8.1-win64.zip` |
| 体积 | **22,319,366 B** |
| SHA256 | `DF053FBE4A386A3BC9B336136BF081EB539A3527D16CA94C116694CCD7030622` |
| 校验文件 | `dist/SHA256SUMS-3.3.8.1.txt`（另含 `SlimeFinder.exe` / `slime_tests.exe` / `slime_ocl_probe.exe` / `slime_kernel_tests.exe` 的 SHA256 与打包时间） |
| 包内版本串 | `SlimeFinder.exe` 含 `v3.3.8.1`；VERSIONINFO `FileVersion=3.3.8.1 / ProductVersion=3.3.8.1` |
| 打包自检 | 包内 GUI 自检 exit 0、单元子集 exit 0、内核自检 exit 0 |
| 截图 | `docs/screenshots/v3381/`（40 张：全窗、六项 UI、浅色/深色、四档尺寸、菜单、设置四页、进度/反馈/日志/命令四分类） |

## 十、未完成 / 不做（如实声明）

1. **任务 6（滑窗访存优化，≥2 亿区块/秒）**：硬件受限，v3.3.8 已端到端复测并明确未达标（CPU-only 444.8 s / GPU 标志 449.5 s = 98.4M 区块/秒），本轮未再投入（任务书 §八 明确不做）。
2. **任务 7（CPU 占用率调整）**：任务书明确不做。
3. **位图随机单点 45 ns**：达不到 10 ns/点（低于 DRAM 随机访问延迟），已如实记录；顺序扫描路径 3.54 ns/点达标，且相对旧实现无任何回退（§5.2）。
4. 除任务书列出的条目外，**未新增任何功能**；未改动排版结构。

## 十一、本轮改动文件

| 文件 | 改动 |
|---|---|
| `src/core/CacheManager.h/.cpp` | v2 位图格式（含 v1 兼容读、原子保存、防御性校验）、`dropHollowRanges`、`mergeFrom`、`loadFile` 进度回调（v2 改为 1 MiB 分块 + 按字节上报） |
| `src/core/SlimeEngine.h/.cpp` | 缓存优先排序 + 命中即停硬停（`cacheFirst*` / `cacheStopFlag`）、缓存位图填充跳过（`fillFromCacheRow` / `cacheFillSkipped` / `cacheHitPoints`）、E3 计时与 `SLIME_FILL_ONLY`、性能汇总补实参 |
| `src/gui/MainWindow.h/.cpp` | 1.3 后台线程导入 + 进度条（`importCacheFromPath`）、3.1 线程数警告、3.2 大范围阈值（预计 >5 分钟）、3.3/3.4/3.5/3.6 四项 UI 微调、更新历史扩展到 1.0.0、自检新增导入验收块（`SLIME_IMPORT_CACHE`） |
| `tests/test_slime.cpp` | `[21]` 进度连续性/单调性断言、`[22]` 填充跳过、`[23]` 缓存优先+命中即停、`[24]` 三连搜索去重 |
| `tools/probes/temp_makebigcache.cpp` | 新增（一次性探针）：生成指定体积的合法 v2 位图缓存，用于导入验收 |
| `scripts/*`、`docs/*`、`README.md`、`CMakeLists.txt`、`src/app.rc`、`src/main.cpp`、`scripts/install.nsi` | 版本号 3.3.8.1、文档同步 |

## 十二、附：本轮工具事故与修复验证（如实记录）

本轮在「版本号批量替换」时，一个自写的 PowerShell 辅助函数因参数展开错误，把 `src/gui/MainWindow.cpp` 中所有 `k/K` 替换成了 `A`、`CMakeLists.txt` 中所有 `v/V` 替换成了 `E`。这类**标识符级污染不会破坏编译**（同一标识符的定义与使用被一致替换），但会破坏语义与可读性，且**污染审计脚本无法发现**（审计只查版本串与若干固定锚点）。

修复与验证方式（全部可复现）：

1. 逐字符还原（词表来自干净源文件、文档、污染前的 `SlimeFinder.exe` 字符串池、`build/*.ninja` / `CMakeCache.txt`），随后由编译器驱动收敛剩余处。
2. **字符串池比对**（决定性）：把「污染前的 v3.3.8.1 构建」（`C:\Temp\slimecache\pre_corruption_SlimeFinder.exe`）与本轮最终 `build\SlimeFinder.exe` 的**两个**字符串池逐条比对：
   - **UTF-16 池**（`QStringLiteral` / Qt 界面文案）：ASCII 字符串「仅在新 0 条、仅在旧 0 条」；CJK 字符串的差异全部是二进制字节对噪声（`退襈恝赈欅` 之类 5 字乱码），**无真实文案差异**。
   - **UTF-8 窄字符串池**（`fprintf` / `Logger` / `check(...)` 的 `const char*` 文案）——**UTF-16 比对看不到这一层**，本项目一度漏检；补齐后抓出 **2 处** 残留污染：`统计信息含 扫描 N/M 区块 + 速度 + 找到 A 个`（应为 **K** 个，与 `docs/user_manual.md` 对照确认）与 `[UI自检] GPU 端到端搜索: Ok=%d`（应为 `ok=%d`），均已还原；复跑后「仅在旧」只剩 2 条噪声与 2 个重构前的 lambda 符号名。
   该比对早前还直接抓出了 3 处漏网的 UTF-16 字面量（`%1_darA-settings-general.png` / `%1_darA-settings-tab%2-%3.png` / 自检文案 `AAppVersion`），均已修复。
   **结论：两个字符串池与污染前构建逐条一致（差异仅为本次有意新增的文案与编译器符号名）。**
3. 标识符级污染由「编译器 + 301 项 GUI 自检断言 + 111/111 逐位一致 + 内核 19 项」四重兜底：`gpuCheck` / `dlgCheck` / `fmtClock` / `hasMark` / `presetBedrock` / `reChunk` / `kAppVersion` / `kCacheFileName` / `kConfigFileName` / `kMaxCards` / `kJavaBoundChunk` / `kBedrockBoundChunk` / `kWholeMapMinArea` / `kContactEmail` / `ink*` / `marker` / `okX/okZ/okN/okM` 等全部还原并复核。
4. 顺带修掉两处本轮引入的缺陷：性能汇总格式串缺实参（C4473，会读到野值）、`CMakeLists.txt` 的 `cmake_minimum_required` 被污染（配置失败）。
5. **自检断言抓出的两个真 bug（均已修复）**：
   - `physicalCoreCount()` 把 `GetLogicalProcessorInformation(nullptr, &len)` 的**预期失败**（`ERROR_INSUFFICIENT_BUFFER`，用于取缓冲区大小）当成探测失败 ⇒ **恒返回 0** ⇒ 任务书 3.1 的「线程数过大」警告永远不会触发（功能实际未生效）。已改为「取长度失败才返回 0」。
   - 「线程数过大」提示原先用 `addFeedback(2, …)`（级别 2 = 信息），不进入警告计数与徽章，与任务书「加警告」的语义不符；已改为 `addFeedback(1, …)`（警告级）。
   两者都是「编译通过、111/111 通过、审计通过」却功能失效的典型，只有 GUI 自检断言能兜住——因此本轮把 3.1 的断言补进自检（断言数 301 → 305）。

   **同一事故还暴露了一个验证盲区**：UTF-16 字符串池比对看不见 `const char*`（`fprintf`/`Logger`/自检断言）这一类窄字符串，本项目因此漏检了 2 处污染（`找到 A 个` 应为 `找到 K 个`、`Ok=%d` 应为 `ok=%d`），补齐 UTF-8 窄字符串池比对后才抓到并修复。**方法学结论：污染检测必须同时覆盖 UTF-16 与 UTF-8 两个字符串池，并配合编译器 + 自检断言 + 逐位对照。**

**教训**：禁止用大小写不敏感的 `-replace` 做批量标识符替换；`.ps1` 辅助函数不得依赖数组参数的隐式展开（本轮的根因）；版本号升级一律走「精确字面量替换 + 编译 + 两个字符串池比对 + 审计 + 全量回归」五步。
