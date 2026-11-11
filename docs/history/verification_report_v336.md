# v3.3.6 验证报告（Bug 修复 + UI 调整 + 设置项扩展）

日期：2026-09-20 ｜ 基线：v3.3.5 ｜ 任务书：`模板：原始文件/v3.3.6 完整任务书.txt`
机器：AMD Ryzen 5 5600（6C/12T，AVX2）+ AMD Radeon RX 6750 GRE 10GB（OpenCL 2.0 AMD-APP 3652.0）
构建：MSVC 14.51 / Release `/O2 /openmp /GL /LTCG /utf-8 /Brepro`（确定性构建）

> 任务书要求：做不完的明说、不降级。本报告 §一 逐项列出状态；**未完成项：无**（§三 列出明确排除项）。

---

## 一、验证清单（任务书 §六）

### Bug 修复

| 验收项 | 状态 | 证据 |
|---|---|---|
| 全界面只有 1 个搜索按钮 | ✔ | 自检递归统计全部 `QPushButton`：文本含「开始搜索」者**恰好 1 个**且父控件为工具栏；工具菜单无重复搜索入口 |
| 全程序无「临界值」字样（除变更日志） | ✔ | 自检递归扫描全部控件 text/tooltip + 全部 `QAction`（text/tooltip）：命中 **0**；源码中相关函数、配置字段、状态栏分支已删除 |
| GPU 提醒已保留 | ✔ | GPU 开关 tooltip 与「GPU 状态」对话框正文均包含任务书原文提醒；自检逐字断言 |
| 设置按钮在工具栏（开始搜索左方） | ✔ | 工具栏最左为 `[⚙ 设置]`，自检断言父控件为工具栏且 `x` 坐标小于「开始搜索」；点击后进入设置入口（自检模式只记录不弹窗）；工具菜单已无「程序设置」 |

### UI 调整

| 验收项 | 状态 | 证据 |
|---|---|---|
| 进度条在工具栏下方 | ✔ | 自检实测：工具栏 y=17 < **进度行 y=49** < 主内容区 y=77；进度行高 **28px**、宽 992/1000（横跨整窗） |
| 底部面板为「左竖排导航 + 右内容」 | ✔ | `#navList`（`QListWidget`，宽 **120px**）+ `#bottomStack`（`QStackedWidget`，4 页）；自检断言 4 项名称/顺序与「一一对应」 |
| 左侧导航计数只统计警告和错误 | ✔ | 造 1 警告 + 1 错误 + 1 成功：`warningCount=1 / errorCount=1 / 成功级 count=0`；导航文本 `● 反馈 ⚠️1❌1`；`badgeText` 四种组合（`⚠️2❌1` / `⚠️2` / `❌1` / 空）逐一断言；进度/日志/命令三项均不含计数 |
| 进度分类为纯文字流水 | ✔ | 格式函数与任务书样例**逐字一致**；真实搜索已写入流水（见 §五 摘录）；0.5 秒追加路径单独断言；`[清空] [复制全部]` 存在且复制内容含完成行 |
| 反馈分类按时间顺序，无成功类型 | ✔ | 扁平列表（顶层行数 == 条目数，无分组节点）；插入「第一条/第二条」后顺序为 上→下；`ℹ️`/`⚠️` 图标与时间戳断言；成功级 `add()` 返回 `nullptr` 且不计数；面板仅 **2 个按钮**（无过滤按钮） |
| 日志分类默认显示「未启用」提示 | ✔ | 关闭态下提示行文本含「未启用」；启用态显示级别过滤 + 自动滚动 + 清空 + 复制全部 + 流水（沿用 v3.3.4 实现，新增自动滚动/最大行数由设置驱动） |
| 命令分类保持原功能 | ✔ | `#bottomStack` 第 4 页即 `CommandTerminal`，命令集与历史/Tab 补全未改动 |

### 设置项

| 验收项 | 状态 | 证据 |
|---|---|---|
| 通用：夜间模式（三档）、提示音、音量 | ✔ | 主题 `QComboBox` 三项数据值为 `light/dark/system`（显示 浅色/深色/跟随系统）；提示音复选框 + 音量 `QSlider(0..100)` + 实时百分比标签（默认 70%） |
| 搜索：GPU 提醒开关、大范围警告阈值 | ✔ | `showGpuReminder`（默认开）+ 说明；`largeRangeWarnChunks` 默认 **100000000**，范围 1e6~2e9；搜索超出阈值时弹窗确认（自检断言该分支被走到，`m_largeRangeWarned=true`） |
| 缓存：自动加载、目录、容量、清空 | ✔ | 沿用 v3.3.5 四项（自动加载 / 目录 + 浏览 / 容量上限 / 清空所有缓存） |
| 日志：启用、级别、路径、保留天数、自动滚动、最大行数 | ✔ | 六项齐全；最大行数默认 **10000**（1000~200000）；自检断言 `LogPanel::maxLines()/autoScroll()` 随设置立即生效 |
| 高级：更新提醒天数、崩溃转储、调试模式、重置所有设置 | ✔ | 四项齐全；`[重置所有设置]` 只把五页填回出厂值（仍需点「应用」，自检未点应用时 `applied()==false`） |
| 所有设置项双向同步到 config.json | ✔ | 自检：9 个新增项 `setValues → currentValues` 往返一致；写入 config.json 后清空内存重读，9 项全部一致；主界面把当前搜索选项在 `closeEvent()` 写回为下次默认值 |

### 回归测试与截图

| 验收项 | 状态 | 证据 |
|---|---|---|
| clean 重建 exit 0 | ✔ | 第 1 轮 `clean rebuild exit=0, errorLines=0` |
| 单元测试 18 组通过 | ✔ | `slime_tests.exe --unit` exit 0，输出「单元测试全部通过 ✔」 |
| 111/111 参考对照通过 | ✔ | `通过 111 项，失败 0 项`（引擎新增的实时计数不参与判定，结果集合未变） |
| GUI 自检（GPU 开/关）exit 0 | ✔ | 两轮均 exit 0、`[UI自检] 全部通过 ✔`，无 `[UI自检 FAIL]` 行 |
| 内核自检 17/17 | ✔ | `汇总：17 项检查，0 项失败` / `RESULT: ALL PASS` |
| OpenCL 探测 PASS | ✔ | `=== 结果: PASS (失败 0 项) ===` |
| 污染审计 CLEAN | ✔ | `scripts/audit_corruption.py` 退出码 0（7 类检查全部无发现） |
| dist/zip 哈希一致 | ✔ | 打包脚本第 5 步四个二进制 `build==zip: True`；复验轮 `hash build==dist` 与 `hash zip==dist` 全 PASS |
| UI 截图 | ✔ | `docs/screenshots/v336/` 共 **37 张**（浅色 27 + 深色 10），覆盖任务书要求：默认尺寸（浅色/深色）、底部 4 分类（浅色/深色各 4）、设置对话框 5 个标签页（浅色/深色）、最小尺寸、拖动 splitter 后 |

---

## 二、任务书 §八：15 个必须回答的问题

1. **搜索按钮是否只有 1 个？**
   是。v3.3.5 曾在底部「进度」页额外放了一组 `[开始搜索][取消]`，本版本删除该组；自检递归统计全部
   `QPushButton`，文本含「开始搜索」的**恰好 1 个**，且其父控件是工具栏（`m_w->start->parentWidget() == m_toolBar`）。

2. **所有「临界值」字样是否已移除？**
   是。删除了 `gpuThresholdChunks()` / `gpuThresholdText()` / `gpuSmallRangeHintText()` /
   `updateGpuThresholdUi()`、`config.json` 的 `gpuBench.crossLo/crossHi`、状态栏「范围较小，建议关闭 GPU」
   分支，以及界面/帮助/命令输出中的全部相关文案。自检**递归扫描所有控件 text/tooltip 与所有 QAction**
   断言命中 0；唯一保留该词的位置是「帮助 → 更新历史」里的版本变更记录（任务书明确豁免）。

3. **GPU 提醒是否保留？**
   是。提醒文案与任务书逐字一致：**「GPU 加速不一定比纯 CPU 强。实际效果取决于你的硬件配置和搜索范围，
   建议实际测试后决定是否启用。」** 出现在两处：① GPU 开关的 tooltip；② 「工具 → GPU 状态」对话框正文。
   另外设置页「搜索 → 显示 GPU 加速提醒」控制勾选 GPU 时是否主动推送该提醒（默认开）。

4. **设置按钮是否在工具栏？**
   是。工具栏最左侧为 `[⚙ 设置]`（文字 + 齿轮图标），点击打开五标签页设置对话框；
   「工具 → 程序设置…」已删除（自检断言工具菜单无该动作）。

5. **进度条是否在工具栏下方？**
   是。中央部件最上方是 28px 高、横跨整窗的进度行 `进度: [████░░] 65% 正在扫描区块…`；
   实测坐标：工具栏 y=17 → 进度行 y=49 → 主内容区 y=77。已用时/预计剩余移到状态栏。

6. **底部面板是否为「左竖排导航 + 右内容」？**
   是。`QTabWidget` 已被替换：左 `QListWidget`（宽 120px，四项 进度/反馈/日志/命令）+ 右 `QStackedWidget`
   （四页一一对应）；当前项 `●` + 加粗 + 高亮背景，未选中 `○`。

7. **左侧导航计数是否只统计警告和错误？**
   是。只有「反馈」项带计数，文本由 `FeedbackPanel::badgeText(warnings, errors)` 生成：
   两者都有 → `反馈 ⚠️2❌1`；只有警告 → `反馈 ⚠️2`；只有错误 → `反馈 ❌1`；都没有 → `反馈`。
   成功与信息级**不计数**（自检：1 警告 + 1 错误 + 1 成功 → `⚠️1❌1`，成功 count=0）。

8. **进度分类是否为纯文字流水？**
   是。每 0.5 秒追加一条 `[HH:MM:SS] 已用时 T 秒，扫描 N 区块，找到 M 个`，完成时追加
   `[HH:MM:SS] 已完成：共扫描 N 区块，找到 M 个结果，耗时 T 秒`，取消/出错各留一条说明行；
   面板提供 `[清空] [复制全部]`。为让「找到 M 个」是真实数据，引擎新增 `SearchOptions::liveMatches`
   实时命中计数（relaxed 原子自增，**只计数、不参与任何判定**，111/111 对照未变）。

9. **反馈分类是否按时间顺序、无成功类型？**
   是。取消四级分组与过滤按钮，改为扁平单列、**时间顺序（最新在最下方）**、**成功消息不再显示**，
   保留 `❌ ⚠️ ℹ️` 图标，行格式 `❌ 14:23:15 GPU 初始化失败，已回退到 CPU 模式`；
   面板只剩 `[清空] [复制全部]`（自检断言按钮数 == 2、顶层行数 == 条目数、顺序与图标）。

10. **夜间模式是否实现三档切换？**
    是。`ThemeManager`（新增 `src/utils/ThemeManager.{h,cpp}`）提供
    `light / dark / system` 三档：`apply(qApp, mode)` 立即设置应用级 QSS（**无需重启**）；
    「跟随系统」读取注册表 `HKCU\...\Themes\Personalize\AppsUseLightTheme`（0=深色、1=浅色），
    并在 Qt 6.5+ 的 `QStyleHints::colorSchemeChanged` 上自动跟随系统切换。
    自检断言三档字符串解析、显示名、切换后 `qApp->styleSheet()` 与 `isDark()` 立即变化。

11. **深色模式下所有界面元素是否可读？**
    是。深色 QSS 覆盖任务书列出的全部类别（`QWidget/QMainWindow/QDialog`、`QPushButton`、
    `QLineEdit`、`QComboBox`（含下拉与弹出列表）、`QSpinBox/QDoubleSpinBox`、`QGroupBox`、
    `QTabWidget/QTabBar`、`QTreeWidget/QHeaderView`、`QTextEdit/QPlainTextEdit`、`QListWidget`、
    `QScrollBar`（纵横及子控件）、`QMenuBar/QMenu/QToolBar/QToolButton`、`QSplitter::handle`、
    `QStatusBar/QProgressBar/QCheckBox/QRadioButton/QSlider/QLabel/QToolTip/QMessageBox`）以及本程序的
    对象名规则（`#navList` `#progressRow` `#updateBadge` `#feedbackList` `#progressStream(View)`）。
    主配色 `#1e1e1e` / `#e6e6e6`（对比度 **13.36:1**），输入区 12.27:1，面板 11.00:1，按钮 10.12:1，
    强调色 `#0e639c`/白字 6.40:1，禁用态 5.10:1 —— 全部 ≥ 4.5:1。
    配套清理了会覆盖全局 QSS 的内联样式（分割条、更新徽章改为 QSS 规则），其余状态色
    （说明文字、模式警告条、GPU 状态、种子高亮）改为随主题切换的 `applyInlineThemeColors()`。
    自检断言深色 QSS 含全部必需选择器 + `contrastOk()` 达标。

12. **设置项是否覆盖通用/搜索/缓存/日志/高级？**
    是，仍是 5 个标签页、共 **25 个**设置项（通用 5 / 搜索 8 / 缓存 3 / 日志 6 / 高级 3），
    每个选项都带一行 8pt 灰色说明；新增 9 项如 §一 所示。

13. **所有设置是否双向同步到 config.json？**
    是。设置对话框点「应用」→ 立即生效并写入 `config.json` 的 `settings.*`；
    主界面把当前搜索选项（模式/线程/目标数量/及时反馈）在 `closeEvent()` 写回为下次默认值；
    「恢复默认」与「高级 → 重置所有设置」填回出厂值后再由「应用」落盘。
    自检：9 个新增项往返 + 写盘 + 重读全部一致。

14. **回归测试是否全部通过？**
    是：clean 重建 exit 0 / 0 error；单元 18 组通过；111/111 参考对照通过；OpenCL 探测 PASS；
    内核自检 17/17；GUI 自检（GPU 关、GPU 开）均 exit 0；基准测试通过；dist/zip 哈希一致（见 §四）。

15. **污染审计是否 CLEAN？**
    是。`py -3 scripts\audit_corruption.py` 退出码 **0**：已知坏片段、词典判定、跨文件还原验证、
    稀有二元组、关键字符串常量、产物一致性、文件卫生（NUL/非法 UTF-8/U+FFFD）七类检查均无发现；
    同时覆盖版本常量漂移（见 §六 第 3 条）。

---

## 三、实现要点与明确排除项

实现细节见 `docs/architecture.md` §16（Bug 修复 / 界面调整 / 设置项 / 夜间模式 / 提示音 / 验证）。
任务书 §五「不实现项」**全部按排除处理**，未做任何尝试：字体大小调整、语言切换、关闭最小化到托盘、
启动时自动检查更新、缓存压缩、导出缓存、快捷键自定义、实验性功能开关。

新增/改动文件（摘要）：

| 文件 | 说明 |
|---|---|
| `src/gui/ProgressStreamPanel.{h,cpp}` | 新增（子代理实现 + 独立运行时自检）：进度分类纯文字流水 |
| `src/utils/ThemeManager.{h,cpp}` | 新增（子代理实现 + 独立运行时自检）：夜间模式三档 + QSS + 对比度工具 |
| `src/utils/SoundPlayer.{h,cpp}` | 新增（子代理实现 + 独立运行时自检）：winmm `waveOut` 提示音（可调音量） |
| `src/gui/FeedbackPanel.{h,cpp}` | 重写（子代理实现 + 独立运行时自检）：时间顺序、过滤成功、无分组/无过滤 |
| `src/gui/SettingsDialog.cpp` | 扩展（子代理实现）：25 项五页 + 重置所有设置 |
| `src/gui/MainWindow.{h,cpp}` | 主改造：删除重复按钮、移除阈值、设置入口、进度行、底部导航、流水/反馈接线、自检第 [14] 组 |
| `src/gui/LogPanel.{h,cpp}` | 新增 `setAutoScroll/setMaxLines`（设置项驱动） |
| `src/gui/HelpDialog.cpp` | GPU 章节改写为提醒文案（删除阈值说明） |
| `src/core/SlimeEngine.h/.cpp` | 新增 `SearchOptions::liveMatches` 实时命中计数（只计数、不参与判定） |
| `CMakeLists.txt` | 新增 3 个源文件、链接 `winmm`、版本号 3.3.6 |

---

## 四、回归测试与实测记录

**第 1 轮（clean 全量重建 + 全部检查，日志 `%TEMP%\slime_verify_20260920_221937`）**：
`total=14 pass=11 fail=3` —— 3 个 FAIL 全是「源码已改、尚未重新打包」的哈希差（预期）；功能 10 步全部 PASS：

```
[PASS] clean rebuild            exit=0, errorLines=0
[PASS] unit tests               exit=0, 单元测试全部通过 ✔
[PASS] reference 111            exit=0（通过 111 项，失败 0 项）
[PASS] ocl probe                exit=0, === 结果: PASS (失败 0 项) ===
[PASS] SIMD kernel self-test    exit=0, RESULT: ALL PASS
[PASS] gui selftest (gpu=off)   exit=0（[UI自检] 全部通过 ✔）
[PASS] gui selftest (gpu=on)    exit=0（[UI自检] 全部通过 ✔）
[PASS] benchmark                exit=0
[PASS] benchmark (v3.2.1 基线)  exit=0
[PASS] dist self-test           exit=0
```

**第 2 轮（打包后复验，`-SkipClean`，日志 `%TEMP%\slime_verify_20260920_222331`）**：
**`total=16 pass=16 fail=0`（exit 0）** —— 含 `hash build==dist`、3 个二进制、`hash zip==dist`
与 zip 自检全部 PASS。打包脚本第 5 步亦已断言 `build == zip`（4 个二进制）、exe 版本串 `v3.3.6`、
VERSIONINFO `3.3.6.0 / 3.3.6`、**包内 GUI 自检 exit 0**、包内单元测试（组 15）exit 0、包内内核自检 exit 0。

**交付哈希（SHA256，最终构建）**

| 文件 | SHA256 |
|---|---|
| `build\SlimeFinder.exe` = dist = zip | `03BB61C4463360C16A37CF1F4FE72F9DF6AF569C3283EC75E0234CEC0A2B2E27` |
| `build\slime_tests.exe` = dist = zip | `E9BF51D2625A85225DB0D9FC1958260CB61088CA556330CEE81568309FA3F145` |
| `build\slime_ocl_probe.exe` = dist = zip | `58390F0FF4A9384E1A41E0204E4CDD0DC478F7DBE4633A6C3EC2B3E5A18FC6CC` |
| `build\slime_kernel_tests.exe` = dist = zip | `65AEB211176FC6E0A4534989D43E5E76925DF8E163E252661933FAD58639CA0C` |
| `dist\SlimeFinder-3.3.6-win64.zip` | 见 `dist/SHA256SUMS-3.3.6.txt`（交付以该文件为准） |

**证据文件**（`docs/`，UTF-8）：`unit_test_v336.txt`、`verify111_v336.txt`、`ocl_probe_v336.txt`、
`kernel_selftest_v336.txt`、`bench_v336.txt`、`gui_selftest_v336_gpu_off.txt`、
`gui_selftest_v336_gpu_on.txt`、`gui_selftest_v336_gpu_on_stderr.txt`、`pkgverify_selftest_v336.txt`、
`corruption_audit_v336.txt`。导出：`powershell -File scripts\export_evidence.ps1 -LogDir <日志目录> -Tag v336`。

**UI 截图**：`docs/screenshots/v336/`（37 张）—— 浅色：默认 1280×860、最小 1000×650、1920×1080、
splitter 最左/最右/底部最高、四个分类、五个菜单、设置对话框 5 页、教程、缓存导入、三种算法模式；
深色：默认尺寸、四个分类、设置对话框 5 页。

---

## 五、关键实测证据摘录（第 1 轮 GUI 自检输出）

```
[UI自检] v3.3.6 检查（搜索按钮/临界值移除/设置入口/进度行/底部导航/流水/反馈/日志/主题/设置往返）…
[UI自检]   进度行 y=49（工具栏 y=17，内容区 y=77）宽=992/1000
[UI自检]   反馈徽章: ● 反馈 ⚠️1❌1
[UI自检] 底部导航：进度 / 反馈 / 日志 / 命令
[UI自检]   流水[0]: [22:21:55] 已完成：共扫描 262144 区块，找到 10 个结果，耗时 0.0 秒
[UI自检]   流水[3]: [22:21:56] 已取消（已扫描 0 区块）
[UI自检]   流水[末]: [22:21:56] 已取消（已扫描 7439999 区块）
[UI自检]   0.5s 流水样例: [22:21:56] 已用时 0.1 秒，扫描 250000 区块，找到 7 个
[UI自检] 实测吞吐率：CPU 16.08 亿区块/秒 ｜ GPU 78.86 亿区块/秒（含回读，config.json 缓存）
[UI自检] GPU 端到端搜索: ok=1 结果=10
[UI自检] 取消响应(立即): 调用 1.0 ms，停止 23.7 ms
[UI自检] v3.3.6 检查完成
```

OpenCL 探测（第 1 轮）：`gfx1031 (Advanced Micro Devices, Inc.) | OpenCL 2.0 AMD-APP (3652.0) |
CU: 18 | 显存 10224 MB`，引擎级 CPU/GPU 结果一致=是。

深色模式对比度（`ThemeManager::contrastOk` 实测输出）：
`#1e1e1e/#e6e6e6 = 13.36:1`、`#252526/#e6e6e6 = 12.27:1`、`#2d2d30/#e6e6e6 = 11.00:1`、
`#333333/#e6e6e6 = 10.12:1`、`#0e639c/#ffffff = 6.40:1`、`#2a2a2a/#9a9a9a = 5.10:1`。

---

## 六、实施记录

1. **分工**：`ProgressStreamPanel`、`ThemeManager`、`SoundPlayer`、`FeedbackPanel` 重写、
   `SettingsDialog` 扩展由 5 个子代理按**冻结接口**并行实现，各自用 MSVC `/Zs` 语法检查把关，
   其中 4 个还用「moc + cl 临时工程」做了独立运行时自检（共 176 项断言）；主窗口改造、引擎计数、
   CMake/版本、帮助与更新历史、自检第 [14] 组、截图、文档与打包由本轮完成。
2. **接口冻结先行**：为避免并行冲突，先改 `SettingsDialog.h`（`AppSettings` 25 项 + 控件声明）并
   为子代理提供 `scripts/syntax_check.cmd`（`/Zs` 不写产物，可与真实构建并行）。
3. **过程中发现并修正的问题**（均已验证）：
   * **版本常量漂移**：`main.cpp` 的 `kVersion` 已升 3.3.6，但 `MainWindow.cpp` 另有一份
     `kAppVersion` 仍是 v3.3.5（自检断言立即报错暴露）。修正并把「源码版本常量 vs app.rc 版本」
     加入 `audit_corruption.py` 的版本漂移检查（v3.3.6 新增规则）。
   * **自检断言写死版本号**：3 处断言硬编码 `v3.3.5`，改为以 `kAppVersion` 为基准推导，
     避免以后升级再漏改。
   * **导航名称断言**：反馈项带 `⚠️1❌1` 徽章后，名称比较需先剥离徽章（已修正）。
   * **内联样式与深色主题冲突**：分割条与更新徽章的内联样式会覆盖全局 QSS，已改为
     `QSplitter::handle` / `#updateBadge` 的 QSS 规则；说明文字等状态色改为随主题刷新。
4. **未降级声明**：任务书列出的 Bug 修复、4 项 UI 调整、日志/命令分类要求、设置项扩展与夜间模式
   全部按原文实现；§五「不实现项」按排除处理，未做替代实现。
