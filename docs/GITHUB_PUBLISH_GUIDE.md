# GitHub 发布与自动更新准备指南（SlimeFinder）

> 本文档给「第一次用 GitHub」的人看：**源码进仓库，安装包进 Releases**，自动更新从 Releases 读取。
> 当前源码版本：`3.10.12.8`；本机工具：Git 已安装（2.56），GitHub CLI 将放在
> `D:\DeepSeek harness\史莱姆区块查找器\github_tools\gh_dir\bin\gh.exe`（下载/解压见文末）。

## 一、两个地方分别放什么

| 位置 | 放什么 | 不要放什么 |
|---|---|---|
| **Repository（仓库）** | `src/`、`tests/`、`scripts/`、`tools/`、`third_party/`、`assets/`、`CMakeLists.txt`、`README.md`、`docs/`（不含大截图/视频）、`.gitignore`、`LICENSE` | `build/`、`dist/`、安装包、日志、缓存、`config.json` |
| **Releases（发行版）** | 每个版本的 `SlimeFinder-X-win64.zip`、`Setup.exe`、`SHA256SUMS-X.txt` | 源码压缩包（GitHub 会根据 tag 自动生成源码包） |

原因：Git 仓库适合存文本、要求体积小；Releases 专门放大文件，且自动更新程序就是访问
`/releases/latest` 拿资产。

## 二、本机现状（已盘点）

- 当前源码：**3.10.12.8**（本目录）；
- 旧源码：**只有 V3.1.1**（`..\模板：原始文件\史莱姆区块查找器V3.1.1.zip` 与 `..\_v311_ref` 解压版）；
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
cd /d "D:\DeepSeek harness\史莱姆区块查找器"
github_tools\gh_dir\bin\gh.exe auth login
```

按提示选：
- `GitHub.com`
- `HTTPS`
- `Login with a web browser`（会显示 8 位验证码，在浏览器打开 https://github.com/login/device 输入）

登录后设置 git 使用 gh 作为凭据：

```bat
github_tools\gh_dir\bin\gh.exe auth setup-git
```

### 2. 本地仓库初始化并提交（源码）

```bat
cd /d "D:\DeepSeek harness\史莱姆区块查找器\SlimeFinder"
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
cd /d "D:\DeepSeek harness\史莱姆区块查找器"
github_tools\gh_dir\bin\gh.exe repo create SlimeFinder --public --source "SlimeFinder" --remote origin --push
```

（`--public` 改 `--private` 即私有；私有仓库自动更新需要额外 token，建议先用 Public。）

### 4. 给当前版本打 tag + 建 Release

```bat
cd /d "D:\DeepSeek harness\史莱姆区块查找器\SlimeFinder"
git tag -a v3.10.12.8 -m "v3.10.12.8"
git push origin v3.10.12.8

github_tools\gh_dir\bin\gh.exe release create v3.10.12.8 ^
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

## 五、自动更新以后怎么接

公开仓库客户端可以直接请求：

```
GET https://api.github.com/repos/<用户名>/SlimeFinder/releases/latest
```

返回里关键字段：
- `tag_name`：如 `v3.10.12.9`
- `assets[].name` / `assets[].browser_download_url`：安装包地址
- `assets[].size`；`body`：更新说明

更新流程建议：
1. 比较本地版本与 `tag_name`（语义化版本比较）；
2. 下载 `SlimeFinder-X-win64.zip`；
3. 用 Release 里的 `SHA256SUMS-X.txt` 校验；
4. 退出程序 → 解压覆盖 → 重启。

可以自己写（WinHTTP/WinINet + JSON 解析），也可以直接集成 **WinSparkle**（成熟的开源更新框架）。
发版时注意：**tag 名 + 资产文件名 + SHA256 清单**三者保持一致，更新器最省事。

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
  `..\github_tools\gh_dir\bin\gh.exe`（压缩包 `..\github_tools\gh.zip` 保留）；
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

## 九、当前仓库信息（2026-11-09 已完成）

- 账号：`PillerBean`
- 仓库：https://github.com/PillerBean/SlimeFinder （Public，默认分支 main）
- 发布页：https://github.com/PillerBean/SlimeFinder/releases
- 最新 Release：https://github.com/PillerBean/SlimeFinder/releases/tag/v3.10.12.8
  - `SlimeFinder-3.10.12.8-win64.zip`（15,988,096 B）
  - `SlimeFinder-3.10.12.8-Setup.exe`（11,045,351 B）
  - `SHA256SUMS-3.10.12.8.txt`
- 自动更新接口（公开仓库可直接读）：
  `https://api.github.com/repos/PillerBean/SlimeFinder/releases/latest`
- 本机工具：
  - GitHub CLI：`..\github_tools\gh_dir\bin\gh.exe`
  - 登录 token：`..\github_tools\token.txt`（临时文件，建议后续删除）
  - 历史版本资产收集脚本：`scripts/collect_release_assets.ps1`

### 日常发版流程（以后每版）

```bat
cd /d "D:\DeepSeek harness\史莱姆区块查找器\SlimeFinder"
git add -A
git commit -m "release: v3.10.12.9"
git tag -a v3.10.12.9 -m "SlimeFinder v3.10.12.9"
git push origin main
git push origin v3.10.12.9
powershell -ExecutionPolicy Bypass -File scripts\collect_release_assets.ps1 -Version 3.10.12.9
github_tools\gh_dir\bin\gh.exe release create v3.10.12.9 "release\3.10.12.9\*" --title "SlimeFinder v3.10.12.9" --notes-file docs\release_notes\v3.10.12.9.md
```
