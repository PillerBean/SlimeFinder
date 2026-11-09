# v3.5.0 进度记录（首个正式版：安装程序 + 轻度压缩 + 正式版标记）

任务书顺序：**① 安装程序 ② 压缩 ③ 正式版标记 ④ 下载页提示（无） ⑤ 验收 ⑥ 汇报**。
本文件按块记录（做完一块更新一次）。

---

## 第 1 块：版本号与正式版标记（✅）

| 项 | 做法 |
|---|---|
| 版本号 3.4.13 → **3.5.0** | `src/app.rc`（4 段式 3.5.0.0 + FILEVERSION/PRODUCTVERSION）、`src/main.cpp`（kVersion / setApplicationVersion）、`src/gui/MainWindow.cpp`（kAppVersion）、`CMakeLists.txt`、`scripts/install.nsi`、`scripts/package_release.ps1`、`scripts/verify_release.ps1`、`README.md`、`docs/build_instructions.md` |
| 更新历史 **V3.5.0 标正式版** | 生成器 ENTRIES 里 `official=True`；共 **48 个版本**；界面按浅绿底 + 深绿版本号渲染（`histType=official`） |
| **补丁版取代规则**（任务书第四节） | 生成器新增 `normalize_official()`：某个正式版若被**同前缀补丁版**取代（如出现 3.5.0.1），自动降为测试版；md 与 C++ 两个输出共用同一份归一化结果 |
| 自检断言同步 | 条目数 47→**48**（两处）；正式版个数 3→**4**（2.8.4 / 3.1.1 / 3.3.0 / **3.5.0**）；历史说明文字同步 |
| 窗口标题 | `史莱姆区块查找器 v3.5.0`（由 `kAppVersion` 自动带入） |
| 程序图标 | 未改动（任务书要求不变） |

自检实测：`更新历史正式版：3.5.0 / 3.3.0 / 3.1.1 / 2.8.4（共 4 个）`、`更新历史文本：48 条版本行（格式异常 0）`。

## 第 2 块：轻度压缩（✅ 远超预期）

改 `scripts/package_release.ps1` 的 docs 步骤：包内**只保留** `docs/update_history.md` + 根目录 `README.md`；
不再复制 `docs/screenshots/`（1011 个条目）与开发文档（verification_report_*、progress、evidence 等）。

| 项 | v3.4.13 | **v3.5.0** |
|---|---|---|
| 便携包体积 | 83,381,930 B（≈79.5 MB） | **14,705,939 B（≈14.0 MB）** |
| 包内条目 | 1144 | **32** |
| 包内截图 | 1011 | **0** |
| 包内 docs | 1113 个条目 | **1（update_history.md）** |

未做（任务书「不做」）：Qt 插件精简、静态链接、额外压缩算法 ✓。
载荷完整性已核对：SlimeFinder.exe / Qt6*.dll / platforms\qwindows.dll / msvcp140*.dll / vcruntime140*.dll /
vcomp140.dll / 三个工具 exe / config.json / build_date.txt / README.md / docs\update_history.md 全部在包内。

## 第 3 块：安装程序（✅ 已实测安装 + 卸载）

`scripts/install.nsi` 全量重写（UTF-8 BOM + CRLF），新增 `scripts/build_installer.ps1`（构建前检查 + 调用 makensis + 报体积/哈希）。

| 要求 | 实现 |
|---|---|
| 全机器安装 / 默认 Program Files / 可自选路径 / UAC | `InstallDir "$PROGRAMFILES64\SlimeFinder"` + `InstallDirRegKey`（升级时预选旧目录）+ `MUI_PAGE_DIRECTORY` + `RequestExecutionLevel admin` |
| 快捷方式 | 开始菜单（`SetShellVarContext all` ⇒ `C:\ProgramData\...\Start Menu\Programs`）+ 桌面（组件页可选，公共桌面） |
| 依赖全部内置 | 包内自带 Qt 运行时、VC++ 运行时（msvcp140*/vcruntime140*/vcomp140）、平台与图像插件；OpenCL 由程序**动态加载**（`LoadLibrary` 系统/驱动 OpenCL.dll；导入表无 OpenCL 静态依赖，缺失时自动回退 CPU） |
| 卸载程序 + 「应用和功能」入口 | `WriteUninstaller` + `HKLM\...\Uninstall\SlimeFinder`（**SetRegView 64**，避免 32 位安装器写进 WOW6432Node）+ DisplayIcon / DisplayVersion / EstimatedSize / QuietUninstallString |
| 卸载删除全部 | 程序目录整目录（含 config.json / 缓存 / 日志 / crashes）+ 开始菜单 + 桌面快捷方式 + 卸载注册表项 + `Software\SlimeFinder` + `.cache` 文件关联 |
| 旧版处理 | 写文件**之前**把旧 `config.json` → `config.json.legacy`、`slime_cache.slimecache` → `.slimecache.legacy`、更早的 `slime_cache.cache` → `.cache.legacy`（文件保留；程序只认原名 ⇒ 不自动加载；可用程序内「导入缓存」手动恢复） |

**实测（`/S` 静默，完整输出见 `docs/evidence_v350/install_uninstall_test.txt`）**：
1. 自选目录 `C:\Temp\sfinstall` + 预置旧配置/旧缓存 ⇒ 安装 exit 0；两个 `.legacy` 文件保留、原文件名不存在、新 `config.json` 版本 v3.5.0（旧配置内容未被读取）；
2. 开始菜单 + 公共桌面快捷方式存在；卸载项在 **64 位**注册表视图（DisplayName=史莱姆区块查找器、DisplayVersion=3.5.0、InstallLocation、EstimatedSize）；
3. 装完能运行：安装目录内的 `SlimeFinder.exe` 跑 GUI 自检 ⇒ **exit 0 / FAIL 0**；
4. 卸载 ⇒ 安装目录、开始菜单、桌面快捷方式、卸载项、`Software\SlimeFinder` 全部消失；
5. 默认位置安装到 `C:\Program Files\SlimeFinder` 成功，并能卸载干净。

## 第 4 块：便携版与依赖（✅）

解压 `dist/SlimeFinder-3.5.0-win64.zip` 到独立目录，**把 PATH 收窄到只剩 `C:\Windows\system32`** 后运行 GUI 自检
⇒ **exit 0 / FAIL 0**（证明 Qt 与 VC++ 运行时全部 app-local，无需另装）。便携版与安装版同一套文件。

> 交付物：
> * 安装程序 `dist/SlimeFinder-3.5.0-Setup.exe` = 10,451,134 B（SHA256 `C9020E666ABE3529F1C8FEF54FC855F1111B983B8D12B4F91156BD859C402FC8`）
> * 便携版 `dist/SlimeFinder-3.5.0-win64.zip` = 14,705,939 B（SHA256 `95EFAC5733DDFEBED745A749422AE35457AA92155040D592EA7BD891AC3951C0`）
> * 校验和 `dist/SHA256SUMS-3.5.0.txt`

---

## 第 5 块：全量回归与验收（✅）

| 项 | 结果 |
|---|---|
| 单元测试 | **28 组全部通过** |
| 参考对照 | **111/111** |
| SIMD 内核 | **19 项 ALL PASS** |
| GUI 自检 GPU 关 / 开 | **exit 0 / exit 0，零 FAIL** |
| 发布包自检 | `verify_release.ps1` **16/16 PASS**（build == dist == zip 哈希一致） |
| 打包内部校验 | **PACKAGE OK** |
| 污染审计 | **CLEAN**（256 文件） |
| 安装/卸载实测 | **31 PASS / 0 FAIL**（含旧版改名保留、64 位卸载项、装完能运行、卸载清空、默认目录安装、便携版最小 PATH 运行） |

## 第 6 块：交付（✅）

| 交付物 | 体积 | SHA256 |
|---|---|---|
| `dist/SlimeFinder-3.5.0-Setup.exe` | 10,451,134 B | `C9020E666ABE3529F1C8FEF54FC855F1111B983B8D12B4F91156BD859C402FC8` |
| `dist/SlimeFinder-3.5.0-win64.zip` | 14,705,939 B | `95EFAC5733DDFEBED745A749422AE35457AA92155040D592EA7BD891AC3951C0` |
| `dist/SHA256SUMS-3.5.0.txt` | — | 含程序 / 三个工具 / 便携包 / 安装程序 |

- 别名包（`SlimeFinder-3.4.1-win64.zip`、`SHA256SUMS-3.4.1.txt`）已删除，不进最终交付物。
- 文档：`docs/verification_report_v350.md`（验收逐条 + 踩坑记录）、本文件、`docs/update_history.md`（48 个版本，V3.5.0 正式版）。
- 证据：`docs/evidence_v350/` 9 份（安装卸载实测 31 项、自检关/开、verify、打包、单元、参考、审计、构建）。
- 截图：`docs/screenshots/v350/` 51 张（仅仓库留档，**不进包**）。

**结论**：任务书六块全部完成。安装程序可装可卸、依赖全内置、旧版文件保留但不加载、便携版解压即用、
更新历史 V3.5.0 标正式版（48 条）、包体积 79.5 MB → 14.0 MB；全量回归与审计全绿。
