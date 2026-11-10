# 自动更新与版本规则

## 版本号

所有版本统一四段，例如 `3.10.12.9`；正式 / 测试不靠版本号区分。

## GitHub Release 规则

- **正式发布**：不勾 `Pre-release` ⇒ 成为 **Latest**；
- **测试发布**：勾 `Pre-release` ⇒ 不会成为 Latest；
- 自动更新默认读取 `/releases/latest`（只拿正式版）；
- 历史版本以 `-archive` tag 归档，只提供二进制，源码自 3.10.12.9 起保留。

## 程序内「检查更新」

帮助 → 检查更新（GitHub/镜像）：

1. **更新通道**：
   - **稳定版（默认）**：只提示正式版（`version.json`）；
   - **测试版**：还会提示 Pre-release（`version-beta.json`），当前测试版为 v3.11.2；
2. **来源**：
   - 自动（推荐）：先试 GitHub 官方；官方不可达时对内置镜像测速，选最快的继续检查与下载；
   - GitHub 官方：`api.github.com` + `raw.githubusercontent.com`；
   - 网盘（国内备用）：蓝奏云页面，仅手动获取，不做自动更新；
3. **下载与安装**：
   - 只下载 Setup；下载后按 Release 的 `SHA256SUMS` 校验；
   - 校验通过 → 提示关闭程序并打开安装向导；
   - 安装时带 `/UPDATE`，会保留 `config.json` 与缓存；用户点“下一步”完成覆盖；
   - 安装完成后可从完成页直接启动新版。

清单地址：`https://raw.githubusercontent.com/PillerBean/SlimeFinder/main/version.json`（稳定）/
`version-beta.json`（测试）。

## 手动更新

1. 打开对应版本的 Release，在 **Assets** 中下载 `SlimeFinder-<版本>-Setup.exe`；
2. 按需下载 `SHA256SUMS-<版本>.txt` 核对；
3. 退出 SlimeFinder；
4. 运行 Setup，按向导安装（默认会安装到原目录；安装器会保留 config/缓存）；
5. 从开始菜单/桌面重新启动。

> 不再发布便携 zip；旧版本仍可从历史 Release 的 Assets 获取 zip。

## 找不到更新？

- 当前没有比本机更新的正式版时，会提示“已是最新”；
- 若官方不可达，请稍后重试，或在检查更新里使用网盘备用页手动下载；
- 测试版：在检查更新里切到「测试版」通道，或在 Releases 页面选择带 `Pre-release` 标记的版本。
