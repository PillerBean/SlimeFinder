# GitHub 发布与手动更新准备指南（SlimeFinder）

> ⚠️ **强制流程：2026-11-10 起以 [`release_process_decision.md`](release_process_decision.md) 为准。**
> 核心规则：**Git 历史是唯一事实来源；`git push` 不成功就不发布。**
> 禁止用 GitHub API 直接创建 tree/commit 替代 git push 上传源码。
>
> **更新方式（v3.12.3 起）：**程序只提示新版本，**不自动下载、不自动安装**。用户需手动从 Releases 下载 Setup 并核对 SHA256。
> 本文档下方关于自动更新的历史设计仅保留作参考，不再作为当前发布要求。
>
> 本文档给「第一次用 GitHub」的人看：**源码进仓库，安装包进 Releases**。
> 历史版本号仅作示例；发布前以当前版本号为准。

## 一、两个地方分别放什么

| 位置 | 放什么 | 不要放什么 |
|---|---|---|
| **Repository（仓库）** | `src/`、`tests/`、`scripts/`、`tools/`、`third_party/`、`assets/`、`CMakeLists.txt`、`README.md`、`docs/`（不含大截图/视频）、`.gitignore`、`LICENSE` | `build/`、`dist/`、安装包、日志、缓存、`config.json` |
| **Releases（发行版）** | 每个版本的 `SlimeFinder-X-Setup.exe`、`SHA256SUMS-X.txt`（**只发 Setup，不再发便携 zip**） | 源码压缩包（GitHub 会根据 tag 自动生成源码包）、便携 zip（历史版本除外） |

原因：Git 仓库适合存文本、要求体积小；Releases 专门放大文件，并且更新提示程序需要访问
`/releases/latest` 拿版本信息。

## 二、本机现状（已盘点）

- 当前源码：**3.10.12.8**（本目录）；
- 旧源码：**只有 V3.1.1**（`..\旧树历史任务书\史莱姆区块查找器V3.1.1.zip` 与 `旧树归档` 解压版）；
  **3.2.1 ~ 3.10.12.7 的源码没有找到**（只有 dist 里的二进制包），无法补回；
- 安装包：`dist/` 下 128 个 zip + 104 个 Setup，版本 3.2.1 → 3.10.12.8，合计约 3.4 GB；
- 结论：**从现在开始用 Git 保存历史**；老版本只能作为历史 Releases 的二进制上传。

## 三、仓库结构与排除规则

本目录已放好 `.gitignore`（排除 `build/`、`dist/`、`docs/screenshots/`、`docs/promo/`、
`logs/`、`crashes/`、`*.slimecache`、`config.json`、根目录误生成物等）。

建议保持如下结构（仓库根 = 本 `SlimeFinder/` 目录）：

```
src/  tests/  scripts/  tools/  third_party/  assets/  docs/
CMakeLists.txt  README.md  .gitignore  LICENSE
```

说明：
- `docs/screenshots/`（86 MB）和 `docs/promo/`（12 MB）不进 git；需要展示时压缩后单独上传或以后用 Git LFS；
- `third_party/cubiomes` 是 MIT 协议，保留其 LICENSE；
- 是否开源、选什么协议（MIT / Apache-2.0 / 私有）由你决定；Public 仓库才能让自动更新**免 token** 读取 Releases。

## 四、第一次上传：一次性步骤

### 1. 登录 GitHub CLI

```bat
cd /d "<工作区>"
gh auth login
```

按提示选：
- `GitHub.com`
- `HTTPS`
- `Login with a web browser`（会显示 8 位验证码，在浏览器打开 https://github.com/login/device 输入）

登录后设置 git 使用 gh 作为凭据：

```bat
gh auth setup-git
```

### 2. 本地仓库初始化并提交（源码）

```bat
cd /d "<工作区>\SlimeFinder"
git init -b main
git config user.name  "你的GitHub用户名"
git config user.email "你的GitHub邮箱"
git config http.schannelCheckRevoke false
git add .
git commit -m "chore: 初始提交（源码 3.10.12.8）"
```

> `http.schannelCheckRevoke false` 只在本仓库生效，用于避开本机证书吊销检查失败导致的 push 报错；
> 若 push 仍报 `CRYPT_E_NO_REVOCATION_CHECK`，先保持此项。

### 3. 创建远程仓库并推送

```bat
cd /d "<工作区>"
gh repo create SlimeFinder --public --source "SlimeFinder" --remote origin --push
```

（`--public` 改 `--private` 即私有；私有仓库自动更新需要额外 token，建议先用 Public。）

### 4. 给当前版本打 tag + 建 Release

```bat
cd /d "<工作区>\SlimeFinder"
git tag -a v3.10.12.8 -m "v3.10.12.8"
git push origin v3.10.12.8

gh release create v3.10.12.8 ^
  "dist\SlimeFinder-3.10.12.8-win64.zip" ^
  "dist\SlimeFinder-3.10.12.8-Setup.exe" ^
  "dist\SHA256SUMS-3.10.12.8.txt" ^
  --title "SlimeFinder v3.10.12.8" --notes "性能优化版本（详见更新历史）"
```

以后每次发版重复这一段即可（改版本号）。

### 5. 历史版本安装包（可选，分批）

GitHub Releases 没有硬性总量限制，但 3.4 GB 上传慢、也没必要：
- 优先上传 `3.9.x` 之后 + 所有 `3.10.x`；
- 更老的可以打包成一个 `legacy` Release（例如把 3.2.1~3.8.x 的 zip 合成一个 7z），或以后需要时再补；
- 每个 Release 建议跟一个 tag（如 `v3.10.9`），保持与版本号一致。

## 五、更新提醒与手动更新（v3.12.3 起）

公开仓库客户端仍可请求：

```text
GET https://api.github.com/repos/<用户名>/SlimeFinder/releases/latest
```

或读取 `version.json` 清单，用于**提示新版本**。

当前流程：

1. 比较本地版本与 `tag_name` / `version`；
2. 只提示“发现新版本”和更新说明；
3. **不自动下载、不自动安装**；
4. 用户手动打开 Releases 下载 Setup；
5. 用户自行核对 `SHA256SUMS-X.txt` 后安装。

发布时仍要保证：

```text
tag 名 + 资产文件名 + SHA256SUMS 一致
```

但程序端不得恢复自动下载/自动安装，除非以后完成代码签名并单独评审。

## 六、发布前安全/合规检查

- 源码目前硬编码了蓝奏云发布链接和访问密码 `3o8i`（`MainWindow.cpp`、`UiStrings.h`、`README.md`）。
  公开仓库会暴露它们；建议在接入 GitHub 更新后把「检查更新」切到 Releases 并移除旧链接/密码。
- **不要提交**：`config.json`（可能含本机路径/设置）、`logs/`、`crashes/`、`*.slimecache`、任何个人种子/索引缓存。
- 第三方库：`third_party/cubiomes` 的 MIT LICENSE 必须保留。
- 建议在 README 增加：构建方法、运行依赖（VC++ 运行库/OpenCL 可选）、版本发布说明链接。

## 七、常见问题

| 现象 | 处理 |
|---|---|
| `curl`/`git` 报 `CRYPT_E_NO_REVOCATION_CHECK` | 命令加 `--ssl-no-revoke`（curl）；git 执行 `git config http.schannelCheckRevoke false` |
| push 提示认证失败 | 先 `gh auth login`，再 `gh auth setup-git` |
| Releases 上传太慢/中断 | 用 `gh release upload <tag> 文件 --clobber` 断点重传（单文件可重传） |
| 仓库太大 | 检查是否误加了 `build/`、`dist/`、`docs/screenshots/`、`docs/promo/` |
| README 图片不显示 | 图片若在 `docs/screenshots/`（被忽略），改为压缩后放 `docs/images/` 再提交 |

## 八、本机工具与已准备好的内容

已完成：
- Git：已安装（`git version 2.56.0.windows.4.niugit`）；
- GitHub CLI：已下载并验证 `gh version 2.103.3`，路径
  `gh`（压缩包 `..\旧树归档的 gh 压缩包` 保留）；
- 本地仓库已初始化：`SlimeFinder/` 已 `git init -b main`，当前 2 个提交，已创建本地 tag `v3.10.12.8`；
  仓库只跟踪源码/文档（452 个文件、约 7.6 MB），`build/`、`dist/`、`docs/screenshots/`、`docs/promo/` 等均已忽略；
- `scripts/collect_release_assets.ps1` 已就绪并实测通过（3.10.12.8 三个资产已汇总到 `release/3.10.12.8/`）：

```bat
powershell -ExecutionPolicy Bypass -File scripts\collect_release_assets.ps1 -Version 3.10.12.8
```

已完成（2026-11-09）：
- GitHub 账号 `PillerBean` 已通过设备码授权登录（token 保存在本机 GitHub CLI keyring）；
- 公开仓库已创建并推送：https://github.com/PillerBean/SlimeFinder
- tag `v3.10.12.8` 已推送；
- Release 已发布并上传 3 个资产：
  https://github.com/PillerBean/SlimeFinder/releases/tag/v3.10.12.8
  资产 SHA256 与本地 `dist/SHA256SUMS-3.10.12.8.txt` 完全一致（GitHub 服务端 digest 已核对）。

后续可做：
- 历史版本安装包按需补传到 Releases（建议从 3.9.x 起，或打包成 legacy Release）；
- 接入 GitHub Releases 自动更新（替换目前硬编码的蓝奏云链接）；
- README 顶部加仓库/发布页链接。

安装图形界面 GitHub Desktop 是可选项，不是必须。

## 九、已确定的发布规则（2026-11-09）

### 版本号与正式/测试版

- **所有版本统一四段版本号**，例如 `3.10.12.9`、`3.10.12.10`（不再用 `X.Y.Z` 表示正式版）；
- 正式 / 测试**不用版本号区分**，统一在 GitHub Release 上手动标记：
  - **正式发布**：Release **不勾** `Pre-release` ⇒ 自动成为 **Latest**；
  - **测试发布**：发布时勾选 `Pre-release`（或命令加 `--prerelease`）⇒ 不会成为 Latest；
- Latest 由手动发布行为控制；自动更新默认只读 `/releases/latest`，
  以后若要支持“测试通道”，再读 `/releases` 列表里 `prerelease=true` 的更高版本；
- 应用内「更新历史」里的 ★正式版 仅用于历史里程碑（2.8.4 / 3.1.1 / 3.3.0 / 3.5.0 / 3.6.19），
  不再表示新版本一定是正式版。

### 每次发版的固定流程

```bat
cd /d "<工作区>\SlimeFinder"
:: 1) 改版本号（CMakeLists / main.cpp / app.rc / MainWindow / 脚本）；不再有内置更新历史
cmd /c scripts\build\build_repo.cmd
build\slime_tests.exe --unit
py -3 tests\verify_against_reference.py
py -3 scripts\audit_corruption.py
powershell -NoProfile -Command "& .\scripts\qa\gui_selftest.ps1 -LogDir build\final -GpuOn"
:: 2) 提交并打 tag
git add -A
git commit -m "release: v3.10.12.9"
git tag -a v3.10.12.9 -m "SlimeFinder v3.10.12.9"
git push origin main
git push origin v3.10.12.9
:: 3) 打包并汇总资产
powershell -NoProfile -Command "& .\scripts\release\package_release.ps1 -Version 3.10.12.9 -SkipSelfTest"
powershell -NoProfile -Command "& .\scripts\release\build_installer.ps1 -Version 3.10.12.9"
powershell -ExecutionPolicy Bypass -File scripts\collect_release_assets.ps1 -Version 3.10.12.9
:: 4) 发布（正式版：不带 --prerelease；测试版：加 --prerelease）
gh release create v3.10.12.9 "release\3.10.12.9\*" --title "SlimeFinder v3.10.12.9" --notes-file docs\release_notes\v3.10.12.9.md
```

### 许可证与仓库展示（已定）

- 许可证：**PolyForm Noncommercial 1.0.0** + `Required Notice` 署名要求（见 `LICENSE`）；
  源码公开、非商业使用；再分发/修改版必须注明来源
  `SlimeFinder — https://github.com/PillerBean/SlimeFinder`；商业使用需作者书面授权；
- `NOTICE` 列出第三方组件（cubiomes MIT / OpenCL-Headers Apache-2.0 / Qt 6）；
- README 已重写为新人向结构（截图 + 下载 + 功能 + 快速开始 + 构建 + 许可），
  完整历史只在 GitHub（Wiki 版本索引 / Releases）查询，程序与安装包不再内置；
- 仓库 About：一句话简介；Homepage 指向 Releases 页。

### 当前状态

- 仓库：https://github.com/PillerBean/SlimeFinder （Public）
- 旧 Release `v3.10.12.8` 已按用户要求删除（含 tag）；
- **`v3.10.12.9` 已发布，但按最新决定作为测试版**（Pre-release，不占 Latest）：
  https://github.com/PillerBean/SlimeFinder/releases/tag/v3.10.12.9
- 历史版本只在 GitHub Releases / Wiki 版本索引查询；程序内不再提供历史页面。
- 仓库 About 已精简为：`Minecraft 史莱姆区块查找器 | C++20/Qt6 | GPU 加速 | 源码公开（非商业）`，
  Homepage 指向 Releases 页。

## 十一、更新提醒与镜像源（v3.10.12.10 历史设计）

> **v3.12.3 起已停用自动下载/自动安装。** 本节仅说明“检查更新/镜像回退”的历史实现；实际发版只允许提示新版本，用户手动下载。

程序内「帮助 → 检查更新」提供三种来源（选择会记住，存在 `update_config.json`）：

1. **自动（推荐）**：先试 GitHub 官方；官方不可达时，对内置的 9 个候选镜像**自动测速**
   （清单响应 + zip 前 256KB 采样），选出最快的镜像继续检查与更新下载，**用户不需要手动切换**；
2. **GitHub 官方**：`api.github.com` + `raw.githubusercontent.com`（需要能直连 GitHub）；
3. **网盘（备用）**：蓝奏云页面，仅手动打开，不做自动更新适配。

为什么镜像回退走 `version.json` 清单：`api.github.com` 一般不能被这些加速站代理，而
`raw.githubusercontent.com` 和 `github.com/.../releases/download/...` 可以被代理。

内置镜像候选（按测速结果自动选择，失效的会自动跳过）：
`gh-proxy.com` / `ghfast.top` / `gh-proxy.org` / `ghproxy.net` / `ghproxy.cc` / `gh.llkk.cc` / `github.moeyy.xyz` / `gh-proxy.net`。

### 发版后的固定动作

1. 上传 Release（**只传 Setup + SHA256SUMS**；不再传便携 zip）；
   - 正式版：不勾 Pre-release，并更新 `version.json`（稳定通道）；
   - 测试版：勾 Pre-release，并更新 `version-beta.json`（测试通道）；
2. 生成并提交版本清单（检查更新的镜像回退依赖它）：

```bat
cd /d "<工作区>\SlimeFinder"
py -3 scripts\release\gen_version_manifest.py --version 3.10.12.10 --notes-file docs\release_notes\v3.10.12.10.md
git add version.json && git commit -m "release: version.json 3.10.12.10" && git push
```

### 更新包下载（历史实现，v3.12.3 起停用）

> 当前版本不自动下载；以下流程仅保留给历史版本参考。

- 下载 zip 到 `%USERPROFILE%\Downloads\SlimeFinder-update-v<版本>\`；
- 下载后用 Release 的 `SHA256SUMS-<版本>.txt` 校验；校验失败自动删除；
- 目前是「下载后提示用户手动解压覆盖」，不做静默替换。

