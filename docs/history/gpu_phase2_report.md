# v3.2.0 GPU 加速 第二阶段（引擎集成）报告

日期：2026-09-08 | 依据《3.2.0.txt 开发计划》第二/四阶段部分目标（CPU/GPU 自动切换、回退、一致性回归）

## 一、交付内容

| 文件 | 说明 |
|------|------|
| `src/core/SlimeEngine.h` | 新增最小接口 `slime::GpuScan`（available + scanRect，纯声明）；
  `SearchOptions::gpuScan`（const GpuScan*，生命周期调用方保证） |
| `src/core/SlimeEngine.cpp` | 经典路径扫描阶段 GPU 分支：
  `runGpuScanStage`（匿名命名空间，行瓦片 2D 切分 ≤256MB/瓦片、缓存覆盖回读、
  新缓存条目收集、进度/取消、失败回退 CPU 且不污染位图）；
  GPU 不可用/区域 <1048576 区块（`SLIME_GPU_MIN_CHUNKS` 可调）/缓存全覆盖时走 CPU |
| `src/gpu/GpuScanAdapter.{h,cpp}` | `slime::GpuScan` 实现（包装 GpuSession，会话内互斥锁串行化——分块路径多线程并发扫描安全） |
| `src/gui/MainWindow.{h,cpp}` | GUI 集成：config.json 新增 `"gpu": {"enabled": true}`（默认开）；
  `SLIME_GPU=0/1` 环境变量强制覆盖；启动预热（一次初始化复用）；搜索注入 gpuScan；日志标注 GPU 扫描 |
| `tests/test_slime.cpp` | 单元 [13] GPU 引擎等价：四场景 CPU vs GPU 完整搜索结果全等 |
| `tools/ocl_probe.cpp` | 新增引擎级完整搜索对比段 |

## 二、验证结果（全部通过）

1. **单元 [13] 引擎等价（结果/数量/totalFound/新缓存条目全等）**
   - Java 密度 2048² ✓；基岩密度 2048² ✓；基岩+缓存覆盖（声明+新条目收集）✓；
     Java 分块 4 块并发 + GPU ✓（分块路径自动获得 GPU：块内 runImpl 走 GPU，互斥串行化设备）
2. **回归**：slime_tests --unit 13/13；与参考 SlimeChecker 111/111；GUI 自检 exit 0（GPU 开/关）；
   GUI 冒烟存活；引擎级 CPU/GPU 结果逐项一致（见下）。
3. **实测（probe 引擎级完整搜索，CPU=12 核全核参考）：**
   | 场景 | CPU 扫描/总 | GPU 扫描/总 | 结果一致 |
   |------|------------|------------|---------|
   | Java 密度 8192²（67M 区块） | 21.6 ms / 1183.9 ms | 95.8 ms / 1217.6 ms | 1,048,576 ✓ |
   | 基岩密度 4096²（16.7M 区块） | 2087.8 ms / 2358.3 ms | **318.8 ms / 563.3 ms** | 885,163 ✓ |

   基岩（判定昂贵）整搜加速 **4.2×**（扫描 6.5×）；Java 密度大区间的耗时主体是**图案匹配**
   （~1.2 s），GPU 扫描已不是瓶颈，总时长持平——匹配墙的解法是第三阶段"CPU 匹配与 GPU 扫描
   重叠的流水线"，届时再报告。小区域（<104 万区块）自动保持 CPU（避免启动开销），
   与计划"GPU 范围>阈值 → GPU 路径"一致。

## 三、设计要点与踩坑

1. **位图坐标必须相对行起点**：GPU 回写网格的 64 位字索引/移位以 rx=x-x0 计算；
   首次实现误用绝对坐标 → 越界写坏内存（fail-fast），单元测试 [13] 即捕获。
2. **失败回退不污染位图**：GPU 判定与 CPU 同一事实源，已写位逐位一致，回退 CPU 时
   按位或语义重扫无副作用；回退有日志（SLIME_DEBUG）。
3. **接口最小化**：core 不依赖 slime_gpu（仅前向声明 + 指针注入）；GpuScan::scanRect 为
   const（实现内部缓冲可变），引擎只读使用。
4. **阈值**：默认启用下限 1<<20 区块（SLIME_GPU_MIN_CHUNKS）；瓦片预算 256MB
   （SLIME_GPU_TILE_BYTES）；两者均可测试调参。
5. **缓存语义不变**：GPU 路径对缓存覆盖区间回读缓存、对未覆盖区间收集新史莱姆，
   与 CPU 路径逐条一致（含分块增量缓存场景）。

## 四、遗留（第三阶段起）

- 匹配墙优化：CPU/GPU 流水线（异步双缓冲）；经典路径扫描可进一步并行化读回转换；
- 基岩内核 LDS/布局优化（现 5600 万/s 已超 5000 万目标，作性能余量）；
- estimateSearch 仍按 CPU 速率估算（GPU 实跑更快 → 估算偏保守，文案可注明"GPU 启用时更快"）；
- v3.2.0 品牌/打包/正式文档（用户手册 GPU 章节、architecture、build_instructions）在第四阶段统一。

原始运行输出：`bench_baseline_v300/gpu_engine_phase2_run1.txt`、`gui_selftest_phase2.txt`
