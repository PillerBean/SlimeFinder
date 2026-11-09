# v3.3.5 验证报告（Bug 修复 + 优化）

日期：2026-09-20 ｜ 基线：v3.3.4 ｜ 任务书：`模板：原始文件/v3.3.5 完整任务书（最终版）.txt`
机器：AMD Ryzen 5 5600（6C/12T，AVX2）+ AMD Radeon RX 6750 GRE 10GB
构建：MSVC 14.51 / Release `/O2 /openmp /GL /LTCG /utf-8 /Brepro`

> 任务书要求：做不完的明说、不降级。本报告 §一 逐项列出状态；**未完成项：无**。

---

## 一、验证清单（任务书 §四）

| 验证项 | 状态 | 证据 |
|---|---|---|
| Tab 补全不跳焦点，补全率 100% | ✔ | 自检：连续 10 次 Tab 焦点事件中**没有任何其他控件获得焦点**；`see`→`seed `（唯一匹配 100%）；多候选补到公共前缀；无匹配时内容与焦点均不变 |
| GPU 状态显示正确 | ✔ | 自检：`工具 → GPU 状态` 复用 `ensureGpuScanOnce()`，输出带 ✅/❌ 标记；可用时含 `MB`（显存）与 `CU`（计算单元），不可用时为「未找到 OpenCL 设备」 |
| 教程无游戏机制错误 | ✔ | 教程改为 Y=40 以下、无亮度限制、约 1/10 区块、蘑菇岛/深暗之域不生成、沼泽 Y51~69 亮度≤7 另有途径、Java 依赖种子而基岩版与世界种子无关（依据 zh.minecraft.wiki/w/史莱姆） |
| 检查更新弹窗先出现 | ✔ | 先弹窗（页面 + 密码 3o8i + [取消][复制链接][打开浏览器]），仅点「打开浏览器」才跳转；自检断言未点击时 `m_updateBrowserOpened == false` |
| 日志可关闭 | ✔ | 菜单项动态切换（自检断言文案与模式：`启用日志…`→`关闭日志`→Off→`启用日志…`）；关闭后日志栏恢复「日志未启用…」提示且不再写文件 |
| GPU 临界值修正 | ✔ | 采用**方案 A 实测基准**：后台测 CPU/GPU 两档规模、拟合固定开销+吞吐、解出交叉点并缓存 config.json（含设备名与核数）；同时显示实测吞吐率（方案 B 信息） |
| 进度条与按钮间隔紧凑 | ✔ | 进度标签页一行布局 `[开始搜索][取消] 进度:[bar] %阶段文字`，自检断言四个控件在同一 `QHBoxLayout` 且 `spacing()==8` |
| 教程有版本差异说明 | ✔ | 新增【为什么基岩版比 Java 版慢】：LCG ≈5 亿/秒 vs 完整 MT19937（624 步）≈3600 万/秒 + 基岩版建议开 GPU/缓存 |
| QSplitter handle 可见 | ✔ | 两个分割条 4 px、`#d0d0d0` 边框、hover `#0078d7`；自检断言 `handleWidth()==4` 且样式表含 `QSplitter::handle` |
| 查看菜单已精简 | ✔ | 自检断言 `m_menuView->actions().size()==1` 且为「恢复默认布局」 |
| 滚轮开关默认关闭 | ✔ | 自检：默认关闭；未聚焦滚轮不改数值、关闭时即使聚焦也无效、开启且聚焦时生效 |
| 程序设置界面可用，每个选项有描述 | ✔ | `SettingsDialog`：5 标签页（通用/搜索/缓存/日志/高级）每项含小字描述；[恢复默认][取消][应用]；自检断言标签页数量/顺序、出厂默认值、写入读回往返、未点应用时 `applied()==false` |
| 所有回归测试通过 | ✔ | 见 §三（单元 18 组 + 111/111 + GUI 开/关 + 内核 17/17 + OpenCL + dist/zip 哈希） |
| UI 截图更新 | ✔ | `docs/screenshots/v335/`（关键界面 + 程序设置对话框 + 教程对话框 + 三个规定尺寸） |

---

## 二、任务书 §六：13 个必须回答的问题

1. **Tab 补全是否拦截了默认焦点切换？**
   是。`CommandTerminal` 的输入框事件过滤器在 `Key_Tab/Key_Backtab` 分支里执行内联补全后
   `ke->accept(); return true;`，并显式 `setFocus()`；同时**删除了会抢焦点的 QCompleter 弹出候选窗**
   （`Qt::Popup` 会夺取键盘焦点，这正是跳焦点的根因）。自检连续 10 次 Tab，无任何其他控件获得焦点。
2. **GPU 状态检查是否复用了搜索路径的初始化？**
   是。`showGpuStatus()` 首先判断 `m_gpuProbeDone`，未探测则调用与勾选 GPU 完全相同的
   `ensureGpuScanOnce()`（同一条 `slimegpu::createGpuScanAdapter` 路径），再读取
   `GpuScan::deviceName()/globalMemBytes()/computeUnits()`。
3. **教程中游戏机制描述是否逐条核对？**
   是，按 `https://zh.minecraft.wiki/w/史莱姆` 核对：Y=40 以下生成、无亮度要求、约 1/10 区块、
   沼泽/红树林沼泽 Y51~69 且亮度 ≤7（满月概率最高）为另一条途径、蘑菇岛/深暗之域（基岩版含河流）
   不生成、Java 版由种子+区块坐标决定而基岩版与世界种子无关。原「任意亮度、任意高度」的错误描述已删除。
4. **检查更新是否改为「先弹窗后跳转」？**
   是。弹窗内容为下载页面 + 访问密码 `3o8i` + 「点击打开浏览器后…」说明，按钮
   `[取消] [复制链接] [打开浏览器]`；`QDesktopServices::openUrl()` 只在点击「打开浏览器」时调用。
5. **日志是否有「关闭」入口？**
   有。「工具」菜单项动态切换为「关闭日志」，点击后 `LogBus::setMode(Off)`（flush + 关闭文件句柄），
   日志面板恢复默认提示；自检验证了完整往返。
6. **GPU 临界值是否重新校准？还是改为显示实测吞吐率？**
   两者都做了：**主口径是方案 A 实测基准**（后台测量 + 拟合 + 交叉点 + 缓存到 config.json），
   界面同时显示**实测吞吐率**（方案 B 的信息），并对「GPU 吞吐低于 CPU」的情形明确提示
   「范围越大越不划算」——因此 36 亿区块这类范围不会再被错误建议开启 GPU。
7. **进度条与按钮间隔是否紧凑？**
   是。`进度` 标签页为单行紧凑布局，`QHBoxLayout::setSpacing(8)`，自检断言间距与同排控件。
8. **QSplitter handle 是否可见？**
   是。两个分割条统一设置为 4 px、浅灰底 + 边框，hover 变蓝（任务书给定样式），自检断言样式与宽度。
9. **查看菜单是否只剩「恢复默认布局」？**
   是（删除了「显示设置面板」「显示结果面板」），自检断言只剩 1 个动作。
10. **滚轮开关是否默认关闭？**
    是（`AppSettings::wheelAdjust = false`），并且「关闭时即使聚焦滚轮也无效」也做了断言。
11. **程序设置界面是否实现 5 个标签页？**
    是：通用 / 搜索 / 缓存 / 日志 / 高级，顺序与任务书一致，每项带小字描述；
    底部 `[恢复默认] [取消] [应用]`。
12. **主界面和设置界面的默认值是否双向同步？**
    是，共享同一份 `config.json` 的 `settings.*` 段：
    * 设置界面点「应用」→ 立即写入并**立即生效**（滚轮开关、更新提醒天数、日志模式、缓存目录迁移、默认值）；
    * 主界面修改搜索选项（模式/线程/目标数量/及时反馈）→ 退出时由 `closeEvent()` 写回为下次默认值；
    * 「恢复默认」→ 出厂值（目标数量 1、提醒 30 天、滚轮关闭、线程自动…）。
13. **每个设置项是否有简短描述？**
    是。所有选项（含占位项）下方都有 8pt 灰色小字说明其作用；缓存/日志页还各有操作按钮。

---

## 三、回归测试

见 §四（一键脚本 14 步全绿）。

---

## 四、最终实测记录与哈希

本版本共经过 **4 轮** 一键脚本（`scripts\verify_release.ps1`）实测；第 3、4 轮在**污染审计修复**
（见 §五）之后执行，第 4 轮为重新打包后的最终复验。

| 轮次 | 场景 | 结果 | 日志目录（`%TEMP%` 下） |
|---|---|---|---|
| 1 | 审计前 · 干净全量重建 + 全部检查 | `total=14 pass=13 fail=1`（唯一 FAIL 为尚未打包的 `hash build==dist`，预期） | `slime_verify_20260920_201828` |
| 2 | 审计前 · 打包后复验 | `total=14 pass=14 fail=0` | `slime_verify_20260920_202159` |
| 3 | 审计修复后 · 干净全量重建（打包前） | `total=16 pass=13 fail=3`（3 个 FAIL 全是「源码已改、尚未重新打包」的哈希差，预期） | `slime_verify_20260920_204758` |
| 4 | 审计修复后 · 重新打包 + 最终复验 | **`total=16 pass=16 fail=0`**（exit 0） | `slime_verify_20260920_205205` |
| 5 | 文档与证据定稿后 · 交付包复验（第 3 次打包产物） | **`total=16 pass=16 fail=0`**（exit 0） | `slime_verify_20260920_205821` |

> 第 1~5 轮的二进制哈希完全一致（`/Brepro` 确定性构建），因此第 4、5 轮给出的哈希对最终交付包同样成立。
> 文档定稿后的最终交付包（第 4 次打包）由打包脚本自身的第 5 步完成「build == zip + 版本串 + VERSIONINFO +
> 包内 GUI 自检 + 包内单元测试（组 15）+ 包内内核自检」校验，并再跑一次
> `verify_release.ps1 -SkipClean -SkipBench -Zip dist\SlimeFinder-3.3.5-win64.zip` 全量复验（日志目录见交付说明）。

第 4 轮逐步结果（脚本原始输出）：

```
[PASS] clean rebuild            exit=0, errorLines=0
[PASS] unit tests               exit=0, 单元测试全部通过 ✔（18 组）
[PASS] reference 111            exit=0（通过 111 项，失败 0 项）
[PASS] ocl probe                exit=0, === 结果: PASS (失败 0 项) ===
[PASS] SIMD kernel self-test    exit=0, RESULT: ALL PASS（含嵌入内核字节一致性）
[PASS] gui selftest (gpu=off)   exit=0（[UI自检] 全部通过 ✔）
[PASS] gui selftest (gpu=on)    exit=0（[UI自检] 全部通过 ✔）
[PASS] benchmark                exit=0
[PASS] benchmark (v3.2.1 基线)  exit=0
[PASS] dist self-test           exit=0
[PASS] hash build==dist         6B86FF76…（4 个二进制逐个比对）
[PASS] hash zip==dist           6B86FF76…（zip 内 exe 与 dist 一致）
[PASS] zip self-test            exit=0
```

**打包**（`scripts\package_release.ps1 -Version 3.3.5`）：`build == dist == zip` 四个二进制一致；
exe 版本串 `v3.3.5` 存在；**VERSIONINFO FileVersion=3.3.5.0 / ProductVersion=3.3.5**；
**包内 GUI 自检 exit 0**（新目录、默认 config.json 与 build_date.txt）、包内单元测试（组 15）exit 0、
包内内核自检 exit 0。

**v3.3.5 关键实测（第 4 轮 GUI 自检输出，证据 `docs/gui_selftest_v335_gpu_on_stderr.txt`）**：

```
[UI自检] v3.3.5 检查（Tab 焦点/GPU 状态/日志开关/更新弹窗/基准/滚轮/设置/菜单/布局）…
[UI自检] Tab×10：焦点事件 0 次，跑到其他控件=无
[UI自检] GPU 检测结果: 可用（）
[UI自检] GPU 端到端搜索: ok=1 结果=10
[UI自检] GPU=gfx1031（Advanced Micro Devices, Inc.） 显存=10224 MB 核数=12 → 临界值=1002352 区块
[UI自检] 小范围提示触发: total=16 状态栏="当前范围较小（16 区块，低于本机临界值约 100 万），建议关闭 GPU 加速以获得更快速度。"
[UI自检] 缓存端到端：文件=…\build\slime_cache.slimecache 条目=6811 重载条目=6811 逐区块比对=65536 不一致=0
[UI自检] 取消响应(立即): 调用 1.0 ms，停止 23.7 ms
[UI自检] 取消响应(运行中=1): 停止 20.1 ms（若为 0 表示搜索已先完成，属正常）
[UI自检] 阶段文字: [正在扫描区块… 42% ｜ 速度 12.3 万/秒 ｜ 预计剩余 1:05] [扫描完成，正在匹配图案…] [正在匹配图案… 77%] [正在排序结果…]
[UI自检] v3.3.5 检查完成
```

**GPU 判定扫描实测（第 4 轮）**：

```
[UI自检] GPU 检测结果: 可用（）                    ← 括号内为「不可用原因」字段，仅失败时填充
[UI自检] 搜索冒烟 GPU 状态: 勾选=1 可用=1
[UI自检] GPU 端到端搜索: ok=1 结果=10
[UI自检] GPU=gfx1031（Advanced Micro Devices, Inc.） 显存=10224 MB 核数=12 → 临界值=1002352 区块
[UI自检] 小范围提示触发: total=16 状态栏="当前范围较小（16 区块，低于本机临界值约 100 万），建议关闭 GPU 加速以获得更快速度。"
OpenCL: gfx1031 (Advanced Micro Devices, Inc.) | 平台: AMD Accelerated Parallel Processing
        | OpenCL 2.0 AMD-APP (3652.0) | 驱动 3652.0 (PAL,LC) | CU: 18 | 显存 10224 MB | 单次分配上限 8472 MB
```

说明：`核数=12` 是 **CPU 逻辑核数**（临界值回退公式 `显存(MB)×1000÷(核数×0.85)` 的自变量），
与 OpenCL 探针报告的 GPU `CU: 18` 是两个不同的量——两者均非缺陷（审计 §五 已逐项核对）。

**交付哈希（SHA256，第 4 轮最终构建）**

| 文件 | SHA256 |
|---|---|
| `build\SlimeFinder.exe` = dist = zip | `6B86FF768E0DF9960350B3C34BB9A62E174F6FF1AB8F15CBBD8322D9725AF7DD` |
| `build\slime_tests.exe` = dist = zip | `E19A898DC566124967A285AB1FCD1DAE21DA1EF07FCFB65E7B010543CDAD89B3` |
| `build\slime_ocl_probe.exe` = dist = zip | `043BFD16800B7EC8301ACBB6A6BE213A12FA68A79259AE6B4F2C80FC36616BCF` |
| `build\slime_kernel_tests.exe` = dist = zip | `65AEB211176FC6E0A4534989D43E5E76925DF8E163E252661933FAD58639CA0C` |
| `dist\SlimeFinder-3.3.5-win64.zip` | 见 `dist/SHA256SUMS-3.3.5.txt`（交付以该文件为准） |

**UI 截图**：`docs/screenshots/v335/`（27 张）——三个规定尺寸（1280×860 / 1000×650 / 1920×1080）、
splitter 最左/最右/底部最高、四个标签页、五个菜单展开、程序设置五个标签页、教程对话框、缓存导入对话框等；
本轮追加 PNG 结构校验（签名 + IHDR 宽高 + IEND）：64 张截图全部完整、0 张截断。

**证据文件**（`docs/`，全部为 UTF-8）：`unit_test_v335.txt`、`verify111_v335.txt`、`ocl_probe_v335.txt`、
`kernel_selftest_v335.txt`、`bench_v335.txt`、`gui_selftest_v335_gpu_off.txt`、
`gui_selftest_v335_gpu_on.txt`、`gui_selftest_v335_gpu_on_stderr.txt`、`pkgverify_selftest_v335.txt`、
`corruption_audit_v335.txt`。
导出方式：`powershell -File scripts\export_evidence.ps1 -LogDir <上表日志目录> -Tag v335`
（按 BOM 解码日志 → 统一写成带 BOM 的 UTF-8，并统计 `U+FFFD`/NUL，任一非零即退出码 1）。

---

## 五、污染审计（v3.3.5 追加）

按交付要求对全树做了「字符替换污染」审计（`scripts/audit_corruption.py`，7 类检查 + 灵敏度自测），
完整报告见 **`docs/corruption_audit_report.md`**，证据见 **`docs/corruption_audit_v335.txt`**。结论：

* **代码字符污染 4 处**（全部在 `src/gui/MainWindow.cpp` 的自检代码段，均为事故 A 的残留，
  均不影响功能）：`oaoheWasEnabled`→`cacheWasEnabled`（2 处）、`gpuWasCheoked`→`gpuWasChecked`（2 处）、
  `CacheManager o4`→`c4`（2 处）、字符串 `"slime_selftest_notoaohe.slimecache"`→`"…notcache…"`（1 处）。已逐处修复。
* **命名异常 3 处**（无法证明为字符污染，但与本文件惯例不符，已规范化）：`leo`→`logEc`、
  `eok`→`estOk`、`to0/to1`→`tCancel0/tCancel1`。
* **关键字符串常量全部完好**：下载页 URL、访问密码 `3o8i`、缓存/配置/日志/崩溃路径、OpenCL 内核入口、
  版本号 `v3.3.5` / `3.3.5` 全部命中且与打包副本一致（65 个 docs 文件逐字节一致）。
* 修复后审计结果为 **CLEAN**（退出码 0）；同一工具在注入合成样本时能稳定报出全部 4 类损坏（灵敏度自测）。

---

## 六、证据编码统一（v3.3.5 追加）

审计（第 [7] 类检查）发现**诊断输出与采集端编码不一致**，导致部分**交付证据文件**出现乱码
（历史版本证据共 18 处缺陷：`verify111_v33x.txt` 每份 481~503 个 `U+FFFD`、stderr 捕获每份 303~449 个、
内核自检每份 1 个、`verification_report_v335.md` 3 个 NUL 字节）。根因与修复：

| 环节 | 根因 | 修复 |
|---|---|---|
| Python 对照脚本 | `print()` 在重定向下按 ANSI(GBK) 输出，被按 UTF-8 解码 | `sys.stdout/stderr.reconfigure(encoding="utf-8")` |
| GUI 自检 | `QString::toLocal8Bit()`（GBK）与同文件 UTF-8 字面量混排 | 22 处改为 `toUtf8()`；缓存路径改用 `u8string()` |
| 崩溃/异常报告 | `GetModuleFileNameA`（ANSI 路径） | 改为宽字符 + `WideCharToMultiByte(CP_UTF8)`（`main.cpp`、`ocl_probe.cpp`） |
| 内核自检 | 按字节截断 70 字节，切断多字节 UTF-8 字符 | 截断后退回 UTF-8 字符边界 |
| 采集端 | PowerShell 5.1 按 ANSI 解码子进程 stdout | `verify_release.ps1` 固定 `[Console]::OutputEncoding`/`$OutputEncoding`，并设 `PYTHONUTF8=1`、`PYTHONIOENCODING=utf-8` |
| 证据落地 | 直接复制日志（UTF-16 或已乱码） | 新增 `scripts/export_evidence.ps1`：按 BOM 解码 → 统一 UTF-8，并统计 `U+FFFD`/NUL |

修复后同一轮实测：`reference111.log` 503→0、stderr 捕获 449→0、内核自检 1→0，**全部证据文件 0 缺陷**。
历史证据（v3.3.0~v3.3.4）因原始字节已被替换字符覆盖、无法无损还原，已移入
`docs/evidence_legacy/`（附 `README.md` 说明）并作为**已登记例外**排除在编码门禁之外；
v3.3.5 的证据全部重新生成，编码体检为 0。

---

## 七、实施记录与更正

* `SettingsDialog`（程序设置，五标签页 + `AppSettings`）、`FeedbackPanel`、`LogPanel`/`LogBus`、
  `CommandTerminal` 由子代理独立实现并按契约接入；其余修复/优化与全部集成、回归、打包由本轮完成。
* 过程中发现并修复了两处**历史遗留的工具链污染**（非本轮功能代码）：`install.nsi` 中所有 `!`
  曾被误替换为 `d`（NSIS 命令前缀失效），已逐条还原并同步版本号。
* **更正上一版报告的说法**：上一版 §五 曾写「主窗口此前被误替换的字符已修复，本轮复核无残留」，
  该表述不准确——本轮专项审计在 `MainWindow.cpp` 中**仍发现 4 处残留**（见 §五）。原因是被一致改名的
  局部标识符不会引起编译错误，词典式修复也难以判定。现已全部修复，并新增
  `scripts/audit_corruption.py` 作为每次批量文本改动后的必跑门禁。
