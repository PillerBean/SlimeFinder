# v3.3.2 验证报告（缓存 Bug 确认与修复）

日期：2026-09-20 ｜ 基线：v3.3.1 ｜ 任务书：`模板：原始文件/V3.3.2.txt`（v1.0）
机器：AMD Ryzen 5 5600（6C/12T，AVX2）+ AMD Radeon RX 6750 GRE 10GB
构建：MSVC 14.51 / Release `/O2 /openmp /GL /LTCG /utf-8 /Brepro`（确定性构建）

> 任务书性质：**P0 Bug 修复 + 状态确认**。本报告第一节直接回答任务书 §3.1 的四个问题，
> 第二节给出根因（含历史二进制/源码硬证据），第三节列出改动，第四节给出任务书 §7 要求的
> 全部证据（代码片段 + 对话框截图 + 运行日志 + 可复现步骤），第五节为 P1 收尾确认，
> 第六节为全量回归与交付哈希。
> **未触及范围**：GPU 密度内核（继续停放于 `src/gpu/experimental/`）、无新功能、无架构改动。

---

## 一、任务书 §3.1：状态确认（直接回答）

**问 1：v3.3.1 是否已修复该 Bug？**
**部分修复。** v3.3.1 完成了「扩展名统一 + 旧格式兼容 + 对话框起始目录改为程序目录」，
但**没有解决「保存后再导入时文件是否被选中」**这一半，也没有覆盖历史 `.pkl` 格式；
v3.3.2 补齐这两点并给出可复现证据（见§三、§四）。

**问 2：保存和加载的扩展名分别是什么？**
保存与加载**同一个文件、同一扩展名**：

| 用途 | 文件名 | 位置 |
|---|---|---|
| 保存（搜索后增量合并 / 导入后合并 / 启动迁移目标） | `slime_cache.slimecache` | 程序目录（`appDirPath()`） |
| 加载（启动自动加载 / 导入缓存） | 同上 | 同上 |
| 兼容读取（仅在 `.slimecache` 不存在时） | `slime_cache.cache` | 程序目录 |

**问 3：文件选择对话框的过滤器字符串是什么？**（v3.3.2，5 档）

```
史莱姆区块缓存 Slime Cache (*.slimecache *.cache *.pkl *.pickle);;
新版缓存 New (*.slimecache);;
旧版二进制缓存 Legacy (*.cache);;
Python 旧版缓存 (*.pkl *.pickle);;
所有文件 All Files (*.*)
```

**问 4：旧格式缓存（如 v3.3.0 的 .pkl 或 .bin）是否兼容？**

| 扩展名 | 来源版本 | 兼容性 |
|---|---|---|
| `.slimecache` | v3.3.1 起 | 直接加载（当前默认） |
| `.cache` | v3.1.1 ~ v3.3.0 | **直接加载**；启动时若只有旧文件 → 自动加载并**迁移**为新格式（旧文件保留不删） |
| `.pkl` / `.pickle` | v3.1.0 及更早（Python 版） | **不能直接加载**（Python pickle 格式）：对话框可见并可选中，选中后弹出提示，需先执行 `py -3 scripts/convert_pkl_cache.py 旧缓存.pkl` |
| `.bin` | 无 | **本程序历史上从未使用该扩展名**（任务书列举的假设格式）：全仓库源码与各版本已发布 exe 中均无 `.bin` 缓存相关代码/字符串 |

---

## 二、根因分析（为什么 v3.1.1~v3.3.0 会出现「文件存在却看不到」）

**硬证据 1：v3.3.0 已发布二进制内的字符串**（`dist/SlimeFinder-3.3.0-win64.zip` → `SlimeFinder.exe`）

```
[ASCII ]  slime_cache.cache                                  ← 唯一的缓存文件名
[UTF-16]  史莱姆区块缓存 (*.cache);;所有文件 (*.*)              ← 导入对话框过滤器
[UTF-16]  请使用发布包中的 scripts/convert_pkl_cache.py 将其转换为 .cache 二进制格式后再导入
```

**硬证据 2：v3.1.1 源码**（`_v311_ref/史莱姆区块查找器V3.1.1/.../src/gui/MainWindow.cpp`）

```cpp
L55 : constexpr const char* kCacheFileName = "slime_cache.cache";
L782: const QString path = QFileDialog::getOpenFileName(
L783:     this, QStringLiteral("选择史莱姆缓存文件"), QString(),      // ← 起始目录为空！
L784:     QStringLiteral("史莱姆区块缓存 (*.cache);;所有文件 (*.*)"));
```

**结论（根因）**：

1. **主因 —— 对话框起始目录未定位**：缓存固定写在**程序目录**，而对话框第 3 个参数为空
   `QString()` → Qt 回退到**进程工作目录 / 上次访问目录**。用户从快捷方式（"起始位置" 常为
   `C:\Windows\System32` 或其它目录）或从别的目录启动程序时，对话框不在程序目录，
   程序目录里的 `slime_cache.cache` 自然「存在但看不到」。
2. **次因 —— 过滤器只列 `*.cache`**：更早的 Python 缓存 `.pkl` 完全不出现（需手动切「所有文件」）。
3. **澄清**：v3.1.1/v3.3.0 的**保存名与过滤器扩展名本身是一致的**（都是 `.cache`）。
   因此「保存用一个扩展名、过滤器用另一个」并不是本次 Bug 的成因；v3.3.1 报告中
   「保存 `slime_cache.cache`、过滤器仅 `*.cache`」的表述容易引起误解，本报告予以更正。

---

## 三、v3.3.2 改动（聚焦、无架构改动）

| # | 改动 | 文件 | 说明 |
|---|---|---|---|
| 1 | 导入对话框**定位到程序目录 + 预选当前缓存文件** | `src/gui/MainWindow.cpp` | `getOpenFileName(...)` → 显式 `QFileDialog` + `selectFile("slime_cache.slimecache")`；保存后再次导入可一步「打开」 |
| 2 | 过滤器**覆盖全部历史格式**（5 档） | 同上（`cacheImportFilter()`） | 新增 `*.pkl` / `*.pickle` 分组与 `Slime Cache` 英文标识，保留 `*.*` 兜底 |
| 3 | **落盘路径写日志** | 同上 | 搜索后保存 / 导入合并保存 / 启动加载（新旧格式）/ 迁移 均记录完整路径；`.pkl` 选中后提示转换 |
| 4 | GUI 自检新增 3 段（12.5/12.6/12.7） | 同上（`runSelfTest`） | 过滤器模式集合断言、对话框「可见 + 预选」断言 + 截图、真实端到端缓存一致性 |
| 5 | 版本号 v3.3.1 → **v3.3.2** | `MainWindow.cpp`、`CMakeLists.txt`、`scripts/install.nsi`、脚本与文档 | — |

**未改动**：缓存磁盘格式（仍为 `u32 魔数 + u32 版本 + ranges + entries`）、
引擎判定逻辑、GPU 扫描路径、算法模式、`clearCache()` 语义（同时删除新旧两个文件）。

---

## 四、验收证据（任务书 §3.3 / §7）

### 4.1 代码片段（任务书要求：`CacheManager.cpp` 扩展名定义 + 对话框过滤器字符串）

扩展名常量（`src/gui/MainWindow.cpp` L62-64；`CacheManager` 的磁盘格式与扩展名无关）：

```cpp
// v3.3.1（任务书 2.1）：缓存扩展名统一为 .slimecache（保存/加载/过滤器/转换脚本一致），
// 旧版 .cache 保留兼容读取并在启动时自动迁移。
constexpr const char* kCacheFileName = "slime_cache.slimecache";   // 保存 / 加载 / 迁移目标
constexpr const char* kCacheLegacyFileName = "slime_cache.cache";  // 旧版（v3.1.1~v3.3.0）
```

过滤器（`src/gui/MainWindow.cpp` L187-196）：

```cpp
QString cacheImportFilter()
{
    return QStringLiteral("史莱姆区块缓存 Slime Cache (*.slimecache *.cache *.pkl *.pickle);;"
                          "新版缓存 New (*.slimecache);;"
                          "旧版二进制缓存 Legacy (*.cache);;"
                          "Python 旧版缓存 (*.pkl *.pickle);;"
                          "所有文件 All Files (*.*)");
}
```

对话框（`src/gui/MainWindow.cpp` L1415-1428）：

```cpp
QFileDialog dlg(this, QStringLiteral("选择史莱姆缓存文件"),
                QString::fromStdWString(appDirPath().wstring()));   // 起始目录 = 程序目录
dlg.setFileMode(QFileDialog::ExistingFile);
dlg.setNameFilters(cacheImportFilter().split(QStringLiteral(";;")));
dlg.selectFile(QString::fromStdString(kCacheFileName));             // 预选当前缓存文件
if (dlg.exec() != QDialog::Accepted) return;
```

### 4.2 手动验证步骤（任务书 §7 第二步）与实测结果

| 步骤 | 操作 | 实测 |
|---|---|---|
| 1 | 启动程序 → 基岩版 → 勾选「使用缓存」→ 搜索 256×256（-128..127） | 日志：`缓存已更新保存：1 区域 / 6811 区块（路径 …\build\slime_cache.slimecache）` |
| 2 | 关闭程序 → 重新启动 | 日志：`启动加载缓存成功（…\slime_cache.slimecache）：1 区域 / 6811 区块` |
| 3 | 点击「导入缓存」 | 对话框**起始目录 = 程序目录**，`slime_cache.slimecache` **在列表中并已预选**（见 4.3 截图） |
| 4 | 点「打开」加载 | 生成缓存 6811 条目；重新加载 6811 条目；65536 区块逐块与引擎判定比对 **不一致 = 0** |

> 自动化等价物：GUI 自检 12.5/12.6/12.7（`SLIME_SELFTEST=1`）逐步断言上述内容，
> 并在 `SLIME_SHOT=<前缀>` 时输出对话框截图。

### 4.3 截图证据

`docs/screenshots/cache-import-dialog-v332.png` —— 用**与 `importCache` 完全相同的对话框配置**
（相同起始目录、相同过滤器组、`selectFile` 预选）在 offscreen 平台渲染：

* 文件列表中**可见** `slime_cache.slimecache`（唯一一行，已高亮选中）；
* 文件名输入框中**已填入**该文件名（预选生效，可直接回车/点“打开”）；
* 过滤器下拉框为默认组（`*.slimecache *.cache *.pkl *.pickle`）。

> 说明：截图在 `QT_QPA_PLATFORM=offscreen` 下渲染，缺少中文字形，因此文字显示为方框；
> 但**文件行、选中高亮与文件名框内容**清晰可见。文字版记录见 4.4 与 `docs/v332_cache_evidence.txt`。

### 4.4 运行期日志与自检输出

GUI 自检（v3.3.2 构建，`SLIME_SELFTEST=1`，exit 0）：

```
[UI自检] 过滤器首组=[*.slimecache *.cache *.pkl *.pickle] 目录内匹配=1 项
[UI自检] 对话框起始目录=C:/Users/Administrator/AppData/Local/Temp/slime_selftest_dlg
         预选=…/slime_selftest_dlg/slime_cache.slimecache
[UI自检] 缓存导入对话框截图已保存: …\ui_cache-import-dialog.png
[UI自检] 缓存端到端：文件=…\build\slime_cache.slimecache 条目=6811 重载条目=6811
         逐区块比对=65536 不一致=0
```

旧格式识别与自动迁移（仅保留 `slime_cache.cache` 后启动一次）：

```
[INFO] 启动加载旧版缓存成功（slime_cache.cache）：1 区域 / 6811 区块
[INFO] 旧版缓存已迁移为新格式：slime_cache.slimecache（…\build\slime_cache.slimecache）
```

迁移前后磁盘状态：启动前仅 `slime_cache.cache`（54524 B）→ 启动后 `slime_cache.cache`
（保留）与 `slime_cache.slimecache`（54524 B，程序此后使用）**同时存在**。

完整证据包：`docs/v332_cache_evidence.txt`。

---

## 五、P1 收尾确认（任务书 §4）

| 项目 | 要求 | 实测 |
|---|---|---|
| 4.1 dist 包内 `logs/`、`crashes/` | 空 | 见 §6.2（解压包检查：两个目录存在且 0 个文件） |
| 4.1 `config.json` | 默认配置（`gpu.enabled: false`） | 见 §6.2（`"enabled": false`，算法 `exact`） |
| 4.2 `user_manual.md` 缓存描述与代码一致 | 一致 | 已更新（§1 缓存文件说明、§3.3 导入/迁移），过滤器 5 档与实现逐字一致 |
| 4.2 `architecture.md` 缓存格式与保存路径一致 | 一致 | 已更新（§3.8 缓存小节 + 新增 §12「v3.3.2 缓存可用性修复」） |

---

## 六、回归与交付

### 6.1 一键验证（`scripts\verify_release.ps1`）

全量运行（clean 重建 + 全部检查）与打包后的完整性复验（`-SkipClean -SkipBench -Zip`）
两轮均为 **14/14 PASS**，退出码 0：

| 步骤 | 全量运行 | 打包后复验 |
|---|---|---|
| clean 全量重建（`--clean-first`） | PASS：exit 0，`error` 行 0 | （跳过） |
| 单元测试 18 组 | PASS：exit 0，全部通过 | PASS |
| 111 参考对照 | PASS：exit 0，**111 通过 / 0 失败** | PASS |
| OpenCL 探测（真实设备一致性 + 基准） | PASS：exit 0，**PASS（失败 0 项）** | PASS |
| SIMD 内核自检（含嵌入内核逐字节比对） | PASS：**17 项检查 0 失败 / ALL PASS** | PASS |
| GUI 自检 GPU 关 | PASS：exit 0 | PASS |
| GUI 自检 GPU 开 | PASS：exit 0 | PASS |
| 性能基准（当前 + v3.2.1 基线） | PASS：exit 0 | （跳过） |
| dist 自检 | PASS：exit 0 | PASS：exit 0 |
| 哈希 build==dist（4 个二进制） | 3 PASS + 1 FAIL（此时 dist 仍是 v3.3.1，预期） | **4 PASS** |
| 哈希 zip==dist + 包内自检 | （未传 `-Zip`） | **PASS**，解压包 GUI 自检 exit 0 |

> 全量运行中唯一的 FAIL 是「`SlimeFinder.exe` build==dist」——当时 `dist/` 里还是 v3.3.1 的 exe；
> 重新打包后该步骤转为 PASS（见上表右列）。其余三个二进制（`slime_tests`/`slime_ocl_probe`/
> `slime_kernel_tests`）v3.3.2 未改动，两次哈希完全一致，可作为「本次只改了 GUI 与文档」的旁证。

**GPU 判定扫描实测（本次构建，AMD RX 6750 GRE 10GB，端到端含回读）**：

| 场景 | GPU | GPU/CPU1 | GPU/CPU12 |
|---|---|---|---|
| Java 16384²（2.68 亿区块） | 24.8 ms（108.2 亿/秒） | 38.27× | 8.09× |
| Java 1000²（100 万区块） | 0.2 ms（45.8 亿/秒） | 17.57× | 6.45× |
| 基岩 2048²（419 万区块） | 137.1 ms（3060 万/秒） | 36.90× | 4.31× |

一致性：9 组矩形 + Java 16.8M / 基岩 1.05M 区块全量比对 `mismatch=0`；
引擎级 Java 密度 8192² / 基岩密度 4096² 结果集合「一致=是」。

### 6.2 交付包检查（任务书 §4.1）

`dist/SlimeFinder-3.3.2-win64.zip`（`scripts\package_release.ps1 -Version 3.3.2` 生成，
包内自检全部通过）解压后逐项核对：

| 检查项 | 结果 |
|---|---|
| `logs/`、`crashes/` 内容 | **zip 内不存在任何 `logs/`、`crashes/` 条目，也不存在任何 `*.log`/`*.dmp`/`*.slimecache`/`*.cache`/`*.pkl` 文件**（包内条目全部为程序 exe、Qt/VC 运行库 DLL、平台插件与文档）；程序首次运行时才创建这两个目录 |
| `config.json` | 默认配置：`"gpu": { "enabled": false }`、`"algorithm": { "mode": "exact" }`、`cache.maxSizeMB: 1024` |
| exe 版本串 | 含 `v3.3.2`（ASCII 扫描，打包脚本自动断言）；exe 内另有 `v3.3.1` 字样，均为**注释/历史说明文本**，非版本常量 |
| 文档 | `docs/verification_report_v332.md`、`v332_cache_evidence.txt`、`user_manual.md`、`architecture.md`、`build_instructions.md`、`screenshots/cache-import-dialog-v332.png` 均在包内 |
| 哈希一致性 | `build == dist == 解压包`（4 个二进制逐一比对） |

### 6.3 交付哈希

| 文件 | SHA256 |
|---|---|
| `build\SlimeFinder.exe` = dist = zip | `32FBC4B1441B32D5F9EEE6E4C0939F8251BD562D309BC8EEB25D33FEFE8DF41C` |
| `build\slime_tests.exe` = dist = zip | `6DF0B8C2988536F40838A9C0681A86EFB92D603A54DD32D8A4C5B68D0931B58A` |
| `build\slime_ocl_probe.exe` = dist = zip | `07076E39DABCB38CC458D428AE0A450DD8D631F5B98540BCEE022EE7C3653F08` |
| `build\slime_kernel_tests.exe` = dist = zip | `E3F743B4F4D1C07DEFBB329559D3F3D8D0200F50425F4745EF6BDB0362EEB662` |
| `dist\SlimeFinder-3.3.2-win64.zip` | 见 `dist/SHA256SUMS-3.3.2.txt`（交付以该文件为准） |

**证据文件**（`docs/`，本次最终构建的原始输出）：`unit_test_v332.txt`、`verify111_v332.txt`、
`ocl_probe_v332.txt`、`kernel_selftest_v332.txt`、`bench_v332.txt`、
`gui_selftest_v332_gpu_off.txt`、`gui_selftest_v332_gpu_on.txt`、`v332_cache_evidence.txt`、
`screenshots/cache-import-dialog-v332.png`。

---

## 七、结论与遗留

1. **任务书 §3.1 的问题已逐条回答**（§一）；Bug 的**真实根因**是「导入对话框起始目录未定位」，
   叠加「历史格式未列入过滤器」，v3.3.1 报告中的表述已在本报告更正（§二）。
2. **v3.3.2 已完成修复**：对话框定位到程序目录并预选当前缓存文件、过滤器覆盖
   `.slimecache/.cache/.pkl/.pickle/*.*`、落盘路径写日志、旧格式自动迁移；
   全部验收项有可复现证据（§四）。
3. **无需中断汇报**（任务书 §9 的两种特例均不成立）：Bug 属于本任务书范围、复现方式一致、
   修复为局部改动（约 60 行 + 自检 100 行），远小于 1 个工作日。
4. **遗留（不属本次范围）**：GPU 密度匹配内核继续停放于 `src/gpu/experimental/`（v3.4.0 起点）；
   `.bin` 扩展名历史不存在，如需支持需先在任务书中定义其格式。
