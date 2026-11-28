# 开发循环与门禁策略（2026-11-28 用户规定）

用户明确要求：**测试版开发可以省略一些“耗时且不容易出错”的自检**，加快迭代速度。
同时：**本地部署目录必须始终保持最新开发构建**——这是用户重要的手工检验路径。

## 1. 本地部署目录（必须最新）

```text
D:\DeepSeek harness\史莱姆区块查找器\SlimeFinder\dist\SlimeFinder\SlimeFinder.exe
```

- 这是用户的“重要检验路径”，**每轮开发构建后必须同步**，无论版本号是什么、是否已发布。
- 同步脚本（自动从 worktree 定位主仓库部署目录，保留 config/cache/logs/crashes，不发布任何东西）：

```powershell
# 在任意 worktree 根目录执行
powershell -ExecutionPolicy Bypass -File scripts\release\sync_local_deploy.ps1
```

- 同步内容：`SlimeFinder.exe`、`slime_bedrock_biome_query.exe`、`slime_tests.exe`、`slime_ocl_probe.exe`、
  `slime_kernel_tests.exe`、`assets\i18n`、`assets\help`、`build_date.txt`；并逐个校验 SHA256。
- 若部署目录里的 exe 正在运行（常见：用户正在手工测试），Windows 不允许覆盖；脚本会把旧 exe
  改名为 `SlimeFinder.exe.old-<时间戳>` 再写入新 exe（运行中的进程继续用旧文件），
  然后提示“关闭并重新打开以使用新构建”。
- 快速门禁脚本默认在最后自动执行本同步（`-NoDeploy` 可跳过）。

## 2. 两级门禁

### 2.1 测试版开发快速门禁（默认，每轮改动后）

```powershell
powershell -ExecutionPolicy Bypass -File scripts\qa\check_fast.ps1
```

包含 4 步（通常十几秒到 1–2 分钟，视增量编译量）：

1. 增量构建 `scripts\build\build_repo.cmd`
2. `build\slime_tests.exe --unit`
3. `py -3 scripts\qa\check_ui_text.py`（三语 key/占位符一致）
4. `scripts\release\sync_local_deploy.ps1`（部署目录保持最新）

适用：GUI 文案/布局微调、小逻辑改动、纯后端小改动；**不跑 GUI 自检、参考对拍、打包验证**。

### 2.2 发布前全量门禁（只在打包/发版前跑）

```powershell
cmd /c scripts\build\build_repo.cmd
build\slime_tests.exe --unit
py -3 tests\reference\verify_against_reference.py          # 117 项
powershell -File scripts\qa\gui_selftest.ps1               # GPU 关
powershell -File scripts\qa\gui_selftest.ps1 -GpuOn        # GPU 开
py -3 scripts\qa\check_ui_text.py
build\slime_kernel_tests.exe --correct-only
# 打包/安装包/发布另见 docs/release_process_decision.md（且必须获得用户明确同意）
```

适用：发布测试版之前、改动引擎/缓存/GPU/环形算法之后、改动 GUI 结构或自检断言较多之后。

## 3. 什么时候必须升级到全量门禁

以下改动即使只是“测试版迭代”，也建议至少跑一次 GUI 自检（GPU 关即可）+ `--unit`：

- 改 GUI 布局层级/可见性逻辑/自检断言/i18n key；
- 改环形搜索 worker、缓存路径、GPU 路径、结果列表列结构；
- 改了 `MainWindowSelfTest.inc` 的断言（自检本身也要能跑通）。

## 4. 已固定到仓库的脚本

```text
scripts/release/sync_local_deploy.ps1   # 部署目录同步（含运行中 exe 的改名替换策略）
scripts/qa/check_fast.ps1               # 测试版快速门禁（build + unit + i18n + deploy）
```

两个脚本均为 ASCII（Windows PowerShell 5.1 兼容），路径一律由脚本自身位置/ git 推导，不含硬编码中文路径。

## 5. 背景

- 2026-11-28：用户发现 `dist\SlimeFinder\SlimeFinder.exe` 未随最新开发构建更新；
  同时反馈开发速度慢、要求测试版可省去耗时且不易出错的自检。本文件即该规定的落地。
