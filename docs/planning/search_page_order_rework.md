# 搜索页顺序重排（搜索内容 → 搜索方式 → 结果要求 → 执行设置）

> 分支：`flow-ui-order`（基于 `flow-integration` 5d21654）
> 范围：只改搜索页左参数列的分组顺序 / 可见性 / 三语文案 / GUI 自检；不改结果列表、环形 worker、地图控件、core、cmake、tests/unit。

## 1. 背景

上一轮 A/B/C 重排（`docs/planning/search_page_abc_rework.md`）把「搜索方式」放在最上面，而
「图案 / 密度模式」与「区域形状（方形 / 环形）」仍在下面的「结果要求」里。用户反馈：
环形四档扫描方式 / 匹配算法到底该选哪个，取决于「搜索什么类型的结果」和「区域形状」，
但现在必须先在下面选模式，再滚回上面选搜索方式，来回滚动。

因此本轮按「先选搜什么，再选怎么搜」重排为四组（从上到下）：

```text
① 搜索内容（新顶层 QGroupBox，新 i18n key ui.search_content_group）
  · 图案 / 密度模式单选
  · 方案预设 combo + presetHint + presetDisclaimer（随模式/形状变化，属于「搜什么」）
  · 图案模式：内嵌图案编辑器 + 方向锁定 lockDir + patternInfo
  · 密度模式：区域形状（正方形 / 环形）单选
② 搜索方式（ui.search_method_group）
  · 环形：环形扫描四档 combo（ringScanRow）
  · 图案 / 正方形密度：匹配算法块（原「匹配方式」gMatch）
③ 结果要求（ui.a62f39a46c98）
  · 搜索范围 gRange（半径 / 快捷范围 / 起点 / 坐标转换）
  · 最低密度 + 忽略阈值行（密度模式常显；图案模式整行隐藏）
  · densityAdvanced（只剩「正方形网格」，仅自定义方案 + 正方形显示）
  · 环形选项（模拟距离 + 环形内/外半径 + 群系权重）
  · 「停止与数量」gResult（目标数量 / 达到即停 / 智能范围）
④ 执行设置（ui.execution_group，本组保持不动）
  · 性能设置（线程 / GPU / GPU 高级面板）
  · 内存预算摘要（只读）
  · 缓存管理
```

## 2. 实现方式

- **复用原有控件对象与 `m_w->*` 指针，不重建控件**：只调整创建位置 / 父布局；
  `m_paramLay = groupsLay`、`groupsLay->addStretch()` 语义不变。
- 原「搜索模式」`gMode` 组被拆开、不再存在：
  - 模式单选 / 预设 / 图案编辑器 / 区域形状 → 新组 ①；
  - 最低密度+忽略阈值行 / densityAdvanced / ringOpts → 组 ③；
  - 目标数量 / 达到即停 / 智能范围 → 组 ③ 的「停止与数量」子分组。
- 新增两个可见性容器（`MainWindow.cpp` Widgets）：
  - `shapeRow`（① 内，区域形状行；仅密度模式可见）
  - `densityThresholdRow`（③ 内，最低密度+忽略阈值行；仅密度模式可见）
  - 另新增 `searchContentGroup` 指针；移除不再有意义的 `modeGroup` / `densityPanel` 指针。
- `updateSearchShapeEnabled()` 统一收敛 ①/②/③ 的可见性；`onModeChanged()` 先就地切换
  图案面板 / 区域形状 / 阈值行，再由前者统一收敛；`updateAdvancedVisibility()` 只负责
  `densityAdvanced`（正方形网格）与 `lockDir`（方向锁定），二者都只在自定义方案出现。

## 3. 可见性矩阵（GUI 自检覆盖）

| 状态 | ① 搜索内容 | ② 搜索方式 | ③ 结果要求 |
| --- | --- | --- | --- |
| 图案模式 | 图案编辑器可见；区域形状行隐藏 | 匹配算法块可见、环形扫描隐藏 | 搜索范围 + 停止与数量可见；最低密度行 / 正方形网格 / 环形选项隐藏 |
| 正方形密度 | 区域形状行可见 | 匹配算法块可见、环形扫描隐藏 | 最低密度+忽略阈值常显可编辑；正方形网格仅自定义可见；环形选项隐藏；停止与数量可见 |
| 环形 | 区域形状行可见（可切回方形） | 环形扫描下拉可见可用、匹配算法块隐藏 | 最低密度+忽略阈值常显可编辑；环形选项（模拟距离/内外半径/群系权重）可见；正方形网格隐藏；stopAtTarget / smartRange 隐藏（保持原语义） |

- ③ 结果要求整组**始终可见**；方案切换不整组隐藏，冲突只通过 `applyResultConflicts()`
  的 enable/disable 表达（忽略阈值时禁用最低密度/停止方式/智能范围）。
- `lockDir` 仍只在「图案模式 + 自定义方案」可见。

## 4. 三语 i18n

新增 1 个 key（其余全部复用旧 key）：

| key | zh-CN | zh-TW | en-US |
| --- | --- | --- | --- |
| `ui.search_content_group` | 搜索内容 | 搜尋內容 | Search Content |

`ui.5483493bb0d8`（旧「搜索模式」组标题）随 gMode 解散而不再被引用；key 保留未删（无破坏性）。

## 5. GUI 自检更新（MainWindowSelfTest.inc）

- 四组顺序：`m_paramLay->indexOf(①/②/③/④)` 递增（内容=0 方式=1 要求=2 执行=3）。
- 控件归属（isAncestorOf）：① 含图案/密度、方案预设、图案编辑器、区域形状；
  ② 含环形扫描 / 匹配方式；③ 含搜索范围、最低密度+忽略阈值、正方形网格、环形选项、停止与数量；
  ④ 含线程 / GPU / 缓存 / 内存预算摘要。
- 可见性矩阵（图案 / 正方形 / 环形）按上表断言，并保留原「无横向滚动条」断言
  （1280×860 / 1000×650 / 1920×1080 / 左栏 340px / 640×720 最小窗口）。
- 旧 A/B/C 断言按新结构调整（语义保留，未删除）：`modeGroup`→`searchContentGroup`，
  `densityPanel`→`shapeRow` + `densityThresholdRow`。
- `SLIME_SHOT` 场景新增「四组顺序」截图：`groups-order-1280x860` / `groups-order-1000x650`
  （搜索页左列滚动到顶部）。

## 6. 验证

独立构建目录 `build-order`（不影响其它分支的 `build/`）：

```powershell
cmake -S . -B build-order -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=C:/Qt/6.11.2/msvc2022_64
cmake --build build-order
build-order\slime_tests.exe --unit            # 退出码 0
# GUI 自检（offscreen；build-order\config.json 需预置参考环境 {"cache":{"maxSizeMB":1024}}）
$env:PATH="C:\Qt\6.11.2\msvc2022_64\bin;$env:PATH"; $env:QT_QPA_PLATFORM="offscreen"; $env:SLIME_SELFTEST="1"
.\build-order\SlimeFinder.exe                 # 退出码 0，日志含「[UI自检] 全部通过」
py -3 scripts\qa\check_ui_text.py             # 1469 keys x 3 languages
```

> 备注：GUI 自检的 GPU 大区域用例与 performance.log 用例在**同时有其它 SlimeFinder 进程**
> （例如并行 worktree 的 GUI 自检或正在运行的本体）占用 GPU 时会偶发假失败，重跑即过；
> 这是项目已知的环境风险（见 `search_page_abc_rework.md` §3.2 备注）。

## 7. 已知风险

- 左列顶层从 3 组变 4 组，竖向总高度略增；窄窗口下竖向滚动更多，但已通过现有一致性断言
  与「无横向滚动条」断言（含最小窗口 640×720）。
- `ui.5483493bb0d8` 成为未引用 key（保留未删）；外部文档/截图若引用旧「搜索模式」标题需同步。
- 搜索行为未改：所有 visible/enable 调整不影响 `runSearch` / `runRingSearch` 读取的控件值，
  结果语义（预设只控制 stop/smart/ignore/algorithm/target/lockDir）保持不变。
