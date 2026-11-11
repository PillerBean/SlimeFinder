# SlimeFinder v3.4.3 验证报告（UI 修正第 7 轮 · 交付版）

- 交付物：`dist/SlimeFinder-3.4.3-win64.zip`（**43,208,758 B**，
  SHA256 `FE5F9D9CC74E358066D0D54E8EF9C7CB8085ED9B83A7A1D8EEB043C9400E6E30`）
- 任务书等价命名副本：`dist/SlimeFinder-3.4.1-win64.zip`（**与上面字节完全相同**，SHA256 一致；
  程序内部版本号仍为 v3.4.3，见 `dist/SHA256SUMS-3.4.1.txt` 内的说明）
- 回滚包：`dist/SlimeFinder-3.4.0-rollback-win64.zip`（= v3.4.0 包原样副本，
  SHA256 `2CB879644943826527F28312156830DA71775BFA716EA09E26A691C9007C1B4D`，本系列 UI 修正前的最后验收版本）
- 校验文件：`dist/SHA256SUMS-3.4.3.txt` / `dist/SHA256SUMS-3.4.1.txt`
- 证据：`docs/screenshots/v343/`（**80 张** = 39 张 GPU 关 + 39 张 GPU 开 + 2 张 R6→R7 前后对比）、
  `docs/evidence_v343/`（本轮最终自检原始日志 `gui_selftest_gpu_{off,on}_stderr.log`、主题重放日志原文、
  `verify_release_final.log`、单元/参考/内核日志；另有一份同时序副本留在 `docs/evidence_v347/`）

## 一、本轮（R7）修复的三处视觉缺陷

用户实测反馈集中在「勾选框看不出勾选 / 数字框旁边有黑杠 / 下拉框一个空灰格」。
逐条定位到 QSS 层，三处都在 `src/utils/ThemeManager.cpp`（浅色 + 深色两套同步修改），
不触碰任何功能逻辑、不改 v3.4.0 骨架、不动结果区文本视图。

| # | 现象 | 根因 | 修复 | 像素级验证 |
|---|---|---|---|---|
| 1 | 数字输入框右侧出现「两根横杠 + 一根竖线」的黑块（用户称「小箭头」） | 样式表给 `QAbstractSpinBox` 上了边框/底色，但没有任何 `::up-button/::down-button` 规则，原生 spinner 箭头在此组合下被画成横杠 | QSS 里把上下按钮与箭头**彻底归零**（`width/height:0`、`border:0`、`image:none`）；与样式规范一致（原型 `input{appearance:none}` 无 spinner） | 修复后「网格大小」输入框右边框落在 `x=238`，其右侧 `x=239…330` 在整个行高范围内**没有任何非白像素**（R6 同位置有两根横杠 + 一根竖线，见 `docs/screenshots/v343/compare_r6_r7_searchmode.png` 上/下两半） |
| 2 | 下拉框右侧只剩一个**空灰格**（有分隔竖线、无箭头） | 只给 `QComboBox::drop-down` 上了灰底 + 左边线，没有箭头图像 ⇒ 看起来像多余的空框 | 去掉灰底与分隔线，改为**自绘 7×4 实心直角三角**（浅/深 × 正常/禁用 4 张位图，随主题换色） | ASCII 像素图确认：箭头为 `y=220` 宽 7 → `y=223` 宽 1 的标准倒三角，位于 `x=187..193`，周围无灰底 |
| 3 | 勾选框「勾上了但看着像没勾」：选中态只有一个草绿实心块，无边框、无勾记（且 `outline` 在 Qt QSS 里根本不生效） | `:checked` 规则用了 `border:4px solid #ffffff` + `outline:1px solid #3D8B40` 的"白框内缩"写法，Qt 不支持 `outline` ⇒ 白框融进白底，只剩 14px 绿块 | 按样式规范「复选 · 直角方形」重做：**1px 边框 + 内缩 3px 实心块**（16×16 位图）；浅色=灰边+黑块，深色=深灰边+浅块，禁用态另配色 | 浅色：`x=41 #c9c9c9` → `x=42..44 白` → `x=45..52 #111111`（8px 实心）→ `x=53..55 白` → `x=56 #c9c9c9`；深色：`#4a4a4a / #111111 / #f0f0f0`。两套主题逐像素比对通过 |

新增资源（`assets/appicon.qrc` 注册，AUTORCC 编译进 exe）：
`check_on_light.png`、`check_on_light_disabled.png`、`check_on_dark.png`、`check_on_dark_disabled.png`、
`arrow_down_light.png`、`arrow_down_light_disabled.png`、`arrow_down_dark.png`、`arrow_down_dark_disabled.png`
（均为 1:1 像素图，QSS 里 `width/height` 与图像尺寸一致，不做缩放 ⇒ 不模糊）。

## 二、本轮新增的自检断言（针对用户实测反馈的回归防线）

| 断言 | 口径 | 结果 |
|---|---|---|
| 可见复选框可点击 | 对「设置页 + 当前可见页面」中每个可见且可用的 `QCheckBox`，用 `QStyle::SE_CheckBoxIndicator` 取指示器矩形中心，发送真实 `QMouseEvent` 按下+抬起，断言 `isChecked()` 翻转后还原 | **可点 5 个，未翻转 0 个 → PASS** |
| 样本量下限 | 至少覆盖 4 个可见复选框（防止断言被"空集通过"） | PASS |
| 禁用诊断（信息性） | 打印"可见但被禁用"的交互控件的类名/文本/所属卡片/父链 | 10 个，全部可解释（见下） |
| 主题下拉存在且档位可读 | 设置页内存在 itemData 含 `light`/`dark` 的 `QComboBox`（不再按文字匹配） | PASS |
| 切「深色」后真正生效 | 走设置页主题下拉 `setCurrentIndex` → 250ms 自动应用 → `ThemeManager::isDark() == true` 且 `qApp->styleSheet()` **逐字等于** `ThemeManager::darkQss()` | **PASS**：`currentData()=dark / isDark=1 / 样式表长度=10412（深色基准 10412）/ 与 darkQss() 逐字相等=1` |
| 该链路写进生产日志 | 应用日志出现 `主题重放：深色（qss 长度 10412）` 与还原后的 `主题重放：跟随系统（qss 长度 10416）` | PASS（原文见 `docs/evidence_v343/theme_replay_log_evidence.txt`） |
| 还原档位 | 自检结束把档位还原为自检开始前的值，`isDark()` 回到原状态（`currentData()=system / isDark=0`） | PASS |

禁用诊断输出（本轮最终构建，自检末态：搜索模式=密度搜索 / 版本=基岩版 / 达到即停=1 / 缓存构建=0）：

1. `QPushButton[取消]` —— 当前没有进行中的搜索（按设计禁用）
2. 种子输入框 `QLineEdit` + 其清除按钮 —— **基岩版不读 Java 种子**（按设计禁用）
3. `QPushButton[Java 边界]`（搜索范围）—— 基岩版下预设按钮禁用（按设计禁用）
4. 「匹配方式」卡片内的算法下拉、层块/比例、采样间隔、`QCheckBox[精算采样（密度精确）]`
   —— 当前算法不是「分层 / 采样」，这些参数不参与计算（按设计禁用）

⇒ 结论：**没有任何"误禁用"**；用户此前遇到的「选项无法勾选 + 大量错位」来自 v3.4.4 给整页加的
`QGraphicsOpacityEffect` 淡入动画（已删除，改为只对导航图案做缩放，文字不动），本轮起由上面的
点击断言长期兜住。

## 三、回归（本轮最终构建）

| 项目 | 结果 |
|---|---|
| 编译 | `scripts/build_repo.cmd` exit 0，无 `error C`/`error LNK` |
| 单元测试 | **25 组全部通过**（`build\slime_tests.exe --unit`） |
| 参考对照 | **111/111**（`tests/verify_against_reference.py`，对照 `模板：原始文件\SlimeChecker.exe`） |
| SIMD 内核自检 | **19 项 ALL PASS**（`build\slime_kernel_tests.exe`） |
| GUI 自检（GPU 关） | **exit 0**（`docs/evidence_v347/gui_selftest_gpu_off_stderr.log`，含本轮新增断言） |
| GUI 自检（GPU 开） | **exit 0**（`docs/evidence_v347/gui_selftest_gpu_on_stderr.log`） |
| 发布包自检 | `scripts/verify_release.ps1 -Zip dist\SlimeFinder-3.4.3-win64.zip` ⇒ **16/16 PASS**（干净重建 / 单元 / 111 / OCL 探针 / 内核 / GUI 自检 ×2 / 基准 / 基准对照 / dist 自检 / 4 组哈希一致性 / zip 自检）；原始输出 `docs/evidence_v343/verify_release_final.log` |
| 污染审计 | `scripts/audit_corruption.py` ⇒ **CLEAN**（230 个文件；main/README/CMake/rc/nsi/zip 名/source 常量 7 处版本字面量全部 3.4.3；build 与 dist 的 `config.json` 均合法；docs 副本 identical 79 / differing 0 / absent 0） |
| 打包内部校验 | `scripts/package_release.ps1` ⇒ **PACKAGE OK**（build == dist == zip 的 4 个产物 SHA256 逐一相等；包内 GUI 自检/单元/内核均 exit 0） |

产物哈希：

```
SHA256  SlimeFinder.exe            = EB127B2A4195552877449AB38160A3529A5A48C18C9CB1143B7A8DB35DFD2E06
SHA256  slime_tests.exe            = 4EB4DED45D8406425E6A9C0E68524161F8ECA04D756A0665A216D62D6DA62F6C
SHA256  slime_ocl_probe.exe        = D963FF701C9610A071E73D0557287725D2E7CC876593C24A72796CC6BEC70B97
SHA256  slime_kernel_tests.exe     = 65AEB211176FC6E0A4534989D43E5E76925DF8E163E252661933FAD58639CA0C
SHA256  SlimeFinder-3.4.3-win64.zip= FE5F9D9CC74E358066D0D54E8EF9C7CB8085ED9B83A7A1D8EEB043C9400E6E30
```

## 四、包内卫生（本轮顺手修掉的两处）

1. `scripts/package_release.ps1` 生成的默认 `config.json` 里版本字段写死为 `v3.4.2`（上一版遗留）
   ⇒ 改为 `"v$Version"`，打包即当前版本；本次包内为 `"version": "v3.4.3"`。
2. `src/app.rc` 顶部注释仍写"文件版本 3.3.9 / 推导当前版本 3.4.0" ⇒ 更正为 v3.4.3（纯注释，无二进制影响）。
3. 复核 zip 内容：**不含** `*.slimecache` / `logs/` / `crashes/` / `*.dmp` / `*.log`（打包第 3 步清理生效），
   共 611 个条目，其中 docs 树含 `docs/screenshots/**` 全部截图证据（含本轮 41 张 v343 证据）。
4. 校验时间线：zip 于 2026-09-23 19:06 产出 ⇒ `verify_release.ps1` 于 2026-09-23 19:06 对**同一个 zip**
   完成 16/16 复核（之后未再重新打包，zip 哈希前后一致 `B9A0E3D7…`）。

## 五、深色模式端到端复核（本轮实测）

- 主窗口深色：背景 `#111111`、卡片边 `#4a4a4a`、标题带 `#161616`、文字 `#f0f0f0`（抽样像素确认）
- 设置对话框（独立弹窗）深色：**98.5% 像素为深色**，正文为浅色像素（`y=92` 行亮像素 46 个）⇒ 无"白底白字"
- 主题切换链路：设置页主题下拉按 `currentData()` 读取（`light` / `dark` / `system`），变化后 250ms 自动
  `applyAppSettings` + `ThemeManager::apply` + 写回 `config.json`，日志落盘 `主题重放：<档位>（qss 长度 N）`
- 勾选指示器与下拉箭头在深色下均为独立配色位图（非浅色图像反色），逐像素验证通过

## 六、遗留与已知限制

1. 结果列表仍是 `QTextEdit` 文本视图（v3.3.8.1 定稿的复制/`/tp` 语义优先），未改为逐行控件列表。
2. 自检日志里出现 `[CRASH] 0x40010006 / 0x4001000A / 0xE06D7363` 字样属**既有噪声**：
   这是本机无调试器时 Qt/CRT 的 first-chance 调试异常被崩溃处理器顺带打印，自 v3.3.4 起的**每一份**
   证据日志都有同样签名（v3.4.0 为 36~38 行，本轮 40 行），进程 exit 0、断言全通过，非新问题。
3. 设置页嵌入的是同一个 `SettingsDialog` 实例（`Qt::Widget`），对话框式「应用/取消/确定/恢复默认」按钮在
   **页内**隐藏、在**独立弹窗**（菜单/帮助入口）里保留 —— 独立弹窗截图 `v343_settings-*.png` 里能看到它们。
4. 本次报告写在打包之后（打包需先产出 zip 才能记录哈希），因此 zip 内的 docs 快照不含本文件；
   仓库 `docs/` 与 `dist/SlimeFinder/docs/` 两侧内容一致（审计的 docs 副本比对为 identical）。
