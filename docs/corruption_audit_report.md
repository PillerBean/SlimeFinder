# 源码污染审计报告（v3.3.5 交付版）

> 审计对象：`SlimeFinder/` 全树（源码、脚本、测试、文档、配置）
> 审计工具：`scripts/audit_corruption.py`（可重复执行，退出码即门禁）
> 证据文件：`docs/corruption_audit_v335.txt`
> 审计日期：2026-09-20

---

## 一、审计背景

项目历史上有过两次「单字符替换」事故，均源于同一个 PowerShell 陷阱：辅助函数收到
`@( @('a','b'), @('c','d') )` 这类嵌套数组时被展平，`$p[0]` / `$p[1]` 取到的是**字符串的
前两个字符**而不是两个数组元素，于是 `String.Replace('c','o')`、`String.Replace('!','d')`
被当成「整篇替换」执行：

| 事故 | 文件 | 损坏形态 |
|---|---|---|
| 事故 A | `src/gui/MainWindow.cpp` | 全文 `c` → `o`（`includes` 变 `inoludes`、`config` 变 `oonfig`…） |
| 事故 B | `scripts/install.nsi` | 全文 `!` → `d`（`!define` 变 `ddefine`…） |

事故 A 当时用「词典驱动还原 + 编译器报错回合」修复了约 2500 个 token，但该修复方法存在
一个**固有盲区**：如果某个局部标识符在**声明和所有使用处被一致地改名**，编译器不会报错、
单元测试照样通过，而词典也无法判定（因为正确拼写在整个工程里从此不再出现）。因此需要
一次专门的词法审计来兜底。

本次审计即针对该盲区，并顺带核对关键字符串常量与打包副本一致性。

---

## 二、审计范围与方法

### 2.1 范围

* 扫描 126 个文件（`SlimeFinder/` 下 129 个受支持文件中排除 `build/`、`dist/`、`third_party/`）。
* 扩展名：`.cpp .h .md .txt .json .ps1 .nsi .cl .cmake .bat .rc .py`。
* 排除：`build/`（编译产物）、`dist/`（发行副本）、`third_party/OpenCL-Headers`（上游 vendored 代码，非本项目编写）。

### 2.2 六类检查

| # | 检查 | 说明 |
|---|---|---|
| 1 | 已知坏片段 | 事故特征字面量（如 `ino·lude`、`o·onst`、`o·aohe`、`Che·oked`、`d·define`）精确匹配；模式表在本工具内以拆分字面量书写，工具不会自命中 |
| 2 | 词典判定 | 每个标识符 / 字符串字面量按 `_` 与 camelCase 切词，每段必须是已知词；若某段本身不是词、但把其中若干 `o` 换回 `c` 后成为**高频已知词**，即报出。词表来源：Qt 6.11.2 头文件 + MSVC 14.51 头文件 + v3.1.1 发行版文本 + 任务书 + 内置工程词表（共 39 万词条） |
| 3 | 跨文件还原验证 | 某 token 全树只出现在一个文件、而它的某个 `o→c` 变体出现在别的文件 → 几乎确定是污染（该法器用于抓「正确拼写别处仍在用」的情形） |
| 4 | 稀有二元组特征 | `c→o` 会造出英文/代码中罕见的 `uo / oh / ao / eo` 组合；仅对「切词后没有任何已知词段」的 token 报出，并人工复核 |
| 5 | 关键字符串常量 | 版本号、下载页 URL、访问密码、缓存/配置/日志/崩溃目录名、OpenCL 内核入口 |
| 6 | 产物一致性 | `build/config.json` 与 `dist/SlimeFinder/config.json` 能否解析、章节是否完整；`docs/` 与包内 `docs/` 副本逐字节比对 |
| 7 | 文件卫生（编码） | NUL 字节、非法 UTF-8 序列、`U+FFFD` 替换字符（乱码）、源码/文档被写成 UTF-16 |

### 2.3 灵敏度自测（证明工具真的能抓到）

审计工具本身经过一次注入式验证：临时写入一个含 4 类损坏的样本文件
（`oaoheSize`、`gpuWasCheoked`、字符串 `"…notoaohe…"`、`o4`），运行审计得到
`bad-pattern=8, dictionary=6, cross-file=1, digraph=1, short-name=3`、退出码 1；
删除样本后复跑恢复 `CLEAN`、退出码 0。样本文件已删除，不进入交付物。

---

## 三、发现与修复

### 3.1 真实字符污染：4 处（全部位于 `src/gui/MainWindow.cpp`）

| # | 行号 | 损坏形态 | 正确形态 | 分类 | 影响评估 |
|---|---|---|---|---|---|
| 1 | 4366 / 4410 | `oaoheWasEnabled` | `cacheWasEnabled` | `c→o` 残留（局部 bool，声明与使用一致） | 无功能影响（命名误导） |
| 2 | 4486 / 4507 | `gpuWasCheoked` | `gpuWasChecked` | `c→o` 残留（局部 bool） | 无功能影响（命名误导） |
| 3 | 4263 / 4264 | `CacheManager o4;` / `o4.loadFile(...)` | `CacheManager c4;` / `c4.loadFile(...)` | `c→o` 残留（`c4` 是该函数内 `c1/c2/c3` 命名序列的第 4 个） | 无功能影响 |
| 4 | 4258 | 字符串 `"slime_selftest_notoaohe.slimecache"` | `"slime_selftest_notcache.slimecache"` | `c→o` 残留（**字符串字面量**，会进入临时文件名） | 仅影响自检临时文件名可读性；该分支只校验「非缓存文件被拒绝」 |

修复方式：逐处用精确文本替换（`edit` 工具），随后全树复核 `oaohe` / `Cheoked` / `notoaohe` 命中数均为 0。

### 3.2 命名异常规范化：3 处（同文件，无法证明为字符污染，但与本文件自身惯例不符）

| # | 行号 | 原形态 | 现形态 | 依据 |
|---|---|---|---|---|
| 1 | 486 / 487 | `std::error_code leo;` 并传给 `create_directories` | `logEc` | 本文件错误码一律命名 `ec/ec2/ec3`；且外层的 `ec`（第 369 行声明、第 454 行使用）就在作用域内，`leo` 既非惯例也非避免重名的需要 |
| 2 | 3941–3943 | `const bool eok = SlimeEngine::estimateSearch(...)` | `estOk` | 同文件同类调用一律用 `ok` 类命名（`ok`、`okx/okz`、`okShot`）；`eok` 无出处 |
| 3 | 4582–4590 | `to0` / `to1` 两个时间点 | `tCancel0` / `tCancel1` | 本文件 7 处时间点一律 `t0`/`t1`（87、1985、4019、4198、4379、4502、4745 行），`to0/to1` 为孤例；改用语义化名字避免与外层/嵌套块重名 |

三项均为**纯改名**，不改变任何行为（编译期绑定，无 ABI/序列化影响）。改名后残留检查为 0，
`estOk` 在 2808 行属另一函数作用域，无冲突。

### 3.3 复核后判定为「正常」的项目（曾有嫌疑，逐一排除）

| 项目 | 位置 | 判定 |
|---|---|---|
| `our` | `tests/verify_against_reference.py:128` 等 | 正常：该脚本统一用 `ours` 前缀（`run_our` / `our_text` / `our_smart`），`cur` 出现在 C++ 引擎里属巧合 |
| `od` / `o4` | `tests/verify_against_reference.py:221-226` | 正常：`o` 前缀 = "ours"，与同函数内参考侧命名一致 |
| `o2` … `o5` | `tests/test_slime.cpp:1911-1938` | 正常：`SearchOptions o, o2, o3, o4, o5` 是刻意的构造序列 |
| `Radeon` | 多份文档、`SlimeEngine.h`、`GpuSession.h` | 正常：显卡品牌名 |
| `FILEOS` | `src/app.rc:15` | 正常：Windows VERSIONINFO 固定字段 |
| `timeout` / `updateGeometry` / `tableOn` / OpenCL 厂商常量 | 多处 | 正常：切词后每段都是已知词，属第 4 类检查的白名单形态 |
| `cpuout` / `gpuok` / `gpuon` / `gpuout` / `contiguous` / `quoted` | 源码 | 正常：工程内既有命名与英文词 |

### 3.4 交付证据编码缺陷：18 处（第 [7] 类检查发现，已修复）

除字符污染外，审计的文件卫生检查在 `docs/` 的**交付证据文件**中发现 18 处编码缺陷。它们不是
「内容写错」，而是**诊断输出端与采集端的编码不一致**，导致中文标签在证据里变成 `U+FFFD` 乱码：

| 缺陷 | 文件 | 数量 | 根因 |
|---|---|---|---|
| 中文标签整体乱码 | `verify111_v330~v335.txt`、`pkgverify_reference111_v334.txt` | 每份 481~503 个 `U+FFFD` | `tests/verify_against_reference.py` 的 `print()` 在重定向下按 **ANSI(GBK)** 编码输出，而 PowerShell 按 UTF-8 解码 |
| 部分行乱码 + 非法字节 | `gui_selftest_v334/v335_gpu_on_stderr.txt` | 303 / 449 个 `U+FFFD` | GUI 自检用 `QString::toLocal8Bit()`（GBK）写 stderr，与同文件其它 UTF-8 行混排 |
| 单个 `U+FFFD` | `kernel_selftest_v331~v335.txt`、`pkgverify_kernel_selftest_v334.txt` | 每份 1 个 | 打印内核首行时按**字节**截断到 70 字节，切断了多字节 UTF-8 字符 |
| NUL 字节 | `verification_report_v335.md` | 3 个 | 报告写入时非 ASCII 片段被截断为 NUL，同时丢失了部分实测数据 |

**修复（源头 + 采集端 + 落地端三层）**：

1. 产生端统一 UTF-8：GUI 自检 22 处 `toLocal8Bit()` → `toUtf8()`，缓存路径改用 `u8string()`；
   `main.cpp` / `ocl_probe.cpp` 的崩溃报告由 `GetModuleFileNameA` 改为宽字符 + `WideCharToMultiByte(CP_UTF8)`；
   Python 脚本 `sys.stdout/stderr.reconfigure(encoding="utf-8")`；内核自检截断后退回 UTF-8 字符边界。
2. 采集端统一 UTF-8：`scripts/verify_release.ps1` 开头固定 `[Console]::OutputEncoding` 与 `$OutputEncoding`，
   并设置 `PYTHONUTF8=1`、`PYTHONIOENCODING=utf-8`。
3. 落地端可复现：新增 `scripts/export_evidence.ps1`，按 BOM 解码日志 → 统一写成带 BOM 的 UTF-8，
   并统计 `U+FFFD`/NUL（非零即退出码 1）。
4. 报告修复：`verification_report_v335.md` 的 3 个 NUL 已清除，缺失的实测数据按第 4 轮真实日志补齐，
   并更正了上一版「复核无残留」的表述。

**修复效果（同一轮实测对比）**：`reference111.log` 503→**0**、stderr 捕获 449→**0**、内核自检 1→**0**，
v3.3.5 全部证据文件编码体检 **0 缺陷**。

**历史证据的处理**：v3.3.0~v3.3.4 的证据因原始字节已被替换字符覆盖，无法无损还原，已移入
`docs/evidence_legacy/`（含 `README.md` 说明根因与修复），作为**已登记例外**排除在编码门禁之外
（例外写在工具常量 `SKIP_LEGACY_EVIDENCE` 处，附原因）；当前版本证据一律纳入门禁。
该目录只保留在源码仓库中，**不随交付包发布**（打包脚本只收集 `docs/` 顶层的 `.md`/`.txt` 与截图）。

### 3.5 复核后判定为「非缺陷」的三个易误判点

| 现象 | 判定 |
|---|---|
| `[UI自检] GPU 检测结果: 可用（）` 括号为空 | 非缺陷：括号内是「不可用原因」字段（`m_gpuProbeReason` 注释即「不可用原因」），仅在失败时填充 |
| GUI 报 `核数=12`，OpenCL 探针报 `CU: 18` | 非缺陷：前者是 **CPU 逻辑核数**（临界值回退公式 `显存(MB)×1000÷(核数×0.85)` 的自变量，代码 4460 行 `hardware_concurrency()`），后者是 **GPU `CL_DEVICE_MAX_COMPUTE_UNITS`**，两个不同的量；建议后续把日志标签写成「CPU 核数」以消除歧义 |
| `to0` / `to1` 时间点变量 | 已规范化（见 3.2）；本文件其它 7 处时间点一律 `t0`/`t1` |

---

## 四、关键字符串常量核对（全部通过）

| 常量 | 期望值 | 命中 |
|---|---|---|
| 下载页 URL | `https://wwben.lanzoul.com/b01d77eebe` | `MainWindow.cpp` ×1 ✅ |
| 访问密码 | `3o8i` | `MainWindow.cpp` ×8 ✅ |
| 默认缓存名 | `slime_cache.slimecache` | ×6 ✅ |
| 旧版缓存名 | `slime_cache.cache` | ×3 ✅ |
| 配置文件 | `config.json` | ×32 ✅ |
| 发布日期文件 | `build_date.txt` | ×6 ✅ |
| 日志目录 | `"logs"` | ×8 ✅ |
| 崩溃目录 | `crashes` | ×1 ✅ |
| OpenCL 内核入口 | `__kernel void scan_java_slime` | ×1 ✅ |
| 版本号 | `v3.3.5`（main/README）、`3.3.5`（CMake/app.rc/install.nsi） | 全部命中 ✅ |

产物一致性：`build/config.json` 与 `dist/SlimeFinder/config.json` 均可解析，章节一致
（`algorithm, cache, gpu, gpuBench, log, search, settings, terminal, version, window`）；
`docs/` 与包内 `docs/` 副本 **65 个文件逐字节一致**。

---

## 五、结论

1. 全树**未发现第二起事故规模的污染**；残留仅限事故 A 修复时遗留的 4 处局部标识符/字符串，
   均位于 `src/gui/MainWindow.cpp` 的 UI 自检代码段，**不影响搜索结果、不影响任何交付行为**。
2. 关键字符串常量、版本号、下载地址与密码、路径与文件名**全部完好**，与打包副本完全一致。
3. 已修复全部 4 处残留 + 规范化 3 处命名异常；修复后重跑审计为 `CLEAN`（退出码 0）。
4. 审计另发现并修复了 **18 处交付证据编码缺陷**（3.4 节）与 1 份报告文件被 NUL 截断的问题；
   v3.3.5 的证据已全部重新生成，编码体检 0 缺陷。
5. 由于源码发生改动，交付物已按门禁流程**重新干净重建、重跑全部测试并重新打包**
   （第 4 轮实测 `total=16 pass=16 fail=0`，见 `docs/verification_report_v335.md` §四、§五、§六）。
6. 审计工具经过注入式灵敏度验证：合成样本的 4 类损坏全部被抓到，样本删除后复跑为 `CLEAN`。

## 六、复现方式

```powershell
# 在 SlimeFinder 目录下
py -3 scripts\audit_corruption.py          # 退出码 0 = 无发现，1 = 有发现（可作发布门禁）
py -3 scripts\audit_corruption.py -v       # 附带词表来源统计

# 证据文件导出（按 BOM 解码日志 → UTF-8，并统计 U+FFFD/NUL）
powershell -File scripts\export_evidence.ps1 -LogDir "%TEMP%\slime_verify_<时间戳>" -Tag v335
```

## 七、防复发规则（已写入交付纪律）

1. **禁止**用 PowerShell 的 `String.Replace` / `-replace` 批量改文本，尤其禁止把「替换对」以嵌套数组形式传参。
2. 文本批量修改使用 Python 脚本（显式 `pathlib` 读写）或 `edit` 工具，改完全树复核关键词命中数。
3. `.ps1` / `.cmd` 辅助脚本保持纯 ASCII（Windows PowerShell 5.1 解析与编码差异是事故放大器）。
4. **诊断输出一律 UTF-8**：C++ 侧用 `toUtf8()` / `u8string()` / `WideCharToMultiByte(CP_UTF8)`，
   Python 侧用 `reconfigure(encoding="utf-8")`，采集端固定 `[Console]::OutputEncoding`；
   禁止按字节截断 UTF-8 文本。
5. 任何一次批量文本改动之后，必须运行 `scripts/audit_corruption.py` 并确认退出码为 0；
   每次打包后必须用 `scripts/export_evidence.ps1` 导出证据并确认 0 缺陷。
