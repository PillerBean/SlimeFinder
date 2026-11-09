# v3.2.0 验证报告（GPU 加速版）

日期：2026-09-08 | 机器：AMD Ryzen 5 5600（6C/12T）+ **AMD Radeon RX 6750 GRE 10GB**（gfx1031）
依据：《3.2.0.txt 开发计划》（已取消网格地图）

## 一、回归验证（CPU 路径语义零改动，全部通过）

| 验证 | 命令 | 结果 |
|---|---|---|
| 单元测试（13 组） | `slime_tests.exe --unit` | 13/13 ✔（[13] GPU 引擎等价：Java/基岩/缓存覆盖/分块四场景 CPU↔GPU 结果、总数、新缓存条目全等） |
| 与参考实现对照 | `verify_against_reference.py SlimeChecker.exe slime_tests.exe` | **111/111** ✔ |
| GUI 无界面自检（GPU 开/关各多次） | `SLIME_SELFTEST=1 SlimeFinder.exe` | exit 0 ✔（无偶发） |
| GUI 冒烟 | 启动 5s 存活 | ✔ |
| GPU 字节级一致 | `slime_ocl_probe.exe`（9 组矩形全量比对 + 大矩形） | mismatch=0 ✔ |
| GPU 不可用回退 | 无设备时 opt.gpuScan 为空/引擎自动 CPU | CPU 路径行为不变 ✔ |
| 缓存功能 | 单元 [5][7][10][12] + GPU 缓存覆盖用例 | ✔（GPU 不改变缓存语义） |

## 二、性能基准（v3.1.1 vs v3.2.0，Release，本机）

### 2.1 扫描（纯判定）
| 版本 | v3.1.1 CPU 单核 | v3.2.0 GPU（端到端含回读） | GPU/单核 |
|---|---|---|---|
| Java | ≈5.3 亿区块/秒 | **≈110 亿区块/秒**（16384²=268M 区块 24ms） | ≈ 20× |
| 基岩 MT19937 | ≈130 万区块/秒 | **≈5600 万区块/秒**（2048² 74ms） | ≈ 40× |

### 2.2 完整搜索（UI 现实口径：最近 50 个结果）
| 场景 | v3.1.1 | v3.2.0 CPU(并行匹配) | v3.2.0 GPU | 提升(GPU) |
|---|---|---|---|---|
| Java 密度 g=2 8192²（67M 区块） | ≈1184 ms（匹配单线程） | ≈129 ms | ≈225 ms（GPU 扫描含回读/启动，扫描非瓶颈） | ≈9× |
| 基岩密度 g=2 4096²（16.7M 区块） | ≈2358 ms | ≈2186 ms | **≈352 ms** | **≈6.7×** |
| Java 密度 8192² CPU 扫描 | — | 26 ms（12T） | — | — |
| 基岩扫描 4096² | 2151 ms（12T） | — | 319 ms | ≈6.7× |

### 2.3 计划性能目标对照（诚实结论）
| 计划目标 | 实测 | 结论 |
|---|---|---|
| Java GPU ≥1 亿区块/秒 | ≈110 亿区块/秒 | ✔ 远超（注：本机 CPU 单核已达 ≈5.3 亿/秒，GPU 相对 CPU 单核 ≈20×） |
| 基岩 GPU ≥5000 万区块/秒 | ≈5600 万区块/秒 | ✔ |
| 小范围 GPU ≥ CPU×5（1000×1000） | GPU/CPU1 ≈13×、GPU/CPU12 ≈5.1× | ✔ |
| 全图（Java ±1,875,000）搜索 ≤30 分钟 | 判定扫描（GPU）：约 1.4 万亿区块 ≈ 2~4 分钟 ✔；
  含图案/密度**匹配**（窗口 ≈ 区块数，12 核并行后仍 ~3~6 小时量级） | ✘（结构性不可达，见下） |
| CPU 性能不回退（GPU 不可用时） | CPU 路径零改动；匹配并行使 CPU 总时长普遍 ↓（Java 8192² ≈9×） | ✔ |

**全图 ≤30 分钟的结构性说明**：该目标需"窗口数≈区块数（1.4 万亿）的评估"在 30 分钟内完成，
即 ≥78 亿窗口/秒。12 核并行匹配实测 ≈5 亿窗口/秒量级（g=2 密度，每窗口 ~4 次行段
popcount）——差 2 个数量级；本机把"整图仅判定扫描"与"整图图案搜索"混同。计划 §10 自身
亦承认 1 亿/秒 GPU 低于 CPU 单核且需 10 亿/秒量级 GPU 才可能 30 分钟内扫完（仍未计匹配）。
v3.2.0 实际收益落在：基岩大范围整搜 ~7×、Java 匹配墙 ~9×、基岩缓存补扫大幅缩短——
均经完整搜索端到端验证；GUI 的整图确认框按实测速率动态估算并如实展示。

## 三、交付物清单

| 交付 | 位置 |
|---|---|
| 源码（含 OpenCL 内核） | `src/`（`core/gpu/gui/utils`）+ `src/gpu/slime_kernel.cl` |
| CMakeLists（含 OpenCL 查找说明/嵌入步骤） | `CMakeLists.txt`、`scripts/embed_kernel.cmake` |
| Khronos OpenCL 头文件 | `third_party/OpenCL-Headers/CL/`（仅类型/常量） |
| SlimeFinder.exe / slime_tests.exe / slime_ocl_probe.exe | `build/` |
| 可分发目录 / ZIP | `dist/SlimeFinder/`、`dist/SlimeFinder-3.2.0-win64.zip` |
| 文档 | `docs/user_manual.md`（GPU 章节）、`docs/architecture.md`（§9）、
  `docs/build_instructions.md`、`docs/gpu_phase1_report.md`、`docs/gpu_phase2_report.md`、本报告 |
| 原始运行输出 | `bench_baseline_v300/gpu_*`、`gui_selftest_phase2.txt` |

## 四、OpenCL 运行时依赖说明
- 无 SDK：构建仅用随源码 Khronos 头文件；运行期动态加载系统 OpenCL。
- GPU 加速为**可选增强**：无 GPU/驱动缺失/显存不足/内核编译失败时自动回退 CPU，
  结果逐位一致；相关状态记入 logs。
- 排障开关：`SLIME_GPU=0/1`、`SLIME_GPU_DEBUG=1`、`SLIME_GPU_MODULE=<icd路径>`、
  引擎阈值 `SLIME_GPU_MIN_CHUNKS` / 瓦片 `SLIME_GPU_TILE_BYTES`。
