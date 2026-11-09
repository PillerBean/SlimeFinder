# SlimeFinder v3.5.13 验证报告

- 版本：v3.5.13（`kVersion`、`VERSIONINFO` 3.5.13.0、安装包与 zip 同名）
- 主题：**环形匹配再提速 + 严格统计口径修正**
- 交付物：`dist/SlimeFinder-3.5.13-win64.zip`、`dist/SlimeFinder-3.5.13-Setup.exe`
- 日期：2026-10-18

---

## 一、性能（环形挂机点搜索）

### 1. 计算阶段（`build\slime_tests.exe --bench-ring 400`，400x400 区块，R=128，内=24，Java 种子 12345）

| 阶段 | v3.5.12 | v3.5.13 | 变化 |
|---|---|---|---|
| [1] 群系初始化 | 0.1 ms | 0.1 ms | — |
| [2] 1:64 权重网格 106x106 | 12.4 ms | 8.0 ~ 9.6 ms | 行带并行度 4 → 8 |
| [3] `AfkPlanner::plan` | 47.2 ms | 32.9 ~ 34.8 ms | 粗扫 25.0 → ~18 ms；内核精修 32.1 → **~2 ms** |
| [4] 候选点群系权重（8 个） | **151.7 ms** | **5.3 ~ 6.8 ms** | 按需采样：4880 / 40328 格（12.1%） |
| **合计** | **211.9 ms** | **47.8 ~ 49.6 ms** | **≈4.4x** |

### 2. GUI 端到端（自检日志里的「用时」）

| 场景 | v3.5.12 | v3.5.13 | 变化 |
|---|---|---|---|
| 大范围 400x400 区块 | 0.251 s | **0.093 s** | 2.7x |
| 中等范围（16x16 个 1:64 格 ≈ 43x43 区块） | 0.162 s | **0.052 s** | 3.1x |

### 3. 小/中范围（加权模式改走粗扫路径，`--bench-ring N`）

| 范围 | v3.5.13 前 | v3.5.13 | 最优覆盖 |
|---|---|---|---|
| 12x12 区块 | 40.5 ms | 18.7 ms | 6964（不变） |
| 16x16 区块 | 51.4 ms | 20.0 ms | 7079（不变） |
| 24x24 区块 | 73.1 ms | 18.2 ms | 7303（不变） |
| 32x32 区块 | 112.0 ms | 18.7 ms | 7303（不变） |
| 64x64 区块 | **0 个候选（缺陷）** | 21.9 ms / 8 个候选 | 7705 |
| 100x100 区块 | **0 个候选（缺陷）** | 25.7 ms / 8 个候选 | 8988 |

### 4. 三个提速点

1. **候选点群系权重按需采样**（新类 `slime::BiomeWeightGrid`）：严格统计只会查询「落在史莱姆区块里」的方块，
   一个 71x71 的 1:4 窗口实际只用到约 12% 的格子；旧实现整片 `genArea` 全采。
   采样坐标与原实现逐格相同，因此统计结果逐位不变（`--bench-ring` 内置自检 + 单元测试 [30] 全比对）。
2. **环带核构造改用积分图**（`subOffsetKernels`）：256 个子偏移核本质是同一张方块级环带掩码的 16x16 盒和，
   先建一次掩码积分图后每格只需 4 次查表（旧实现每格 256 次距离判定），单线程约 30 ms → 约 2 ms。
3. **中等范围路由到「区块级粗扫 + 1 格核精修 + 前 12 名抛光」**：实测候选位置与逐格精确解相同或更好，
   速度快 2~8 倍；同时修掉了加权窗口上限造成的「中等范围搜不到」缺陷。

---

## 二、逐格统计口径修正

| 问题 | 现象 | 修正 |
|---|---|---|
| `measureExact` 的 X 轴区块坐标 | 用「相对偏移」算 `chunkX`（Z 轴却是绝对坐标），统计按错位区块网格取样 | 改为绝对方块坐标归属区块，与 `evaluate` / 粗扫核 / 朴素基准一致 |
| 环带边界 | 精确路径按 `(inner-1)²` 扣内圆，核/严格统计按 `inner²` | 统一为 `d² ∈ [inner², outer²]`（整数判定，无浮点误差） |
| 加权窗口上限 | 窗口 1025~2048 的中等范围 Java 版静默返回 0 个站位 | 改走粗扫路径；`plan()` 与 `planChunkCoarse()` 的路由不再互相递归 |

修正后同一站位（种子 12345，(-1611,-796)，R=128，内=24）的有效格数由 **6334 → 9678 格**（旧值按错位网格取样，偏低）。

### 新增回归测试

- **[33] 严格 1 格统计（朴素基准 vs 三条实现）**：逐方块双循环为唯一基准，`evaluate` / `measureExact` / `plan`
  必须完全一致；覆盖 5 个点（含负坐标、正坐标、R=64/128、内=0/24）。
- **[30] 按需采样与整片采样逐格一致**：71x71 窗口全比对，差异 0 处。
- **[32] 内核精修格数 == 逐格统计**：`planChunkCoarse` 的核精修结果必须与 `measureExact` 逐位相同。
- **基准内置校验**：`[校验] 内核精修 vs 逐格统计：完全一致`、`[自检] 按需采样 vs 整片采样：完全一致`。

---

## 三、验证矩阵

| 项目 | 结果 |
|---|---|
| 单元测试（34 组，新增 [33]） | **全部通过 ✔** |
| 参考实现对照（SlimeChecker.exe） | **111 / 111** |
| SIMD 内核独立自检 | **19 项全过（ALL PASS）** |
| GPU 探测 / 一致性 | PASS（失败 0 项） |
| GUI 无界面自检（GPU 关） | exit 0，**172 条断言 0 FAIL** |
| GUI 无界面自检（GPU 开） | exit 0，**全部通过** |
| 打包脚本自检 | `PACKAGE OK`（包内 GUI 自检 exit=0、单元测试组 15 exit=0、内核自检 exit=0） |
| 发布验证 `verify_release.ps1` | **16 / 16 PASS**（clean 重建、单元、参考 111、ocl、内核、GUI×2、两套基准、dist 自检、4 组哈希、zip 自检） |
| 安装包冒烟 | 静默装 → 版本 3.5.13 → 安装副本自检 exit=0 → 卸载清空目录 |
| 污染审计 | **CLEAN**（285 文件，exit 0） |
| dist 与 zip | 条目与逐文件 SHA256 一致 |

### 交付物校验

| 文件 | 大小 | SHA256 |
|---|---|---|
| `SlimeFinder-3.5.13-win64.zip` | 15,822,866 B | `FF6FFBCFB9D915EA1EF7B05CD58BC52E3E2D32537B1EB3856CFF13E2CB86D52B` |
| `SlimeFinder-3.5.13-Setup.exe` | 10,686,194 B | `41424C08FE98FDCFCEA4510F61256919FE52524690CFD72B914D2F39822216C1` |
| `SlimeFinder.exe`（包内） | — | `D16B41842EEBF65C092B4C0C9E3D883449D0E228154EBB7468BB086738B95BC2` |

---

## 四、复现命令

```powershell
powershell -ExecutionPolicy Bypass -File scripts\build_repo.cmd          # 或 cmake --build build
build\slime_tests.exe --unit                                             # 34 组
build\slime_tests.exe --bench-ring 400                                   # 环形分阶段基准
build\slime_kernel_tests.exe                                             # SIMD 内核
py -3 tests\verify_against_reference.py                                   # 111/111
powershell -ExecutionPolicy Bypass -File scripts\gui_selftest.ps1         # 172 条断言
powershell -ExecutionPolicy Bypass -File scripts\package_release.ps1 -Version 3.5.13
powershell -ExecutionPolicy Bypass -File scripts\build_installer.ps1 -Version 3.5.13
powershell -ExecutionPolicy Bypass -File scripts\verify_release.ps1 -Zip dist\SlimeFinder-3.5.13-win64.zip
py -3 scripts\audit_corruption.py
```
