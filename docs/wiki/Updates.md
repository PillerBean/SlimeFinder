# 自动更新与版本规则

## 版本号

所有版本统一四段，例如 `3.10.12.9`；正式 / 测试不靠版本号区分。

## GitHub Release 规则

- **正式发布**：不勾 `Pre-release` ⇒ 成为 **Latest**；
- **测试发布**：勾 `Pre-release` ⇒ 不会成为 Latest；
- 自动更新默认读取 `/releases/latest`（只拿正式版）；
- 历史版本以 `-archive` tag 归档，只提供二进制，源码自 3.10.12.9 起保留。

## 程序内「检查更新」

帮助 → 检查更新（GitHub/镜像），支持：

1. **自动（推荐）**：先试 GitHub 官方；官方不可达时，自动对内置镜像
   （gh-proxy / ghfast / gh-proxy.org 等）测速，选最快的继续检查与下载；
2. **GitHub 官方**：只走 `api.github.com` + `raw.githubusercontent.com`；
3. **网盘（国内备用）**：蓝奏云页面，仅手动下载，不做自动更新。

镜像回退使用仓库根目录的 `version.json` 清单（raw 地址可被代理加速）。
下载更新包后会使用 Release 的 `SHA256SUMS` 校验，校验失败自动删除。

## 手动更新

1. 从 Releases 下载对应版本的 zip；
2. 退出 SlimeFinder；
3. 解压覆盖原程序目录（保留 `config.json` 与缓存文件即可）；
4. 重新启动。

## 找不到更新？

- 当前没有比本机更新的正式版时，会提示“已是最新”；
- 若官方不可达，请稍后重试，或在检查更新里使用网盘备用页手动下载；
- 测试版需要在 Releases 页面自行选择（带 `Pre-release` 标记）。
