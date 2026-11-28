# 搜索页 A/B/C 重排（P2）

> 分支：`flow-search-abc`（基于 `flow-integration` 1d253e5）
> 范围：搜索页左参数列布局重排 + 可见性 + 三语文案 + GUI 自检；只改 GUI/i18n，不改 core、不动搜索行为。

## 1. 背景与原则

HANDOFF §5 P2 与 `cache_and_search_ui_fixes.md`「搜索页重排规划」：

- 旧结构把「怎么搜」和「搜什么」混在一起：`搜索模式` 组里同时有图案/密度、方案预设、区域形状、
  环形模拟距离、目标数量；`最低密度 / 忽略阈值` 又藏在 `densityAdvanced`（仅自定义方案展开）。
- 原则：**搜索方式只决定采样/复核策略；最低密度、目标数量、模拟距离等属于结果要求。**

拆成三个顶层分组（从上到下 A → B → C）：

```text
A 搜索方式（怎么搜）
  · 环形：环形扫描 4 档（撒点抽取 / 预筛精修 / 环带上界复核 / 暴力遍布）
  · 图案 / 正方形密度：匹配方式（精确 / 分层 / 估算 / 暴力 + 参数 + 摘要）
B 结果要求（搜什么）
  · 搜索范围（半径 / 快捷范围 / 起点 / 坐标转换）
  · 搜索模式（图案/密度、方案预设、区域形状、最低密度+忽略阈值、正方形网格、环形模拟距离）
  · 停止与数量（目标数量 / 达到即停 / 智能范围）
C 执行设置
  · 性能设置（线程 / GPU 开关 / GPU 状态 / GPU 高级面板）
  · 内存预算摘要（只读）
  · 缓存管理（导入 / 清空 / 使用缓存 / 缓存状态）
```

约束：保留原 6 个子 QGroupBox 对象与内部控件（只换父布局），`m_w->*` 控件指针全部保持有效；
`m_paramLay`（构建页额外选项挂载点）语义不变；`groupsLay->addStretch()` 仍在最后。

## 2. 第一阶段：控件迁移 / 可见性 / 三语 / 自检

### 2.1 结构

| 顶层分组 | 新 Widgets 指针 | 子分组（原对象） |
| --- | --- | --- |
| A 搜索方式（`ui.search_method_group`） | `searchMethodGroup` / `ringScanRow` | `ringScanRow`（环形扫描 combo，从 `ringOpts` 迁出）+ `matchGroup`（原「匹配方式」） |
| B 结果要求（`ui.a62f39a46c98`） | `requirementGroup` | 原「搜索范围」`gRange`、原「搜索模式」`gMode`（`m_w->modeGroup`）、原「结果要求」`gResult`（标题改为新 key `ui.result_limits_group`「停止与数量」，避免与外层 B 同名） |
| C 执行设置（`ui.execution_group`） | `executionGroup` / `memoryBudgetLabel` | 原「性能设置」`gPerf`、内存预算摘要行、原「缓存管理」`gCache` |

关键迁移：

1. `ringScanCombo`（4 档）从 `ringOpts` 移到 A 的 `ringScanRow`；`ringOpts` 继续留在 B 的密度面板里
   承载环形专属项（第一阶段仅模拟距离）。
2. `targetCount` 从 `gMode` 底部移到 `gResult`（「停止与数量」），与 `stopAtTarget` / `smartRange` 同组。
3. `minDensity` + `ignoreThreshold` 从 `densityAdvanced` 移到 `densityPanel` 常显行；
   `densityAdvanced` 只剩「正方形网格」等高级项，仍只自定义方案显示。
4. 尺寸兼容：嵌套 QGroupBox 自带 14px 边框/内边距（主题 QSS `padding 4px 6px 6px 6px` + border），
   外层分组布局不再重复留白（`contentsMargins(0,4,0,4)`），左栏最小宽 360→370（宽文字语言 480→490），
   保证 1000×650 最小窗口下 QScrollArea 无横向滚动条。

### 2.2 可见性矩阵

| 状态 | A 搜索方式 | 最低密度 / 忽略阈值 | 正方形网格 | 模拟距离 | 匹配方式 | 目标数量/停止方式 |
| --- | --- | --- | --- | --- | --- | --- |
| 图案模式 | 匹配方式块可见（算法单选按原逻辑禁用） | 不可见（密度面板隐藏） | 不可见 | 不可见 | 可见 | 可见 |
| 正方形密度 | 匹配方式块可见 | **常显**（可用性由冲突裁决） | 仅自定义方案可见 | 不可见 | 可见 | 可见 |
| 环形 | 环形扫描下拉可见、匹配方式隐藏 | **常显**（本次修复点；默认方案下可编辑） | 隐藏 | 可见 | 隐藏 | 可见 |

- 结果要求（B）整组始终可见；方案预设不再整组收起，冲突仍由 `applyResultConflicts`
  以 enable/disable 表达（忽略阈值时禁用最低密度/停止方式/智能范围，并清掉后者勾选）。
- `lockDir`（方向锁定）只在图案模式 + 自定义方案可见（保持原语义）。
- `updateAdvancedVisibility` 不再控制 `resultGroup` / `matchGroup` / `ringScanRow`；
  A 里显示哪块由 `updateSearchShapeEnabled` 决定。

### 2.3 内存预算摘要（C，只读）

`updateMemoryBudgetUi()` 用 `slime::MemoryBudget::fromSettingMB(m_settings.memLimitMB)`（与搜索启动
完全同源）显示：自动/手动 + 分块位图 / pack LRU / GPU 主机 MB；超可用内存收敛时追加提示。
刷新点：构建后（`applyLanguage` 末尾）、设置变更后（`applyAppSettings` 末尾）。
tooltip 说明「在设置页调整内存上限（0 = 自动）」。第二阶段可选加镜像 QSpinBox（未做，见 §4）。

### 2.4 i18n 新增 key（zh-CN 基准，三语同步）

| key | zh-CN |
| --- | --- |
| `ui.search_method_group` | 搜索方式 |
| `ui.execution_group` | 执行设置 |
| `ui.result_limits_group` | 停止与数量 |
| `ui.memory_budget_label` | 内存预算: |
| `ui.memory_budget_summary` | `%1 · 分块位图 %2 MB · pack LRU %3 MB · GPU 主机 %4 MB` |
| `ui.memory_budget_manual` | 手动 %1 MB |
| `ui.memory_budget_clamped` | （已按可用内存收敛） |
| `ui.memory_budget_tip` | 在设置页调整内存上限（0 = 自动）…… |
| `ui.weight_biome` | 按群系权重排序（Java） |
| `ui.weight_biome_tip` | 仅 Java + 环形模式可用…… |
| `ui.ring_radius_moved` | 环形内/外半径已移到搜索页「结果要求」里的环形选项（默认 24 / 128 格）。 |

顺带修复 `retranslateModeUi` 的环形扫描 4 档文案：原实现只改前两项，会把「预筛精修」误写成
「暴力遍布」，现按 `itemData` 恢复各档文案。

### 2.5 GUI 自检新增/改写断言

- A/B/C 三个顶层分组存在，`m_paramLay->indexOf` 顺序 A→B→C；子控件归属（B 含区域形状/最低密度/
  目标数量/停止方式，A 含环形扫描/匹配方式，C 含线程/GPU/缓存/内存预算摘要）。
- 环形：环形扫描下拉可见且可用、匹配方式块隐藏、模拟距离可见可调、**最低密度可见且可编辑**、
  目标数量在结果要求组内可见。
- 正方形：匹配方式可见、环形扫描隐藏、最低密度/忽略阈值常显且可编辑。
- 图案：匹配方式块可见、最低密度不可见。
- 预设/自定义方案：结果要求始终可见、正方形网格仅自定义可见；内存预算摘要非空且含 MB 数字与 tooltip。
- 保留原有「无横向滚动条」断言（1280×860 / 1000×650 / 1920×1080 / 左栏 splitter 最左）。

## 3. 验证

### 3.1 第一阶段

```powershell
# build-abc 独立构建目录
cmake -S . -B build-abc -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=C:/Qt/6.11.2/msvc2022_64
cmake --build build-abc
build-abc\slime_tests.exe --unit                 # 退出码 0
# GUI 自检（offscreen；build-abc\config.json 需为仓库参考环境：{"cache":{"maxSizeMB":1024}}）
$env:PATH="C:\Qt\6.11.2\msvc2022_64\bin;$env:PATH"; $env:QT_QPA_PLATFORM="offscreen"; $env:SLIME_SELFTEST="1"
Start-Process build-abc\SlimeFinder.exe -Wait -PassThru   # 退出码 0，stdout 含「[UI自检] 全部通过」
py -3 scripts\qa\check_ui_text.py                # 1467 keys x 3 languages
```

结果（commit `c3c5f72`）：构建成功；单测全通过；GUI 自检 `exit=0` 且「全部通过」；i18n 通过。

### 3.2 第二阶段

同一条构建/单测/GUI 自检/i18n 命令链，结果：构建成功；单测全通过；GUI 自检 `exit=0` 且
「全部通过」（新增环形半径迁移、联动、config 落盘、设置页去重、群系权重默认/置灰/纯方块数口径断言）；
i18n `1468 keys x 3 languages` 通过。

> 备注：全新 build 目录首次运行会由程序写 `config.json` 默认 `cache.maxSizeMB=10240`，
> 而自检断言「config 默认 1024MB」按仓库既有参考环境（`build/config.json`）为 1024。
> 因此自检前需把 `build-abc/config.json` 预置为参考格式；这是环境差异，与本次改动无关。
> GPU 探测为原生 OpenCL 调用；若同一时间有其它进程（例如并行 worktree 的 GUI 自检）在用 GPU，
> 探测可能偶发原生崩溃（项目已知风险，重跑即可），与本次改动无关。

## 4. 第二阶段：环形半径 / 群系权重 / 内存摘要（已完成，除镜像 QSpinBox）

第一阶段 commit `c3c5f72` 且构建+单测+GUI 自检+i18n 全绿后实施。

### 4.1 环形内/外半径迁移

- B 的环形选项新增：内半径 `QSpinBox(0..64)`、外半径 `QSpinBox(16..1024)`（标签/提示复用
  `ui.key.RingInner/RingInnerTip/RingOuter/RingOuterTip`，单位「格」在框外）。
- 数据源仍是 `m_settings.afkRingInner/afkRingOuter`：改动立即写回 + `scheduleConfigWrite()` +
  `updateSimDistanceNote()`；`runRingSearch` 原本就读 `m_settings`，无需改动。
- 内外联动：外半径自动保持 ≥ 内半径+8（与 `runRingSearch` 的模拟距离兜底口径一致）。
- 设置页 `SettingsDialog` 的高级项里**移除**这两个控件（保留 `m_settings` 读写与出厂默认 24/128），
  原位留一行说明「已移到搜索页」（新 key `ui.ring_radius_moved`）。
- 加载/命令同步：构造函数 `loadAppSettings()` 后、`applyAppSettings()` 末尾调用
  `syncRingRadiusFromSettings()`；命令终端 `set ring_inner/ring_outer` 也因此同步控件。

### 4.2 群系权重开关

- B 的环形选项新增复选框 `weightBiome`（`ui.weight_biome`）「按群系权重排序（Java）」，默认勾选；
  仅 Java + 环形可编辑（`updateRingWeightBiomeEnabled()` 在 `applyVersionUi` 与
  `updateSearchShapeEnabled` 中维护），Bedrock 置灰。
- `MainWindowRingMap.inc` 小改（任务书允许的「小改」）：`RingSearchResult` 增加 `useBiomeWeight`；
  关闭时**不建群系图、不设 `o.weightAt`**，`measureStrictForCandidates` 的 `useBiome` 传 false
  → 排序/统计为纯方块数口径（`coveredWeight == coveredBlocks`）。
- 默认勾选时的代码路径与改动前完全一致（既有环形结果用例全部通过，作为回归证据）。

### 4.3 内存上限镜像（未做）

维持只读摘要 + 「在设置页调整内存上限」tooltip。未加镜像 `QSpinBox`：会与设置页控件重复，
且写回路径（`applyAppSettings`/`scheduleConfigWrite`）已有单一入口，风险收益比低，留给 lead 决定。

## 5. 已知风险

- B 内子分组 `gResult` 标题从复用「结果要求」改为新 key「停止与数量」，避免与外层 B 同名；
  若外部文档/截图引用旧标题需同步。
- 嵌套分组带来 14px 固定宽度开销：宽文字语言（en/zh-TW）左栏最小宽 490；若后续再嵌一层需重新核宽度。
- GUI 自检新增全局诊断输出（分组最小宽 / 横滚动诊断），仅 stderr，不影响界面与结果。
- 本阶段不改搜索行为：所有 enable/visible 变化不影响 runSearch/runRingSearch 读取的控件值。
