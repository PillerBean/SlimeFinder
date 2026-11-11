# v3.3.4 验证报告（界面重构 + 底部四标签页 + 日志/终端 + 更新提醒）

日期：2026-09-20 ｜ 基线：v3.3.3 ｜ 任务书：`模板：原始文件/v3.3.4 完整任务书（最终版）.txt`
机器：AMD Ryzen 5 5600（6C/12T，AVX2）+ AMD Radeon RX 6750 GRE 10GB
构建：MSVC 14.51 / Release `/O2 /openmp /GL /LTCG /utf-8 /Brepro`

> 本报告按任务书 §7 要求，**逐项列出已完成 / 未完成及原因**，不做降级实现。

---

## 一、交付清单与完成状态（任务书 §6/§7）

| 任务 | 状态 | 说明 |
|---|---|---|
| 任务 1：UI 整体重构（P0） | **完成** | 菜单栏/工具栏/状态栏原生 + 中部双 Splitter + 左 6 分组 + 右信息栏/QTextEdit + 底部 4 标签页；尺寸规范全部满足 |
| 任务 2.1 进度标签页（P0） | **完成** | 进度条 + 阶段文字 + 已用时 + 预计剩余（200 ms 刷新） |
| 任务 2.2 反馈标签页（P1） | **完成** | `QTreeWidget` 四级分组（错误>警告>信息>成功）、时间倒序、可折叠、计数、过滤按钮 + 右键菜单、严重错误弹窗并自动切换、空组隐藏 |
| 任务 2.3 日志标签页（P1） | **完成** | 默认关闭（仅一行提示）、隐蔽入口「工具 → 启用日志…」、两级选项（详细带降速警告）、级别过滤/自动滚动/清空/复制全部、内存 10000 条、500 ms 节流、`logs/performance-YYYY-MM-DD.log`、关闭不写文件 |
| 任务 2.4 命令终端（P1） | **完成** | 15 条命令、命令插队（GUI 线程同步执行，不等待搜索线程）、↑/↓ 历史（持久化 config.json 100 条）、Tab 补全、无系统命令 |
| 任务 3 更新提醒（P2） | **完成** | `build_date.txt`（单独文件，打包脚本写入）、>30 天显示徽章、点击打开下载页并提示访问密码、帮助菜单含检查更新/更新历史 |
| 任务 4 默认值修改（P0） | **完成** | 种子默认空 + Java 版未填/非法警告（[去填写]/[取消] + 反馈栏 ❌ + 日志 + 高亮闪烁；`0` 合法、空格视为空、`123abc` 非法、基岩版不受影响）；目标数量默认 1 |
| 任务 5 v3.3.3 遗留验证（P1） | **完成** | 见 §三（版本号、缓存标签、GPU 临界值、分阶段进度、帮助 7 章节） |
| 任务 6 回归测试（P0） | **完成** | 见 §四（clean 重建 / 单元 18 / 111 / GUI 开-关 / 内核 17 / OpenCL / dist+zip 哈希） |
| 任务 7 UI 截图验证（P0） | **完成** | 18 张截图（3 个规定尺寸 + splitter 最左/最右 + 底部最高 + 4 标签页 + 5 个菜单展开 + 模式/算法/对话框），见 §五 |

**未完成任务**：无。
**说明（非降级）**：任务书 §2.4 的「最小命令集（15 条）」原文把 `version` 列了两次，
实际去重后为 14 条；本实现按 15 条交付：上述 14 条 + `log`（日志模式查看/切换），
`clear` 由终端内置处理（不经过主窗口）。命令总数与名称可用 `help` 或自检核对。

---

## 二、任务书 §8 的 11 个问题（逐条回答）

1. **菜单栏、工具栏、状态栏是否全部用 QMainWindow 原生接口？**
   是。`menuBar()` / `addToolBar()` / `statusBar()`；自检断言 `menuBar()->actions().size() >= 5`、
   工具栏存在且含动作、状态栏存在。
2. **中间区域是否用 QSplitter？左侧是否用 QScrollArea？**
   是。垂直 `QSplitter`（上：水平 `QSplitter`；下：`QTabWidget`）；水平 `QSplitter` 左侧为
   `QWidget → QScrollArea → 6 个 QGroupBox`。自检断言两个 Splitter 各含 2 个面板、
   `mainScroll->widget() != nullptr`。
3. **底部是否用 QTabWidget？**
   是，4 个标签页，名称与顺序为 进度 / 反馈 / 日志 / 命令终端（自检逐项断言）。
4. **反馈栏是否用 QTreeWidget？是否按级别分组排序？**
   是。`QTreeWidget` + 4 个固定顶级分组（错误 > 警告 > 信息 > 成功），组内插入到最前（时间倒序），
   组标题实时计数、空组隐藏、可按级别过滤。
5. **日志默认是否关闭？隐蔽入口在哪？2 级选项是否实现？**
   默认关闭（`LogBus::mode() == Off`，不写文件、不分配条目），面板只显示一行提示
   「日志未启用。如需启用，请到 [工具] 菜单 → [启用日志]」；入口为**菜单栏「工具 → 启用日志…」**
   与日志面板内按钮；两级选项（简要 / 详细，详细带「可能降低搜索速度 10~30%」警告）已实现，
   默认选中「简要」，取消不改状态。
6. **命令终端是否实现命令插队？**
   是。命令解析在控件内、执行同步调用宿主（GUI 线程），与搜索线程（`SearchTask` 的 `std::thread`）
   完全独立 → 设置类命令立即生效、查询类立即返回、`cancel` 立即置位取消标志。
   自检在**搜索运行中**执行 `status` / `cancel` 并断言立即返回与快速停止。
7. **发布日期是否用单独文件存储？**
   是：`build_date.txt`（与 exe 同目录，内容 `YYYY-MM-DD`），由 `scripts\package_release.ps1`
   打包时写入；缺失/非法时退回内置默认日期（并记一条日志）。
8. **种子默认是否为空？Java 版未填是否警告？**
   是（默认空）。Java 版未填 → 弹窗「未填写世界种子。Java 版判定依赖种子，未填写无法搜索。」
   [去填写] / [取消]；[去填写] 触发输入框高亮闪烁；同时反馈栏记录 ❌ 错误与日志。
9. **目标数量默认是否为 1？**
   是（`QSpinBox` 默认 1，config.json `search.targetCount` 默认 1）。
10. **是否提供了 8 张截图？**
    提供了 18 张（含任务书要求的全部 8 类），见 §五。
11. **是否有未完成的任务？原因是什么？**
    没有未完成任务。唯一与任务书字面不一致处：§2.4 列出的 15 条命令中有重复项，
    已按 14 条 + `log` 补足 15 条（见 §一说明）。

---

## 三、任务 5：v3.3.3 遗留项在 v3.3.4 上的验证

| 项 | 结果 | 证据（GUI 自检输出） |
|---|---|---|
| 版本号统一 | ✔ | 应用版本常量 `v3.3.4`、窗口标题含 v3.3.4、`QApplication::applicationVersion()==3.3.4`、`config.json` 写入 `"version": "v3.3.4"`；**exe 属性 → 详细信息：文件版本 3.3.4.0 / 产品版本 3.3.4**（打包脚本断言） |
| 缓存状态标签 | ✔ | 「缓存已加载，共 N 个区块（覆盖 R 个区域，约 X MB）」；端到端 6811 条目、65536 区块逐块比对不一致 0 |
| GPU 临界值提示 | ✔ | 勾选后显示「本机估算临界值约 100 万区块」；极小范围搜索时状态栏提示「建议关闭 GPU 加速」（命中分支已断言） |
| 进度分阶段显示 | ✔ | 四段文字映射断言通过：`正在扫描区块… X%` / `扫描完成，正在匹配图案…` / `正在匹配图案… X%` / `正在排序结果…` |
| 帮助（7 章节） | ✔ | `helpTutorialHtml()` 包含全部 7 个章节标题，帮助按钮存在可用，F1 打开 |

---

## 四、回归测试（任务 6）

见 §六（一键脚本 14 步全绿）与 `docs/verify_summary_v334.txt`：

| 项目 | 结果 |
|---|---|
| clean 全量重建 | exit 0，`error` 行 0 |
| 单元测试 18 组 | 全部通过 |
| 111/111 参考对照 | 111 通过 / 0 失败 |
| GUI 自检（GPU 关 / 开） | exit 0 / exit 0 |
| 内核自检 | 17 项检查 0 失败 |
| OpenCL 探测 | PASS（失败 0 项） |
| dist / zip 哈希一致 | 4 个二进制 + 解压包全部一致 |

---

## 五、UI 截图验证（任务 7）

截图由 GUI 自检在 `SLIME_SHOT` 下生成（`docs/screenshots/v334/`），每张截图前都断言
**设置区横向滚动条为 0**（任何错位即失败）：

| 类别 | 文件 | 结果 |
|---|---|---|
| 默认尺寸 1280×860 | `ui_size-1280x860.png` | 横滚动 0 |
| 最小尺寸 1000×650 | `ui_size-1000x650.png` | 横滚动 0 |
| 最大尺寸 1920×1080 | `ui_size-1920x1080.png` | 横滚动 0 |
| 水平 splitter 拖到最左（左栏 340） | `ui_splitter-left.png` | 横滚动 0（另有断言） |
| 水平 splitter 拖到最右 | `ui_splitter-right.png` | 横滚动 0 |
| 垂直 splitter 拖到底部最高 | `ui_splitter-bottom-max.png` | 横滚动 0 |
| 标签页：进度 / 反馈 / 日志 / 命令终端 | `ui_tab-0-进度.png` … `ui_tab-3-命令终端.png` | 4 张，横滚动 0 |
| 菜单展开：文件 / 编辑 / 查看 / 工具 / 帮助 | `ui_menu-file.png` … `ui_menu-help.png` | 5 张 |
| 附加：模式/算法联动、缓存导入对话框 | `ui_mode-*.png`、`ui_cache-import-dialog.png` | — |

---

## 六、最终实测记录与哈希

**第 1 轮（clean 全量重建 + 全部检查，日志目录 `%TEMP%\slime_verify_20260920_190755`）**

```
total=14 pass=12 fail=2
[PASS] clean rebuild            exit=0, errorLines=0
[PASS] unit tests               exit=0, 单元测试全部通过 ✔
[PASS] reference 111            exit=0（111 通过 / 0 失败）
[PASS] ocl probe                exit=0, === 结果: PASS (失败 0 项) ===
[PASS] SIMD kernel self-test    exit=0, RESULT: ALL PASS（17 项检查 0 失败）
[PASS] gui selftest (gpu=off)   exit=0
[PASS] gui selftest (gpu=on)    exit=0
[PASS] benchmark                exit=0（含 v3.2.1 基线）
[FAIL] dist self-test           exit=1（**预期**：dist 内仍是「测试用例修复前」的 v3.3.4 预览构建，
                                其 GUI 自检因「种子默认空」的新用例失败；重新打包后转 PASS）
[FAIL] hash build==dist         （**预期**：尚未打包为 v3.3.4）
```

**第 2 轮（打包后复验，日志目录 `%TEMP%\slime_verify_20260920_191128`）**

```
total=14 pass=14 fail=0
[PASS] 全部 14 步（含 dist 自检 exit 0、zip 自检 exit 0、4 个二进制与解压包哈希一致）
```

**打包**（`scripts\package_release.ps1 -Version 3.3.4`）：
`build == dist == zip` 四个二进制全部一致；exe 版本串 `v3.3.4` 存在；
**VERSIONINFO FileVersion=3.3.4.0 / ProductVersion=3.3.4**；
**包内 GUI 自检 exit 0**（新目录、仅默认 config.json 与 build_date.txt）、
包内单元测试组 15 exit 0、包内内核自检 exit 0；`build_date.txt` 由打包脚本写入（ISO 日期）。

**GPU 判定扫描实测（本次构建）**：Java 16384²（2.68 亿区块）**26.9 ms**（99.8 亿/秒，35.37× CPU 单核、
6.26× CPU12）；Java 1000² 0.3 ms；基岩 2048² 132.3 ms（31.7 M/s，38.26× 单核）。一致性：全部矩形
`mismatch=0`，引擎级密度搜索「一致=是」。

**交付哈希（SHA256，最终构建）**

| 文件 | SHA256 |
|---|---|
| `build\SlimeFinder.exe` = dist = zip | `D2BF16F2972D6CFA0C6C0BC503C3E485D7341143794A1A6DB4FF338FF999444F` |
| `build\slime_tests.exe` = dist = zip | `E19A898DC566124967A285AB1FCD1DAE21DA1EF07FCFB65E7B010543CDAD89B3` |
| `build\slime_ocl_probe.exe` = dist = zip | `C951C3F215A9D0290CD6F12019D33CADD3C39F0AEF6D5826D761D68884EE1CD3` |
| `build\slime_kernel_tests.exe` = dist = zip | `E3F743B4F4D1C07DEFBB329559D3F3D8D0200F50425F4745EF6BDB0362EEB662` |
| `dist\SlimeFinder-3.3.4-win64.zip` | 见 `dist/SHA256SUMS-3.3.4.txt`（交付以该文件为准） |

**证据文件**（`docs/`）：`unit_test_v334.txt`、`verify111_v334.txt`、`ocl_probe_v334.txt`、
`kernel_selftest_v334.txt`、`bench_v334.txt`、`gui_selftest_v334_gpu_off.txt`、
`gui_selftest_v334_gpu_on.txt`、`gui_selftest_v334_gpu_on_stderr.txt`（自检全量输出，
含结构断言/布局断言/命令插队/日志/反馈/更新提醒等 v3.3.4 新增项的实测记录）。

> 过程说明（诚实记录）：重构期间主窗口源文件曾被一次错误的 PowerShell 文本处理
> 全局替换了字符（小写 `c` → `o`）。由于该替换长度不变，已用「词典驱动的逆向修复」
> （词表来自本项目全部完好源码、头文件、文档与 Qt 头文件）逐词还原，并用编译器错误列表
> 复核；此后自检、单元测试、111/111、OpenCL 探测与性能基准全部通过，功能无缺失。

---

## 七、实施记录（过程说明，供追溯）

* 三个自包含模块由子代理独立实现并按契约接入：`FeedbackPanel`（反馈分级面板）、
  `LogBus`+`LogPanel`（日志总线与面板）、`CommandTerminal`（命令终端与 `CommandHost` 接口）；
  主窗口负责结构重构、四个标签页装配、更新提醒、默认值/校验与全部集成。
* 重构过程中发现 v3.3.3 的折叠实现（搬迁布局条目）与任务书 1.4 的「不做折叠、不搬移布局条目」
  直接冲突，已按要求删除该机制。
* 为满足「任意规定尺寸/分割位置都不出现错位」，将范围/算法等长行拆分为多行并压缩控件最小宽度，
  使 6 个分组的最小宽度从 444~504 px 降到 200~294 px（340 px 左栏也不出横向滚动条）。
