# src/gpu/experimental —— GPU 密度匹配内核（v3.4.0 起点，v3.3.1 不参与构建）

> 状态：**开发中/未完成**。按 v3.3.1 交付决定，本目录**不加入 CMake 目标**，不参与
> `slime_gpu` 的编译，也不会被 `SlimeFinder.exe` / `slime_tests.exe` 链接。
> 保留原因：作为 v3.4.0「GPU 密度匹配内核」的起点，避免重复实现。

## 文件说明（均为 2026-09-20 中断时的原始快照，未做清理）

| 文件 | 内容 | 说明 |
|---|---|---|
| `slime_kernel.cl.wip` | OpenCL 内核（扫描 + 密度三段流水线） | 含 `scan_java_bitmap` / `scan_bedrock_bitmap` / `density_count_emit` 等密度内核；扫描内核 `scan_java_slime` / `scan_bedrock_slime` 为 v3.3.0 起长期验证通过的实现 |
| `GpuSession.h.wip` / `GpuSession.cpp.wip` | 主机侧会话（设备/上下文/队列/内核编译/扫描/密度匹配/分块） | `matchDensity()` 主流程、按显存自动分块（上限 512MB）、取消检查、`DensityDebugDump` 诊断都在这两个文件里 |
| `GpuScanAdapter.cpp.wip` | 引擎适配器（`scanRect` + `matchDensity` override） | `matchDensity` 把 `slime::DensityMatchParams` 转成 `DensityMatchRequest`，命中密度按 `(float)count/(float)(g*g)` 回填 |
| `gpu_density_selftest.cpp` | 独立自检/基准程序（不依赖 CMake） | 含内核中间量比对、位图/段计数逐位比对、一致性用例、取消延迟、maxResults 超限、吞吐与全图外推、与引擎 `run()` 对照 |

## 中断时的已知问题（v3.4.0 需先解决）

1. **Java 位图内核不正确**：自检 `[0a]/[0]` 显示 Java 向量化判定路径与引擎不符
   （`lcg自洽=0`、位图数千处不一致）；Bedrock 位图一致。**建议直接回退到逐区块标量 LCG**
   （v3.3.0 内核里的 `java_is_slime`，已从 v3.3.0 交付二进制中提取备份）。
2. **`GpuSession::initImpl` 崩溃（0xC0000005 / 0xC0000374）——根因已定位，属构建期缺陷，非本目录算法问题**。
   v3.3.1 排查结论（两个缺陷均已修复并加入回归，详见 `docs/verification_report_v331.md` §5.3）：
   * `scripts/embed_kernel.cmake` 旧实现在每 24 字节换行处写入 C 转义 `\n`（而非仅源码换行），
     把换行**注入到内核字符串内容**里 → 注释被截断成代码，OpenCL 编译器运行期报
     `unknown type name` / `not valid UTF-8`（`CL_BUILD_PROGRAM_FAILURE`）；
   * 中文版 cl.exe 的 `/showIncludes` 前缀（GBK 字节）与 Ninja 的 `msvc_deps_prefix` 不匹配
     → Ninja **静默丢弃全部头文件依赖**（`ninja -t deps` 显示 `#deps 0`）→ 改头文件不重编译，
     `slime_gpu`/`slime_tests` 长期使用陈旧对象（虚表/结构体布局不一致）→ 表现为
     `GpuSession::initImpl` / `runScan` 范围内的随机 `0xC0000005` 与驱动侧堆损坏。
   **修复后的实测**：扫描内核编译正常，`slime_ocl_probe.exe` 一致性 + 基准全部 PASS（exit 0；
   Java 16384² 27.1 ms ≈ 99.2 亿区块/秒，Bedrock 2048² 71.8 ms），
   `slime_tests.exe --unit 13/17/18` 均 exit 0。**因此本目录的 `.wip` 在 v3.4.0 中应先按上述
   两个前提（干净重建 + 嵌入校验）复核，再怀疑它自己的新增代码。**
3. **`GpuSession.cpp.wip` 末尾存在未完成的编译错误**（`buildProgram()` 里
   `const KernelRef densityKernels[] = {...}` 附近的半成品改动，另有 `bx1`/`local` 未声明）——中断时正在修改。
4. ~~自检程序曾出现 `0xC0000374`（堆损坏）~~ —— 见第 2 条：该现象随上述构建期缺陷一并消失；
   若在 v3.4.0 复现，请优先检查密度路径的缓冲区尺寸（结果缓冲、去重表、位图行列宽）与
   多线程下的 `GpuSession` 互斥范围，并始终用 `--clean-first` 全量重建后复测。

## 如何在 v3.4.0 中继续

1. 把 `.wip` 文件复制回 `src/gpu/`（去掉 `.wip` 后缀），把 `slime_kernel.cl.wip` 中
   密度内核保留、扫描内核与 `src/gpu/slime_kernel.cl` 合并（两者应保持同源）。
2. 在 `CMakeLists.txt` 里恢复密度自检目标：
   ```cmake
   add_executable(slime_gpu_density_tests tests/gpu_density_selftest.cpp)   # 或直接放本目录
   target_include_directories(slime_gpu_density_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
   target_link_libraries(slime_gpu_density_tests PRIVATE slime_core slime_gpu)
   ```
3. 引擎侧接口与调用点**已经就绪且保持不变**（v3.3.1 已交付，恒走 CPU 回退）：
   - `src/core/SlimeEngine.h`：`slime::GpuScan::matchDensity()` 与 `DensityMatchParams`
     （默认实现返回 `false` = 不支持）；
   - `src/core/SlimeEngine.cpp`：`runImpl` 阶段 1a 的调用点（条件、`SLIME_GPU_MATCH=0` 开关、
     TopN 入堆与取消处理都已写好）；
   - 单元测试 `[18]`（模拟提供者：堆序/回退/启用条件/多分块锚点划分）已通过，
     可作为 v3.4.0 的回归基线；`[17]`（真实 GPU 对照）也会在实现 `matchDensity` 后自动生效。
4. 目标与验收（任务书 3.2 原文）：结果与 CPU 100% 一致、GPU 不可用自动回退、111/111、
   自动分块 ≤512MB 且单块可取消 <1s、全图密度匹配尽量压缩到 20 分钟内。
