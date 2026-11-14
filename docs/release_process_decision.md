# GitHub 发布流程决策（强制遵守）

> 决策日期：2026-11-10
> 适用范围：从 v3.10.12.11 起的所有 SlimeFinder 源码 / Release / Wiki 发布
> 状态：**强制规范**；本文件优先级高于 `GITHUB_PUBLISH_GUIDE.md` 中的旧流程描述。

## 一、最高原则

> **Git 历史是唯一事实来源。`git push` 不成功，就不发布。**

GitHub API 只允许用于：

- 上传 / 修改 Release 资产与说明；
- 查询、校验 Release；
- 读取文件做验证。

**禁止**用 GitHub API 直接创建 tree/commit 来“替代 git push 上传源码”。  
API 提交不会出现在本地历史里，一旦本地继续提交就会分叉——v3.10.12.11 发布时已经发生过一次，后来用 `git fetch + git reset --hard origin/main` 修复。

## 二、标准发布顺序（必须按序，不得跳步）

### 1. 本地准备

- 工作区干净：`git status --porcelain` 为空；
- 版本号一致：
  - `CMakeLists.txt`
  - `src/main.cpp`（`kVersion` / `applicationVersion`）
  - `src/app.rc`
  - `packaging/windows/install.nsi`
  - `scripts/release/build_installer.ps1`
  - 更新历史、Release Notes、README
- **更新说明必须写明：自 v3.12.3 起只提示新版本，用户需手动下载安装，不自动下载/安装。**
- 跑检查：单元测试、参考对拍、GUI 自检；
- 打包前确认只出 Setup（不再出便携 zip）。

### 2. 同步远端

```bat
git fetch origin
git status
git log --oneline origin/main..HEAD
```

- 本地必须基于最新 `origin/main`；
- 若有本地新提交，先 `git rebase origin/main`；
- 分叉未解决前不得继续发布。

### 3. 提交并推送源码

```bat
git add -A
git commit -m "..."
git push origin main
```

推送必须满足：

- 成功（exit code 0）；
- `git ls-remote origin refs/heads/main` 的 SHA == 本地 `git rev-parse HEAD`。

失败处理：

- 最多重试 5 次，退避 10s / 30s / 60s；
- 仍失败 ⇒ **中止发布**，保留本地提交，等网络恢复；
- 不得改用 API 提交源码。

### 4. 打 tag + 上传 Release

```bat
gh release create vX.Y.Z dist/SlimeFinder-X.Y.Z-Setup.exe dist/SHA256SUMS-X.Y.Z.txt \
  --target <刚推送的源码 SHA> --prerelease --title "..." --notes-file docs/release_notes/vX.Y.Z.md
```

规则：

- 源码 push 成功后才建 tag/Release；
- 测试版必须 `--prerelease`；
- Release 资产只允许：
  - `SlimeFinder-X.Y.Z-Setup.exe`
  - `SHA256SUMS-X.Y.Z.txt`
- 不再上传便携 zip（历史 Release 保持原样）；
- 上传后核对资产大小与 SHA256。

### 5. 发布后生成更新清单

```bat
py -3 scripts/release/gen_version_manifest.py --channel beta  --version X.Y.Z --notes-file ...
py -3 scripts/release/gen_version_manifest.py --channel stable --version X.Y.Z --notes-file ...
```

- **先有 Release 资产，再推送清单**（避免清单指向 404 资产）；
- 清单推送到 `main` 后，核对 raw URL：
  - `version-beta.json`（测试版）
  - `version.json`（正式版）
- 清单必须包含实际资产大小。

### 6. 更新 Wiki

```bat
py -3 scripts/release/gen_version_index.py
powershell -ExecutionPolicy Bypass -File scripts/release/publish_wiki.ps1
```

- Wiki 失败不影响 Release，但必须记录为待补；
- 校验 `https://raw.githubusercontent.com/wiki/PillerBean/SlimeFinder/Version-Index.md` 能读到新版本。

### 7. 最终校验清单

- [ ] 本地 HEAD == `origin/main`
- [ ] Release 为 Pre-release/正式版状态正确
- [ ] Release 资产只有 Setup + SHA256SUMS
- [ ] 资产 digest 与本地一致
- [ ] `version-beta.json` / `version.json` 指向正确 tag
- [ ] 清单资产大小正确
- [ ] Wiki `Version-Index` 含新版本
- [ ] 工作区干净，没有未提交的发布文件

## 三、网络故障处理

| 情况 | 处理 |
|---|---|
| `git push` 超时 | 退避重试；5 次失败即中止发布 |
| GitHub HTTPS 持续不可用 | 可切换 **SSH over 443**（`ssh.github.com:443`）作为备用传输；需先配置 key |
| API 可用、git 不可用 | **不发布**；不使用 API 提交源码 |
| 已经误用 API 提交 | 记录远端 SHA；网络恢复后 `git fetch origin`，再 `git reset --hard origin/main`（或 rebase 本地独有提交）；未同步前禁止继续提交 |
| Wiki push 失败 | 重试；Release 可已完成，但 Wiki 标记待补 |
| Release 资产上传失败 | 保留 tag；修复网络后重传，不重建 tag |

## 四、当前事件记录

- 2026-11-10：v3.10.12.11 发布时 `github.com:443` 的 Git 传输间歇失败；
- 当时错误地用 GitHub API 生成 commit 推了 `main`，本地随后继续提交 ⇒ 历史分叉；
- 网络恢复后执行 `git fetch origin` + `git reset --hard origin/main`，本地已与远端 `b892c38` 一致；
- 该事件促成此强制规范：**后续不再允许 API 源码 fallback**。

## 五、当前状态（2026-11-10 校验）

- 远端 `main`：`b892c381847d9a2fe6f26509727af076f15581d4`
- 本地 `HEAD`：同上
- Release：`v3.10.12.11`（Pre-release）
  - Setup `11,080,008 B`，SHA256 `65404770…EEB3B2`
  - SHA256SUMS `210 B`
- `version-beta.json` → `3.10.12.11`
- Wiki `Version-Index` 已包含 v3.10.12.11
- 无 draft Release、无重复资产、工作区干净
