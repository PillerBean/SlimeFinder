# v3.2.0 GPU 加速 第一阶段（可行性）报告

日期：2026-09-08 | 硬件：AMD Radeon RX 6750 GRE 10GB（gfx1031 / Navi22，18 CU? 驱动报告 18 计算单元）
驱动：3652.0（AMD-APP OpenCL 2.0/2.1，PAL 后端）| 依据《3.2.0.txt 开发计划》

## 一、交付内容

| 交付 | 说明 |
|------|------|
| `third_party/OpenCL-Headers/` | Khronos 官方头文件（仅类型/常量来源，不链接） |
| `src/gpu/ClApi.h/.cpp` | OpenCL 动态加载层：LoadLibrary + 函数指针表（22 个 1.2 API），无需 OpenCL SDK/导入库 |
| `src/gpu/GpuSession.h/.cpp` | 设备发现（多候选模块：系统加载器 → 注册表 ICD → DriverStore AMD → System32 已知名）、上下文/队列、内核编译、矩形扫描 API |
| `src/gpu/slime_kernel.cl` | Java LCG + 基岩 MT19937 判定内核（逐位移植） |
| `scripts/embed_kernel.cmake` | 构建期将 .cl 嵌入 C++ 原始字符串（单一事实来源） |
| `tools/ocl_probe.cpp` | 独立 CLI 工具：设备检测 + 一致性验证 + 性能基准（构建产物 `slime_ocl_probe.exe`） |

## 二、架构要点与踩坑记录

1. **无需 SDK**：Khronos 头文件仅供编译期类型/常量；运行期动态加载 opencl.dll（ICD 加载器）。
   直连 ICD 回退链解决"注册表 Vendors 键缺失/损坏"机器（本机注册表仅有失效的 GeForce 210 的
   nvopencl，AMD 运行时只存在于 DriverStore 且未注册 → 加载器自身按厂商惯例找到 AMD 平台）。
2. **关键 Bug（已修复）**：`ClApi` 结构含裸 HMODULE 指针；默认移动赋值是"复制"语义，
   导致 `bestApi = std::move(trial)` 后源对象 `closeModule()` 把仍被持有的 DLL 卸载，
   之后一切调用随机崩溃（0xC0000005，崩溃地址指向已卸载模块入口）。
   已实现显式移动构造/赋值（源对象 module 置空）。同类结构务必自查移动语义。
3. **同驱动多实例**：进程内同时加载 ICD 加载器 + 多次直连同一 amdocl64 会破坏驱动运行时状态；
   策略为"获得可用 GPU 即停止扫描"。
4. 环境变量调试开关 `SLIME_GPU_DEBUG=1`（stderr 输出）、模块强指 `SLIME_GPU_MODULE=<dll 路径>`。
5. 本机 AMD OpenCL 平台经 System32 的 AMD 加载器（= 驱动目录内 AMD_OpenCL64.dll，3.0.6.0）获得，
   设备名 `gfx1031`，OpenCL 2.0（AMD-APP 3652.0）。如需长期注册：在
   `HKLM\SOFTWARE\Khronos\OpenCL\Vendors` 增加 DriverStore 的 amdocl64.dll 值（本环境无提权未能写入）。

## 三、一致性验证（GPU vs CPU，逐位）

CPU 参照 = `SlimeEngine::isSlimeChunk*`（已与参考 SlimeChecker.exe 111/111 对齐）。
全部 **mismatch = 0**：

- Java：seed=0 原点 1024²；seed=12345 远坐标非整幂宽 777×333；大种子 -987654321098765 于
  (1048576,-1048576) 2048²；超大坐标 ±2²⁵ 400×300；再 16.8M 区块大矩形全量比对。
- 基岩：原点 1024²；( -1000000, 2000000) 777×333；超大坐标 ±2²⁵；再 1.05M 区块全量比对。
- 史莱姆密度（GPU 计数 == CPU 计数）均一致，如 Java 268M 区块 26850820（≈10%）。

## 四、性能基准（RX 6750 GRE，端到端含回读）

| 场景 | GPU | CPU 单核 | CPU 12 线程 | GPU/单核 | GPU/12T |
|------|-----|---------|------------|----------|---------|
| Java 268M 区块（16384²） | **110.9 亿/s**（24.2 ms/次） | 2.83 亿/s | 14.6 亿/s | 39.2x | 7.57x |
| Java 1M 区块（1000×1000，计划验收项） | 38.0 亿/s | 2.81 亿/s | 7.5 亿/s | 13.5x | 5.06x |
| 基岩 4.19M 区块（2048²） | **5602 万/s**（74.9 ms/次） | 84 万/s | 676 万/s | 66.6x | 8.29x |

注：probe 的 CPU 口径为逐块 `isSlimeChunk*` 调用 + 计数（含取模解码），
与引擎内向量化扫描实测值（Java 单核 ~5.3 亿/s）不同口径，比率仅供相对比较。

## 五、计划验收对照（第一阶段）

- ☑ 能编译 OpenCL 测试程序（无 SDK 依赖）
- ☑ 独立 CLI 测试工具能检测 GPU 设备（列出平台/设备参数）
- ☑ Java LCG 判定内核与 CPU 一致（全量比对）
- ☑ 基岩 MT19937 判定内核与 CPU 一致（全量比对）
- ☑ 小范围 GPU 扫描（1000×1000）：GPU ≥ CPU×5（实测 13.5x 单核 / 5.06x 12 线程；更大范围 39x）
- 额外：Java GPU 110 亿/s、基岩 GPU 5600 万/s，均已超过计划终期目标
  （Java ≥1 亿/s、基岩 ≥5000 万/s）——但需注意 CPU 单核已达 5.3 亿/s（Java），
  纯 GPU 对 Java 的绝对优势需在"CPU+GPU 流水线"架构下才能完全兑现（计划 4.3/10.3 已述）。

## 六、遗留与后续（第二阶段起）

1. 内核仍为"每区块全量重算"（Java 每区块重算 rowBase/xtail）；后续可按行预计算/预置表优化。
2. 基岩内核为每工作项私有 624 字数组（编译器会溢出到本地内存），仍达 5600 万/s；
   计划 3.2 阶段可试验 LDS 分片/转置布局等进一步提升。
3. 引擎集成：GpuSession 扫描与 CPU 结果缓冲格式（1B/区块 vs 引擎位图 words）的转换层，
   异步流水线（GPU 扫块 + CPU 匹配重叠）、进度/取消、范围阈值自动切换、失败自动回退 CPU。
4. 计划文档 §10 存在算术内部矛盾（1 亿/s Java GPU 目标低于本机 CPU 单核 5.3 亿/s；
   "全图 ≤30 分钟"需 ~78 亿/s 综合吞吐），建议按实测数据在集成阶段重新校准验收口径。

原始运行输出存档：`bench_baseline_v300/gpu_probe_phase1_run1.txt`
