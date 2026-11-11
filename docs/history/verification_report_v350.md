# SlimeFinder v3.5.0 验证报告（首个正式版：安装程序 + 轻度压缩 + 正式版标记）

- **安装程序**：`dist/SlimeFinder-3.5.0-Setup.exe`（**10,451,134 B**，
  SHA256 `C9020E666ABE3529F1C8FEF54FC855F1111B983B8D12B4F91156BD859C402FC8`）
- **便携版**：`dist/SlimeFinder-3.5.0-win64.zip`（**14,705,939 B**，
  SHA256 `95EFAC5733DDFEBED745A749422AE35457AA92155040D592EA7BD891AC3951C0`）
- **校验和**：`dist/SHA256SUMS-3.5.0.txt`（程序 / 三个工具 / 便携包 / 安装程序）
- 主程序 `SlimeFinder.exe` SHA256 `1EAE9E94D352C368BE20DB842DBA60D9CCBF3041505E6F7F11D06C1F4156C990`
- 进度：`docs/v350_progress.md`；证据：`docs/evidence_v350/`（安装卸载实测 31 项、自检关/开、verify、打包、单元、参考、审计、构建）
- 截图：`docs/screenshots/v350/`（51 张，**不进包**，仅仓库留档）

## 一、验收（任务书第六节逐条）

| 验收项 | 标准 | 结果 |
|---|---|---|
| 安装程序 | 双击可装、可自选路径、装完能运行 | ✅ 带 UAC 清单（`RequestExecutionLevel admin`）+ 目录选择页；实测静默安装到**自选目录**与**默认 `C:\Program Files\SlimeFinder`** 均 exit 0；安装目录内 `SlimeFinder.exe` 跑 GUI 自检 **exit 0 / FAIL 0** |
| 依赖 | 无 Qt / 无 VC++ 运行时也能跑 | ✅ 包内自带 Qt6*.dll、VC++ 运行时（msvcp140*/vcruntime140*/vcomp140）、平台与图像插件；**把 PATH 收窄到只剩 `C:\Windows\system32`** 后安装版与便携版都 exit 0（OpenCL 由程序动态 `LoadLibrary` 系统/驱动 DLL，导入表无静态依赖，缺失自动回退 CPU） |
| 卸载 | 从「应用和功能」卸载，删除所有文件 + 配置 + 缓存 | ✅ 注册表 `HKLM\...\Uninstall\SlimeFinder`（**64 位视图**）带 DisplayName/DisplayVersion/DisplayIcon/EstimatedSize/QuietUninstallString；卸载后安装目录（含 config.json / 缓存 / logs / crashes）、开始菜单、桌面快捷方式、卸载项、`Software\SlimeFinder` 全部消失 |
| 便携版 | 解压即用，与安装版功能一致 | ✅ 解压 32 个文件（无截图 / 无开发文档），自检 exit 0 / FAIL 0，与安装版同一套载荷 |
| 旧版处理 | 保留旧配置/缓存但不加载 | ✅ 安装时（**写文件之前**）旧 `config.json` → `config.json.legacy`、`slime_cache.slimecache` → `.slimecache.legacy`、更早的 `slime_cache.cache` → `.cache.legacy`；原文件名不存在 ⇒ 程序只认原名、不会自动加载；实测旧内容原样保留（64 B，含 v3.4.0），运行后 `config.json` 仍是随包默认（v3.5.0）；用户可用程序内「导入缓存」手动恢复 |
| 更新历史 | V3.5.0 标正式版，47+1 条 | ✅ **48 个版本**；`V3.5.0 · 2026-09-24 ★正式版`；自检实测 `更新历史正式版：3.5.0 / 3.3.0 / 3.1.1 / 2.8.4（共 4 个）`、`48 条版本行（格式异常 0）` |
| 压缩 | 包体积明显下降 | ✅ **83,381,930 B → 14,705,939 B（−82%）**；包内条目 1144 → **32**，截图 1011 → **0**，docs 1113 → **1**（仅 `update_history.md`） |
| 回归 | 单元 28 / 111 / 内核 19 / GUI 开·关 / 审计 CLEAN / verify 16/16 | ✅ 全部通过（见第三节） |

## 二、交付物与安装包内容

| 交付物 | 用途 | 体积 |
|---|---|---|
| `dist/SlimeFinder-3.5.0-Setup.exe` | 安装程序（用户只需下载这一个） | 10,451,134 B |
| `dist/SlimeFinder-3.5.0-win64.zip` | 便携版（解压即用） | 14,705,939 B |
| `dist/SHA256SUMS-3.5.0.txt` | 校验和 | 604+ B |

包内载荷（32 项）：`SlimeFinder.exe`、`Qt6{Core,Gui,Widgets,Network,Svg}.dll`、`platforms/imageformats/iconengines/styles/tls/networkinformation/generic` 插件、
`msvcp140*.dll`、`vcruntime140*.dll`、`vcomp140.dll`、三个工具 exe（`slime_tests`/`slime_ocl_probe`/`slime_kernel_tests`，发布校验用）、
`config.json`（干净默认）、`build_date.txt`、`README.md`、`docs/update_history.md`。

**别名包已删除**（`SlimeFinder-3.4.1-win64.zip` / `SHA256SUMS-3.4.1.txt`）—— 依「别名包不要出现在最终交付物里」的历史教训。

## 三、回归（最终构建）

| 项目 | 结果 |
|---|---|
| 编译 | exit 0（无 `error C` / `error LNK`） |
| 单元测试 | **28 组全部通过** |
| 参考对照 | **111/111** |
| SIMD 内核自检 | **19 项 ALL PASS** |
| GUI 自检（GPU 关 / 开） | **exit 0 / exit 0，零 FAIL** |
| 发布包自检 | `verify_release.ps1` ⇒ **16/16 PASS**（build == dist == zip 三方哈希一致） |
| 打包内部校验 | **PACKAGE OK**（包内 GUI 自检 + 单元 15 组 + 内核 + 版本串/VERSIONINFO=3.5.0） |
| 污染审计 | **CLEAN**（256 文件，0 项发现） |
| 安装/卸载实测 | **31 PASS / 0 FAIL**（`docs/evidence_v350/install_uninstall_test.txt`） |

## 四、实现要点与踩到的坑

1. **NSIS 3.12**（本机原先没有 → 经 `winget install NSIS.NSIS` 安装）；新增 `scripts/build_installer.ps1`：
   编译前检查载荷目录、拒绝把 `*.slimecache/*.cache/*.legacy/*.log/*.dmp` 打进去（防止把多 GB 缓存塞进安装包），并输出体积与 SHA256。
2. **注册表视图**：NSIS 生成的是 32 位安装器 ⇒ 默认把 HKLM 写进 `WOW6432Node`；用 `SetRegView 64`（安装 + 卸载两侧）修正，
   实测卸载项出现在 64 位视图、32 位视图无残留。
3. **开始菜单位置**：NSIS **没有** `$COMMONPROGRAMS` 常量（第一版误用 → makensis 报 unknown variable）；
   正确做法是 `SetShellVarContext all` + `$SMPROGRAMS/$DESKTOP`，实测快捷方式落在 `C:\ProgramData\...\Start Menu\Programs` 与公共桌面。
4. **旧文件改名时机**：必须在 `File /r` **之前**改名，否则随包的新 `config.json` 会先覆盖旧文件、旧配置就丢了。
5. **正式版「被取代即降级」规则**：生成器新增 `normalize_official()` —— 某正式版若被同前缀补丁版取代（如 3.5.0.1），
   自动降为测试版；md 与 C++ 两种输出共用同一份归一化结果，避免两边不一致。
6. **轻度压缩**：只改 `package_release.ps1` 的 docs 步骤（只拷 `update_history.md` + `README.md`，不拷 screenshots/evidence/开发文档）；
   未做 Qt 插件精简、静态链接、额外压缩算法（任务书「不做」）。

## 五、说明与后续

1. 安装版与便携版是**同一套载荷**：安装版只是多了一层安装/卸载与快捷方式；两者运行行为一致（同一 exe、同一 config 位置语义）。
2. 静默安装参数（`/S`、`/D=`）仅用于本次验收测试，**不作为产品功能对外宣传**（任务书「不做：命令行参数」）。
3. 数字签名未做（任务书「不做」）⇒ 首次运行可能出现 SmartScreen 提示，属预期。
4. 本报告写在打包与安装测试之后，zip/Setup 内不含本文件；仓库 `docs/` 保留全部开发文档与截图。
