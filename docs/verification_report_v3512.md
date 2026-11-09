# SlimeFinder v3.5.12 验证报告

- 版本：v3.5.12（kVersion、VERSIONINFO 3.5.12.0、安装包与 zip 同名）
- 日期：2026-10-18
- 交付物：`dist/SlimeFinder-3.5.12-win64.zip`、`dist/SlimeFinder-3.5.12-Setup.exe`

## 1. 本版变更

### 1.1 环形搜索提速（用户要求：更快且质量不回退）
- 粗扫由「逐候选卷积」改为**散射 + 16 条行带并行**：每个区块只算一次 `score`（4 个网格子偏移各累加），整数化权重避免浮点误差。
- 128×128（区块）卷积核按 (R, inner) 缓存，OpenMP 多线程构建；精修改为**核局部精修**（3×3 区块 × 4 子偏移 = 36 个站位/候选），再按多项式插值取前 12 名做 1 格精度抛光。
- 精修/抛光/核构建全部 OpenMP 并行，命中表并行合并。
- **质量**：最优覆盖 6327 → **6334** 格（更优），±20 格小范围仍为 5941 格（与原版一致，差 1 格为 1:4 取样边界差异）。
- **速度（GUI 实测，400×400 区块）**：0.871s → **0.251s**（3.5×）；小范围 ±20 格：0.598s → **0.26s**（2.3×）。
- **分段基准（`--bench-ring 400`）**：plan 434.2ms → **45.8ms**（粗扫 95.5 → 23.0ms、精修 158.4 → 9.2ms、抛光 11.1ms、查询 5.9ms）。

### 1.2 群系地图采样提速 + 修复脏数据 bug
- `BiomeMap::genArea` 行带并行时**禁止浅拷贝 Generator**（cubiomes 内部指针会指向复制体栈帧 → 脏群系色块）。改为每个工作线程自建 Generator + 各自 `setupGenerator`，主线程与线程间均安全。
- 1:4 采样 13 万格：397~409ms → **85ms**（4.7×）。地图拖动/缩放不再卡主线程；环形搜索统计里的「0.6s 异常慢」也随之消失。

### 1.3 崩溃日志噪声（自检里长期存在的「假崩溃」）
- VEH 现在直接放过良性首次机会异常：`0x40010006/0x4001000A/0x4001000B`（OutputDebugString）、`0xE06D7363`（MSVC C++ 异常）、`0x406D1388`（SetThreadName）。
- 自检 stderr 从 807 行降到 **353 行**，`[CRASH]` 从 24+ 条降到 **0**；高并发阶段不再被栈回溯拖慢。

### 1.4 打包健壮性（本版踩到的两个坑）
- **缺 offscreen/minimal 平台插件**：windeployqt 只部署了 `qwindows.dll`，无头自检（`QT_QPA_PLATFORM=offscreen`）会卡在不可见的错误框上把打包挂死 20+ 分钟。`package_release.ps1` 现在强制补 `platforms/qoffscreen.dll`、`qminimal.dll`。
- **自检无超时**：`Start-Process -Wait` 改为 `WaitForExit(240000)`，超时即 kill 并记失败，打包不再无限挂起；同时 `$p.Refresh()` 后再读 ExitCode，避免空值误判。

### 1.5 继承修复（v3.5.11）
- 「最小密度」只做筛选：两处 `if (opt.minDensity > 0.0)` 分支改为 `return pickSpread(filtered, opt.topN, opt.minSeparation);`，结果数量重新受「目标数量」约束（用户案例：64 条 → 8 条）。

## 2. 验证矩阵

| 项 | 结果 | 说明 |
|---|---|---|
| clean rebuild | PASS | 0 error |
| 单元测试 `--unit` | PASS | 全部组通过（含 [32] 600×600 大范围 0.44s、步长 1） |
| 参考实现对照 | PASS | 111/111 |
| OpenCL 探针 | PASS | 失败 0 项 |
| SIMD 内核自检 | PASS | RESULT: ALL PASS |
| GUI 自检（GPU 关） | PASS | **172 条断言、0 FAIL、0 崩溃噪声** |
| GUI 自检（GPU 开） | PASS | 同上 |
| `verify_release.ps1` | PASS | **16/16** |
| 包内自检（zip 解压后） | PASS | in-package GUI self-test exit=0 / unit exit=0 / kernel exit=0 |
| 安装包冒烟 | PASS | 静默装 → 版本 3.5.12 → 安装副本自检 FAIL=0 → 卸载清空 |
| 审计 | CLEAN | 284 文件无损坏特征 |
| dist 与 zip | 一致 | 32 条目逐文件 SHA256 相同 |

## 3. 交付哈希

| 文件 | 大小 (B) | SHA256 |
|---|---|---|
| `SlimeFinder-3.5.12-win64.zip` | 15,301,275 | `875EFF3B311F4853557953CAF9C6528A2CF0DA07C81A1F4C3DBA33B6423C2CB4` |
| `SlimeFinder-3.5.12-Setup.exe` | 10,681,509 | `62D71B3D01B5681E1F6339B8F2E289F10839798752C0B8280C8A218574D192B3` |
| `SlimeFinder.exe` | 2,487,808 | `1DB265E360379FD2D70D6973B4E05023B9B8FFADF78F3D408F995FB552491983` |

## 4. 已知限制

- 环形大范围（> 2048×2048 格窗口）走散射粗扫 + 核精修，属高精度近似；小范围自动回落精确逐格路径。
- 「有效格 · 总权重」的口径：按游戏最小单位 1:4 群系网格（每格 16 方块）严格逐格；只有前 8 名做全量重算，其余沿用 planner 估算值。
- 基岩版不提供群系权重（游戏无对应生成占比），平台切到基岩版时权重列显示为面积口径。
- 结果表最多显示 500 条；环形范围上限 4096×4096 区块。
- 无头（offscreen）自检依赖 `platforms/qoffscreen.dll`，安装包与 zip 均已内置。

## 5. 复现命令

```bat
scripts\build_repo.cmd
build\slime_tests.exe --unit
build\slime_tests.exe --bench-ring 400
py -3 tests\verify_against_reference.py
build\slime_kernel_tests.exe
powershell -ExecutionPolicy Bypass -File scripts\gui_selftest.ps1 -LogDir C:\Temp\gui_check
powershell -ExecutionPolicy Bypass -File scripts\package_release.ps1 -Version 3.5.12
powershell -ExecutionPolicy Bypass -File scripts\build_installer.ps1 -Version 3.5.12
powershell -ExecutionPolicy Bypass -File scripts\verify_release.ps1 -Zip dist\SlimeFinder-3.5.12-win64.zip
```
