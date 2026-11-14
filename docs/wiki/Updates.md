# 更新与版本规则

## 版本号

所有版本统一四段版本号，例如 `3.10.12.9`；正式版 / 测试版不靠版本号区分。

## 更新方式（v3.12.3 起）

**只提示新版本，不自动下载、不自动安装。**

1. 打开「帮助 → 检查更新」；
2. 若有新版本，只显示版本号和更新说明；
3. 点击 GitHub 发布页（或网盘备用页）手动下载 `Setup.exe`；
4. 核对 Release 中的 `SHA256SUMS-<版本>.txt`；
5. 退出 SlimeFinder，运行 Setup 覆盖安装；
6. 安装会保留 `config.json` 和缓存。

当前没有代码签名，因此不提供自动下载/自动安装。

## GitHub Release 规则

- **正式发布**：不勾 `Pre-release` ⇒ 成为 **Latest**（作为手动下载入口）；
- **测试发布**：勾 `Pre-release` ⇒ 不会成为 Latest；
- 更新提示默认读取 `/releases/latest`（只拿正式版）；
- 历史版本以 `-archive` tag 归档，只提供二进制。

## 检查更新来源

「帮助 → 检查更新」支持：

1. **自动（推荐）**：先试 GitHub 官方；官方不可达时测试内置镜像，选最快的一个；
2. **GitHub 官方**：`api.github.com` + `raw.githubusercontent.com`；
3. **网盘（国内备用）**：蓝奏云页面，只手动打开，不做自动下载。

版本清单地址：

```text
https://raw.githubusercontent.com/PillerBean/SlimeFinder/main/version.json
https://raw.githubusercontent.com/PillerBean/SlimeFinder/main/version-beta.json
```

## 手动更新

1. 打开对应版本的 Release；
2. 在 **Assets** 中下载 `SlimeFinder-<版本>-Setup.exe`；
3. 下载并核对 `SHA256SUMS-<版本>.txt`；
4. 退出 SlimeFinder；
5. 运行 Setup，按向导安装（默认安装到原目录，保留配置与缓存）；
6. 从开始菜单或桌面重新启动。

> 不再发布便携 zip；旧版本 zip 仍可从历史 Release 的 Assets 获取。

## 找不到更新？

- 当前没有比本机更新的正式版时，会提示“已是最新”；
- 若官方不可达，请稍后重试，或在检查更新里使用网盘备用页手动下载；
- 测试版：在检查更新里切到「测试版」通道，或在 Releases 页面选择带 `Pre-release` 标记的版本。
