# v3.4.3 UI 修正轮（R3～R7）进度与交付记录

> 前置：任务书《SlimeFinder v3.4.1 任务书：UI 视觉自优化循环》的自驱循环已在 R1/R2 收敛
> （见 `docs/v341_progress.md`、`docs/v341_diff_round1.md`、`docs/v341_diff_round2.md`）。
> 此后进入**用户实测反馈驱动**的修正轮（R3～R7），每轮都以「用户描述的现象 → 代码级根因 → 最小修复 →
> 全量回归」闭环，只动 QSS / 布局 / 图标 / 自检，不动功能逻辑。

## 轮次总览

| 轮 | 版本标号 | 触发 | 修复要点 | 证据目录 |
|---|---|---|---|---|
| R1 | v3.4.4 | 循环第 1 轮截图差异清单 | 窄窗叠字（参数栏 320 最小宽 / 结果栏 380 最小宽 / 卡片间距 10） | `docs/screenshots/v341_round1/` |
| R2 | — | 循环第 2 轮复比 | 无新 P0/P1 ⇒ 收敛（P0=0 / P1=0 / P2=3 可接受） | `docs/screenshots/v341_round2/` |
| R3 | v3.4.3 | 用户实测：深色模式、设置不持久、历史列表、构建入口 | 设置页改动 250ms 立即生效并写 `config.json`；更新历史去索引列 + 排序按钮；构建内容并入搜索页「额外选项」；导航三项 | `docs/evidence_v343/` |
| R4 | v3.4.4/5 | 用户实测：**选项无法勾选 + 大量错位** | 定位到整页 `QGraphicsOpacityEffect` 淡入动画 ⇒ **删除**（连带删掉不再使用的 `QPropertyAnimation` 头文件）；错位随动画一起消失 | `docs/evidence_v344/`、`docs/evidence_v345/` |
| R5 | — | 用户实测：失去对比度 / 缺黑边 | 全局 `QWidget` 背景规则把容器底色刷成"白盖白" ⇒ 重写 `ThemeManager.cpp`：全局 `QWidget` 只给 `color`/`font`，**背景只给容器与输入/文本/列表类控件** | `docs/evidence_v346/` |
| R6 | v3.4.6 | 用户实测：主题日志总是「跟随系统」、动画带动文字、箭头黑块 | 主题档位改读设置页 `QComboBox::currentData()`（原来按单选按钮**文字**匹配，控件早已改成下拉）；导航动画改为**只缩放图案**、文字与布局不动；删掉自定义 `::up-arrow/::down-arrow` 规则 | `docs/evidence_v347/` |
| R7 | v3.4.7 | 用户实测：小箭头/黑块、勾选看不出勾上、下拉空灰格 | 见下（本轮三项像素级修复 + 新增交互可用性断言） | `docs/screenshots/v343/`、`docs/evidence_v347/` |

## R7 详情（本轮）

1. **数字输入框的"横杠黑块"**：QSS 给 `QAbstractSpinBox` 上了边框/底色却没有 `::up-button/::down-button` 规则，
   原生 spinner 箭头被画成两根横杠 + 一根竖线。⇒ 上下按钮与箭头全部归零（`width/height:0`、`border:0`、`image:none`），
   与样式规范一致。验证：输入框右边框 `x=238`，右侧整行无任何非白像素。
2. **下拉框只剩一个空灰格**：只有 `::drop-down` 的灰底 + 分隔线，没有箭头图像。⇒ 去掉灰底/分隔线，
   自绘 7×4 实心直角三角（浅/深 × 正常/禁用共 4 张位图，进 `assets/appicon.qrc`）。验证：箭头像素为标准倒三角
   （`y=220` 宽 7 → `y=223` 宽 1）。
3. **勾选态"看着没勾上"**：原 `:checked` 用 `border:4px solid #ffffff` + `outline:1px solid #3D8B40`，
   而 Qt QSS **不支持 `outline`** ⇒ 白框融进白底，只剩 14px 绿块。⇒ 按样式规范「复选 · 直角方形」重做为
   **1px 边框 + 内缩 3px 实心块**（16×16 位图；浅色=灰边+黑块，深色=深灰边+浅块，禁用态另配色）。
   验证：浅色 `#c9c9c9 / 白 3px / #111111 8px / 白 3px / #c9c9c9`；深色 `#4a4a4a / #111111 / #f0f0f0`。
4. **新增自检第 18 组：交互可用性**
   - 对设置页 + 当前可见页面的每个可见且可用复选框，用 `QStyle::SE_CheckBoxIndicator` 取指示器矩形中心，
     发送真实 `QMouseEvent`（按下 + 抬起），断言 `isChecked()` 翻转后还原 ⇒ 本轮结果「可点 5 个，未翻转 0 个」；
   - 打印"可见但被禁用"的交互控件（类名/文本/所属卡片/父链/原因），本轮 10 个全部为设计性禁用
     （取消按钮无搜索 / 基岩版不读 Java 种子与预设 / 当前算法不用分层·采样参数）。
5. **包内卫生**：`package_release.ps1` 生成的默认 `config.json` 版本字段由写死的 `v3.4.2` 改为 `v$Version`；
   `src/app.rc` 顶部注释更正为 v3.4.3（纯注释）。

## 交付清单（本轮最终）

| 项 | 值 |
|---|---|
| 源码 | `src/`（`ThemeManager.cpp` 547 行、`MainWindow.cpp` 8373 行）、`assets/appicon.qrc` + 8 张新位图 |
| 发布包 | `dist/SlimeFinder-3.4.3-win64.zip`（43,208,758 B，SHA256 `FE5F9D9C…400E6E30`） |
| 任务书等价命名 | `dist/SlimeFinder-3.4.1-win64.zip`（同一字节，SHA256 一致）+ `dist/SHA256SUMS-3.4.1.txt`（内含说明） |
| 回滚包 | `dist/SlimeFinder-3.4.0-rollback-win64.zip`（= v3.4.0 包副本，SHA256 `2CB87964…007C1B4D`） |
| 校验 | `dist/SHA256SUMS-3.4.3.txt`；`scripts/verify_release.ps1` ⇒ **16/16 PASS** |
| 截图 | `docs/screenshots/v343/`（39 张界面 + 2 张 R6→R7 前后对比） |
| 日志 | `docs/evidence_v347/gui_selftest_gpu_{off,on}_stderr.log`（最终构建原始自检日志） |
| 报告 | `docs/verification_report_v343.md` |

## 回归结论（本轮最终构建）

- 编译 exit 0（无 error C/LNK）；单元 **25 组**通过；参考对照 **111/111**；SIMD 内核 **19 项 ALL PASS**
- GUI 自检 **GPU 关 exit 0 / GPU 开 exit 0**（含新增的复选框点击断言与禁用诊断）
- 发布包 **verify_release 16/16 PASS**（docs/evidence_v343/verify_release_final.log）、**PACKAGE OK**、污染审计 **CLEAN（231 文件）**
- 深色模式端到端复核：主窗口/对话框均为深色底 + 浅色字（对话框深色像素占比 98.5%），
  勾选指示器与下拉箭头在深色下使用独立配色位图

## 遗留（未做，需用户确认后再动）

1. 结果区仍是 `QTextEdit` 文本视图（v3.3.8.1 定稿语义），未改逐行控件列表。
2. 自检日志中的 `[CRASH] 0x40010006/0x4001000A/0xE06D7363` 为既有噪声（自 v3.3.4 起每份证据日志同签名），
   进程 exit 0，非回归。
3. 版本号沿用 3.4.3（R1/R2 报告的 3.4.1 包名以等价命名副本提供）。
