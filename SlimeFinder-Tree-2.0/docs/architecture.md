# 代码架构说明（architecture.md）

> 史莱姆区块查找器（Slime Chunk Finder）v1.0.0 — 纯 C++/Qt 6 重构版
> 依据《史莱姆区块查找器 – C++/Qt 重构任务书》v1.0 实现；参考实现为
> `SlimeChecker.exe` / `main.cpp`（判定算法）与 `史莱姆区块查找.py` v2.9.1（GUI 行为语义）。

---

## 1. 目录结构

```
SlimeFinder/
├── CMakeLists.txt              # CMake ≥ 3.16；Release/Debug；OpenMP；Qt6 Widgets
├── README.md                   # 快速开始
├── docs/
│   ├── architecture.md         # 本文档
│   ├── user_manual.md          # 用户使用说明
│   └── build_instructions.md   # 编译/打包指南
├── src/
│   ├── main.cpp                # 程序入口（崩溃转储、日志、主窗口）
│   ├── core/                   # 纯 C++20 计算核心（无任何 Qt 依赖）
│   │   ├── RandomGenerators.h/.cpp   # JavaRandom / MersenneTwister / 溢出乘法
│   │   ├── SlimeEngine.h/.cpp        # 扫描 + 严格/密度匹配 + 智能范围 + 提前停止
│   │   ├── CacheManager.h/.cpp       # 基岩版缓存（自定义二进制格式）
│   │   └── SearchTask.h/.cpp         # 后台搜索任务（取消/进度/完成回调）
│   ├── gpu/                    # v3.2.0 GPU 加速（OpenCL，动态加载，无需 SDK）
│   │   ├── ClApi.h/.cpp              # OpenCL 运行时加载层（函数指针表 + 多级回退）
│   │   ├── GpuSession.h/.cpp         # 设备发现/上下文/内核编译/矩形扫描（线程安全）
│   │   ├── GpuScanAdapter.h/.cpp     # 引擎接口 slime::GpuScan 实现
│   │   └── slime_kernel.cl           # Java LCG / 基岩 MT19937 判定内核（构建期嵌入）
│   ├── gui/
│   │   ├── MainWindow.h/.cpp         # 主窗口（全部 UI 逻辑）
│   │   └── PatternEditor.h/.cpp      # 图案编辑器对话框
│   └── utils/
│       ├── Logger.h/.cpp             # 日志系统（按日期分文件）
│       └── CrashDump.h/.cpp          # 崩溃转储（Windows .dmp）
├── third_party/OpenCL-Headers/CL/    # Khronos OpenCL 头文件（类型/常量，不链接）
├── tools/
│   └── ocl_probe.cpp                 # OpenCL 探测/一致性/基准/引擎对比工具
├── tests/
│   ├── test_slime.cpp                # 引擎测试 + 参考兼容 CLI + 性能基准（含 [13] GPU 引擎等价）
│   └── verify_against_reference.py   # 与 SlimeChecker.exe/Python GUI 逻辑交叉验证
├── scripts/
│   ├── install.nsi                   # NSIS 安装脚本（可选交付）
│   ├── embed_kernel.cmake            # v3.2.0：slime_kernel.cl → C++ 原始字符串
│   └── convert_pkl_cache.py          # Python 旧版 pickle 缓存 → 新版 .cache
└── dist/SlimeFinder/                 # 可分发目录（exe + 运行库，windeployqt 生成）
```

## 2. 模块职责与依赖

| 模块 | 职责 | 依赖 Qt | 说明 |
|---|---|---|---|
| `slime_core`（静态库） | 算法/缓存/任务 | 否 | 独立可测试 |
| `RandomGenerators` | Java 48 位 LCG、MT19937、int32/uint32 溢出乘法 | 否 | 与参考实现逐位一致 |
| `SlimeEngine` | 单区块判定、并行扫描位图、严格匹配（位掩码）、密度匹配、进度/取消 | 否 | `run()` 单次完整搜索 |
| `CacheManager` | 覆盖范围 + 史莱姆集合，二进制读写、合并 | 否 | 线程只读共享 |
| `SearchTask` | `std::thread` 包装 SlimeEngine，`std::atomic<bool>` 取消 | 否 | 回调返回结果 |
| `SlimeFinder`（GUI 程序） | 界面/参数/结果/缓存按钮/复制 | 是 | 主线程不阻塞 |
| `Logger` | 按日文件日志（info/warn/error） | 否 | 全局使用 |
| `CrashDump` | `SetUnhandledExceptionFilter` + MiniDump | 否 | 仅在 Windows |
| `slime_tests` | 引擎单元测试 / 参考 CLI / 基准 | 否 | 供验证脚本驱动 |

依赖方向：`MainWindow → SearchTask → SlimeEngine → RandomGenerators / CacheManager`，
`PatternEditor → MainWindow`，`Logger / CrashDump` 全局。

## 3. 核心设计决策（含与任务书的差异说明）

### 3.1 Java 版判定：精确 32 位溢出（任务书 6.1）
- 所有 `x*x*4987142`、`x*5947611` 等乘法经 `int32Mul()` 在 32 位回绕，结果符号扩展为 64 位求和；
- `(long)(z*z)*4392871L` 的 64 位乘法经 `mul64Wrap()` 按二补码回绕；
- 种子求和与 `^ 987234911L` 在无符号 64 位域完成（与 Java long 回绕一致）；
- `JavaRandom::nextInt(10)` 完整复刻 `java.util.Random`（含 48 位掩码与拒绝采样逻辑）。
- 对照 `SlimeChecker.exe` 在 **int32 边界坐标**（约 ±21.4 亿）上扫描结果一致（见 4.1）。

### 3.2 基岩版判定
完整 624 步 MT19937 实现，种子 `x*0x1F1F1F1F(低32位) ^ z`，首次抽取 `%10 == 0`；
与参考实现逐位一致（见 4.1 基岩扫描对照）。

### 3.3 扫描与匹配架构
- 扫描阶段把区域写成**行主序 64 位压缩位图**（BitGrid），并行粒度 ≈ 65536 区块/检查点；
  行（z）为并行单元，每行仅由单个任务写，天然无锁、无伪共享；
- 严格匹配：每行按位图段与图案掩码做 `(segment & enforceMask) == greenBits` 比较，
  任意行不匹配立即短路；行内窗口按 x 顺序滑过；
- 密度匹配：**未使用整幅二维前缀和**，而采用「行内分段 + `__popcnt64`」按窗口统计。
  原因：二维前缀和（int32）在任务书要求的 10000×10000 区域需约 400 MB 内存；
  popcount 方案内存 O(位图) 且实测足够快（见 `--bench`）。语义与参考完全一致。

### 3.4 智能范围限定（任务书 13.1）
- UI 复选框默认关闭；
- 开启后扫描行的执行顺序与匹配行的遍历顺序均改为**以查询起点为中心、由近及远**：
  行按 `|z − 起点|` 升序（并行任务静态分配保证近行先算），行内窗口自中心向两侧扩展；
- 未达到目标数量时自动扩展至用户设定全范围（范围仍由用户设定，未被截断）；
- 任务书中“优先半径（默认 1000）”未做成独立控件（主窗口布局 7.1 亦无该控件）：
  由近及远扫描等价地实现了“先看附近、找不到再外扩”的行为，半径大小不影响结果正确性，
  仅影响进度顺序，故不提供虚假可调参数。

### 3.5 “及时反馈：达到目标数量即停止”（任务书 3.1 P0）
- 结果收集采用容量 N 的**最大堆**（堆顶 = 当前第 N 好），同时累计总命中数；
- 每完成一行后计算**剩余未扫描窗口集合的距离下界 L**：
  - 光栅顺序：剩余窗口构成矩形 → 点到矩形距离；
  - 螺旋顺序：剩余行按距起点单调不减 → 下一行行距；
- 当堆内第 N 个（最差）距离 ≤ L 时停止——此时数学上可证剩余窗口不可能优于已取结果，
  因此**提前停止返回的仍是全局最近 N 个**，与完整扫描结果逐条一致（单元测试 [4] 验证）。
- 旋转（方向不锁定）模式下不做提前停止（后续旋转可能包含更近窗口），保证始终全局最优。

### 3.6 结果排序与参考 GUI 完全一致
- 与 Python v2.9.1 相同：取每个窗口中心 `( (min+max)/2 )` 到查询起点 `hypot` 距离，
  距离升序稳定排序取前 N；
- 引擎用 `(距离, 光栅序号)` 字典序等价复刻“稳定排序”的平局语义（光栅序号 =
  x 主序线性编号，与参考 C++ 的 x-外层/z-内层发射顺序一致）；
- 验证脚本按 Python GUI 的 `math.hypot` + `list.sort` 逻辑重建期望列表逐条比对（见 4.2）。

### 3.7 方向锁定与旋转
- 参考 `main.cpp` 的 `strict_match_full` 实际上忽略 direction_locked 参数（永远不旋转）；
  为避免破坏与参考的一致性，**默认勾选“方向锁定”**且行为与参考一致；
- 取消勾选时按 4 个 90° 旋转逐一匹配并去重（按窗口四元组）。这是对参考 GUI 复选框的
  完整语义实现，锁定状态下输出与参考逐字节一致。

### 3.8 缓存（任务书 6.6 + Python 语义）
- 内存模型沿用 Python 版：若干**已完整确认的矩形区域(ranges)** + **区域内的史莱姆坐标集合**；
  - 对任意落在某 range 内的区块：是史莱姆 ⟺ 坐标在集合中；
  - 查询区域被任一 range 整体覆盖 → 直接由集合推导位图，不重算 MT；
  - 部分覆盖 → 覆盖段查集合，缺口段实时计算，完成后把“新算出的史莱姆坐标 + 本区域”
    合并进缓存并**自动保存**（行为与 Python `_compute_missing_range` + `save_combined_cache` 一致）；
- 磁盘格式（自定义二进制）：`u32 魔数 0x534C4D45 + u32 版本=1 + u32 range数 + u64 条目数`
  + `i32×4×rangeCount` + `u64×entryCount`（高 32 位 X、低 32 位 Z）；
  单次连续读写，无中间格式；**文件格式与扩展名无关**；
- **v3.3.1（任务书 2.1）扩展名统一** + **v3.3.2（任务书 3.2/3.3）对话框可用性修复**：
  默认缓存文件为程序目录 `slime_cache.slimecache`：
  - 常量集中在 `MainWindow.cpp`：`kCacheFileName = "slime_cache.slimecache"`、
    `kCacheLegacyFileName = "slime_cache.cache"`；保存/加载/清除/状态显示全部使用同一路径；
  - 启动时优先加载 `.slimecache`；若不存在而旧版 `.cache` 存在 → 自动加载旧文件并**另存为**
    新扩展名（旧文件保留，用户可自行清理），日志记录“启动加载旧版缓存成功 / 已迁移为新格式
    （含完整落盘路径）”；搜索后的保存与导入合并保存同样在日志中写明路径；
  - **v3.3.2 根因修复**：v3.1.1~v3.3.0 的 `QFileDialog::getOpenFileName(..., QString(), filter)`
    第 3 参数为空 → 对话框落在进程工作目录/上次目录，而缓存固定在程序目录，
    于是「缓存文件存在却在对话框里看不到」。现改为显式构造 `QFileDialog`：
    **起始目录 = 程序目录（`appDirPath()`）+ `selectFile("slime_cache.slimecache")` 预选**，
    保存后再次导入时文件既可见也已选中；
  - 过滤器（`cacheImportFilter()`，`importCache` 与自检共用）覆盖全部历史格式：
    `史莱姆区块缓存 Slime Cache (*.slimecache *.cache *.pkl *.pickle)` /
    `新版缓存 New (*.slimecache)` / `旧版二进制缓存 Legacy (*.cache)` /
    `Python 旧版缓存 (*.pkl *.pickle)` / `所有文件 All Files (*.*)`；
  - 历史格式兼容矩阵：`.slimecache`（v3.3.1+，直接加载）、`.cache`（v3.1.1~v3.3.0，直接加载 +
    启动自动迁移）、`.pkl`/`.pickle`（v3.1.0 及更早的 Python pickle，选中后提示用
    `scripts/convert_pkl_cache.py` 转换）、`.bin`（**本程序历史上从未使用**，无兼容代码）；
  - `scripts/convert_pkl_cache.py` 默认输出扩展名同步为 `.slimecache`；
  - 自检 [12] 覆盖：常量与过滤器字符串、`.slimecache` 保存→重载往返、旧 `.cache` 兼容加载、
    非缓存文件被拒绝并给出原因；**v3.3.2 新增** 12.5 过滤器覆盖历史格式、12.6「对话框可见 + 预选」
    （含 `SLIME_SHOT` 对话框截图）、12.7 真实端到端（基岩 256×256 搜索→生成缓存→重载→
    65536 区块逐块与引擎比对一致）。
- Python 版 .pkl 缓存无法在 C++ 内解析，提供 `scripts/convert_pkl_cache.py` 转换后导入
  （旧格式含 `ranges` 与 `slime_set`，均可无损迁移）。

### 3.9 群系权重接口（任务书 3.2 / 13.3）
- `SlimeEngine::densityWindowMeets(slimeCount, gridSize, minDensity, const float* biomeWeight = nullptr)`
  与内部匹配流程均预留 `biomeWeight` 参数位，当前固定传 `nullptr`（权重 1.0），不暴露 UI。

### 3.10 取消与线程模型
- GUI 点击“开始搜索” → `SearchTask` 起后台线程 → `SlimeEngine::run()`；
  - 扫描/匹配循环定期检查 `std::atomic<bool>` 取消标志（扫描粒度 ≤ 65536 区块），
    取消后**不保存任何部分结果**；
  - 进度回调（0.5% 步进 + CAS 节流）经 `QMetaObject::invokeMethod(..., QueuedConnection)`
    投递到主线程更新进度条/速度/ETA，UI 永不阻塞；
- 关闭窗口：若在搜索，先询问 → 取消 → join 线程后才退出；
- OpenMP 线程数 = 用户设置（1..逻辑核心数），`omp_set_num_threads` 在每次搜索开始前设置。

### 3.11 崩溃转储与日志
- `CrashDump::install()` 注册 `SetUnhandledExceptionFilter` + `std::set_terminate`，
  崩溃时向 `<exe>/crashes/` 写 `SlimeFinder_时间戳.dmp`（MiniDump）与 `crash_report.txt`；
- 日志：`<exe>/logs/SlimeFinder_YYYY-MM-DD.log`，记录启动、搜索参数、结果统计、
  耗时/速度、缓存操作与错误，线程安全（互斥 + 追加写）。

## 4. 验证方法（交付前已执行）

### 4.1 与参考实现（SlimeChecker.exe）的一致性
`tests/verify_against_reference.py` 驱动两个可执行程序并比较输出：
1. **Java 扫描**：10 个种子（含 ±2^63 极值）× 4 普通区域 + 2 个 int32 边界区域，
   输出区块坐标**集合完全一致**；
2. **基岩版扫描**：3 个区域坐标集合完全一致；
3. **严格图案（方向锁定）**：7 种图案（含验收场景 2×2 全绿、含白色/灰色格）× 区域/种子，
   窗口四元组集合完全一致；
4. **密度搜索**：g∈{2,3,5,8}，阈∈{0.05,0.2,0.3,0.4,0.5}，窗口集合一致且每个窗口
   的密度数值一致（相对误差 < 1e-4）；
5. **GUI 最近 N 排序**：以 Python GUI 的 `math.hypot`+稳定排序重建期望列表，
   与引擎 `--near` 输出逐条比对（含智能范围+提前停止组合，仍与全局最近 N 完全一致）。

### 4.2 单元测试（slime_tests --unit，10 组）
溢出模拟 / JavaRandom 与手工 LCG 一致 / 密度与严格匹配对朴素逐窗验证 /
排序-螺旋-提前停止自洽 / 缓存流程与文件往返 / 取消响应 / CacheManager API /
密度合理性 / 旋转匹配与朴素枚举一致 / 缓存容量剪枝（v3.1.0）。

### 4.3 性能基准（slime_tests --bench，见交付报告）
单核与多核扫描速度、严格/密度匹配耗时、100 万条目缓存加载耗时。

## 5. 已知边界与限制
- 扫描上限：区域 ≤ 40 亿区块（v3.1.0 起，容纳 ±30000 全局预设）且位图 ≤ 512 MB；GUI 在
  >100 万区块时先弹确认框（与 Python 一致）；
- 图案行列 ≤ 64（位掩码单段比较上限，编辑器中同步限制）；
- 密度网格 ≤ 512；
- 缓存仅对基岩版有意义，Java 版不参与缓存；
- 窗口无匹配/图案全忽略/区域小于窗口时结果为空（与参考一致）。

---

## 6. v3.1.0 升级说明（相对 v3.0.0）

### 6.1 扫描内核优化（实测单核 3.0 亿 → 5.2~5.5 亿区块/秒，+72%~+84%）
1. **行基座拆解（rowBase）**：Java 判定中仅依赖 z 的项
   `(worldSeed + z²*4392871 + z*389711 类项，uint64 回绕)` 每行只算一次；
2. **x 尾项预计算表**：`(x²*4987142 + x*5947611 的 32 位回绕/符号扩展和)` 与 z 无关，
   区域宽度 ≤ 2²¹ 列时建表（16MB），每区块一次内存读 + 一次加法；
3. **寄存器行累计（“先收集后处理”）**：判定结果先在寄存器累计成 64 位块，
   每 64 区块一次写回该行字，消除逐区块内存读改写与大部分分支存储；
4. **基岩版**：twist 消除全部取模（i+397 mod 624 ≡ i−227），分支改掩码；
   再做**双链融合**（两条独立 MT 初始化链同循环，乱序核重叠延迟），
   单核 22.8 万 → ~135 万区块/秒（+490%），多核 150 万 → ~600 万区块/秒；
5. **编译**：Release 追加 `/GL` + 链接 `/LTCG`（链接时代码生成，约 +3%）。
6. **SIMD（AVX2/AVX-512）方向说明**：
   - Java 哈希+LCG 判定（v3.0.0 基线 3 亿/秒所指内核）在纯标量重构后已达 5.3 亿/秒 ≥ 5 亿目标，
     故主路径保持标量（无条件迁移风险）；代码未使用任何 `__AVX2__` 专属指令；
   - 任务书 2.1 的“5 亿/秒（基岩版哈希判定）”在本项目语境指**哈希/LCG 型判定内核**
     （基线 3 亿/秒即该内核的 v3.0.0 实测值）；**游戏精确的基岩版判定为 624 步 MT19937，
     每次判定约 1248 次 32 位运算，纯运算上限约 1.2×10⁷ 区块/秒/核，即使 SIMD 8 路也无法
     逼近 4 亿/秒**（约差两个数量级，详见 verification_report_v310.md 瓶颈分析）。
     该路径以“双链融合 + 无模 twist”逼近单核实际可达带宽；大范围建议依赖缓存。
7. **结果存储策略**：引擎始终采用「容量 N 最大堆 + 有序过滤」收集匹配（比“小 N 用 vector、
   大 N 用 std::set”方案更优：O(1) 内存上界、O(log N) 推送、顺序无关、平局键稳定）。

### 6.2 用户体验新增
- 快速预设按钮（附近/局部/全局/世界边界/基岩边界，按版本启用）；
- 版本边界（Java ±1875000 / 基岩 ±1048576 区块）自动截断 + 弹窗提示；
- 结果改为卡片列表：每条附「复制命令」（`/tp @s x 64 z`，中心方块坐标）；全量复制保留；
- 右下角「联系我们」对话框（纯文本邮箱，无超链接）。

### 6.3 缓存策略持久化与容量
- 启动自动加载（原有）；增量合并后自动保存（原有）；
- `config.json`：`cache.maxSizeMB`（默认 1024，1~1M MB），首次运行自动生成；
- 保存前 `CacheManager::pruneToFit(maxBytes)`：按加入顺序丢弃最旧 range，
  并仅保留仍被现存 range 覆盖的条目——被丢弃区域的“非史莱姆=不在集合”语义同步撤销，
  保留区域的覆盖与判定语义不变（单元测试 [10] 验证）。

### 6.4 性能日志与群系权重
- `logs/performance.log`：每次搜索一行，含日期/开始/结束/耗时/扫描区块/匹配结果/
  平均速度/进程内存（Windows `GetProcessMemoryInfo`）；
- `SlimeEngine::densityWindowMeets` 等保留 `biomeWeight=nullptr` 占位（API 不变），
  权重说明以用户手册附录《群系对史莱姆生成的影响》提供，界面不暴露（按任务书决定）。

### 6.5 工程化
- 新增无界面自检 `SLIME_SELFTEST=1`（覆盖预设/截断/命令格式/卡片/性能日志/配置读取）；
- 单元测试扩至 10 组（新增缓存剪枝）；
- v3.0.0 的 111 项参考对照全部保持通过；新内核（行基座/xTail 表/寄存器累计/双链 MT）
  与旧实现及参考 exe 逐点一致。

---

## 7. v3.1.1 升级说明（整图搜索，相对 v3.1.0）

### 7.1 移除硬限制与分块执行（chunked path）
- 删除 40 亿区块/位图 words 硬上限；仅保留坐标 ±(2³¹-100) 与 int64 计数护栏；
- 整区位图估算 > 64M words（约 512MB）时自动进入 **z 分块执行**（`runChunked`）：
  - 按锚点行切块：每块数据行 = `anchorStep + hMax - 1`（hMax = 图案/网格最大行高），
    块内调用经典路径（单块位图 ≤ 16M words≈128MB，内存有界）；
  - 块间并行 ≤ min(用户线程, 12)（整图扫描参考目标 ≤ 50 分钟量级）；
  - 每块锚点行域 [start, start+anchorStep-1] 经线程局部 `g_anchorMaxAZ` 精确划分，
    旋转等更矮变体不再越块重复计算（totalFound/去重与经典一致）；
  - 结果按 (距离, minX, minZ) 归并取全局最近 N —— 数学上等价于单次路径的最近 N；
  - 分块路径不使用智能排序/提前停止（保证全局最优），进度按分块粒度回报；
  - 块间并行使用普通线程池（OpenMP 并行区内禁止再调用 omp_set_num_threads，见 §8）；
  - 缓存处理（v3.1.2 起）：并行阶段仅“收集”每块新增（分块覆盖声明），全部块完成后
    单线程并入缓存（详见 §8），内存有界并遵循容量预算。
- 经典路径（≤64M words）代码与行为完全不变 —— 111 项对照与全部常规用例不受影响。

### 7.2 整图确认与动态估算
- GUI：点击「世界边界/基岩边界」预设 → `SlimeEngine::estimateSearch()`：
  先跑 1024×1024 样本实测“扫描速率/匹配速率”（同版本/模式/图案），
  再按分块几何（重叠、并行度）外推整图耗时；确认框如实显示本机预计范围，
  并注明参考目标 ≤50 分钟与“图案匹配可能占主导”；
- 确认后填入范围（会话内记忆，切换版本/小预设后失效）；开始搜索时对 ≥1 万亿区块
  自动兜底一次确认；其他预设/普通范围不弹此框。
- 测试开关：`SLIME_FORCE_CHUNKED=1` 强制分块、`SLIME_CHUNK_WORDS=<n>` 缩小单块容量
  （用于制造多分块并与经典路径逐条对拍；单元测试 [11]）。

### 7.3 其他
- 版本号 v3.1.1；「联系我们/关于」更新为任务书指定完整内容
  （AI 开发致谢、@protolambda/@jocopa3 算法来源与 GitHub、邮箱/QQ 频道/B 站）；
- 旋转去重由 O(n²) 线性查找改为哈希集（哈希碰撞以相等比较兜底），大窗口数不退化；
- 单元测试扩至 11 组（新增整图分块一致性/估算自洽）；GUI 自检新增整图估算路径。

---

## 8. 缓存修复补丁（v3.1.1 交付，仅修复大范围基岩缓存 Bug）

### 8.1 大范围基岩缓存 Bug（高优先级修复）
- **现象**：基岩版 ≥全局预设（进入分块路径）搜索后，缓存文件仅 ~1KB（只有头部与空声明）。
- **根因（v3.1.1 引入）**：分块路径对“整区新增条目”设 64M 上限，超限即清空收集
  （避免整图内存爆炸），随后 GUI 仍按整区做覆盖声明 → 文件 ≈ 空；
  且旧实现“块内完成即并入共享缓存”与并行块读取竞争（可崩溃）。
- **修复**：
  1. 块间并行改为**普通 std::thread 线程池** —— 分块内部经典路径需要
     `omp_set_num_threads(1)`，OpenMP 规范禁止在活动并行区内设置线程数
     （实测触发 User Error 1001 / 快速失败）——线程池天然规避嵌套问题；
  2. 缓存写入采用**“并行只收集、结束单线程统一并入”**：
     每块保存 (分块覆盖声明 + 该块新条目)（`CachePiece`），并行期零缓存写入；
     全部块完成后顺序 `mergeRegion` 到 cacheWrite（并执行容量预算 prune）；
  3. 内存有界：每块条目 ≤ 块面积×10%，收集总量按 `cacheBudgetBytes`
     （默认 config 的 1024MB，未显式给定按 1GB 兜底）保留**最新**分块，
     超出即丢弃最旧分块（其覆盖声明一并撤销，语义自洽）；
  4. 基岩版“收集缓存”场景自动缩小单块容量（≤2.5M words），控制单块暂存内存；
  5. `SearchOutcome::cacheHandledIncrementally`：GUI 对分块路径不再做整区合并
     （避免失真声明），仅负责写盘与状态展示。
- **验证**：单元测试 [12]——分块基岩+增量缓存：多块覆盖声明、抽查判定与直接计算一致、
  文件往返重载一致、覆盖缓存下第二次搜索结果与全新扫描逐条一致；
  文件大小随扫描范围线性增长（验收“≥ 预期 80%”）。
- **物理限制说明（替代方案）**：基岩版整图史莱姆条目 ≈ 4.4×10¹¹（约 3.5TB 原始）
  —— 单文件/单进程内存均不可行；工具以“分块覆盖声明 + 容量预算（config.json
  cache.maxSizeMB）”作为**分片式存储**方案：保留最新扫描分块直至预算上限，
  被丢弃分块自动回到“未知”（重扫即可续增），缓存语义始终自洽。
  常规规模（亿级区块）缓存文件大小与范围成正比，导入/导出/重载完全正常。

## 9. v3.2.0 升级说明（GPU 加速 + 匹配并行，相对 v3.1.1）

> v3.2.1 修复补丁（默认关闭 + 无驱动环境稳定）见 §9.4。

### 9.1 GPU 加速（OpenCL，可选增强，自动启用/回退）
- **运行期动态加载 OpenCL**（`src/gpu/ClApi`）：不依赖任何 OpenCL SDK/导入库，
  Khronos 头文件仅作类型/常量来源（`third_party/OpenCL-Headers`）；
  模块回退链：系统 ICD 加载器 → 注册表 Vendors 列出的 ICD → DriverStore AMD
  运行时 → System32 已知名（覆盖“注册表 ICD 缺失”等异常机器）。
- **判定内核**（`slime_kernel.cl`，构建期经 `scripts/embed_kernel.cmake` 嵌入，
  单一事实来源）：Java LCG / 基岩完整 MT19937，全部无符号算术，与 CPU 逐位一致。
- **引擎接入**：`SlimeEngine.h` 声明最小接口 `slime::GpuScan`（core 零依赖，
  指针经 `SearchOptions::gpuScan` 注入）；经典路径扫描阶段在
  `totalChunks ≥ 1048576`（`SLIME_GPU_MIN_CHUNKS` 可调）且设备可用时走
  `runGpuScanStage`：行瓦片 2D 切分（≤256MB/瓦片，`SLIME_GPU_TILE_BYTES` 可调）、
  缓存覆盖区间回读缓存、新缓存条目收集、进度/取消（瓦片粒度，取消响应 <1s 量级）；
  GPU 失败自动回退 CPU（两路径判定同一事实源，位图按位或回扫无污染）。
  分块路径（整图）自动继承：块内单线程经典路径改用 GPU 判定，
  GpuSession 内部互斥串行化保证多块并发安全。
- **GUI**：config.json `gpu.enabled`（默认 true）+ 环境变量 `SLIME_GPU=0/1`；
  启动预热一次初始化复用；日志标注「GPU扫描」。
- **实测（RX 6750 GRE）**：Java 判定 ≈110 亿区块/秒、基岩 ≈5600 万区块/秒
  （端到端含回读）；位精确性经字节级全量比对与引擎级完整搜索等价验证。
  详见 `docs/gpu_phase1_report.md`、`docs/gpu_phase2_report.md`。

### 9.2 匹配阶段多核并行
- 经典路径匹配原为单线程（约 1.2 s/8192² 密度），现按**行并行**
  （条件：单变体 + 无提前停止 + 行数 ≥ 2×线程）：行处理提取为 `evalRow`，
  线程局部 TopNHeap，结束后按 betterThan 全序归并 —— 结果与顺序路径
  （含旋转去重/提前停止路径）完全等价；旋转模式与“及时停止”保持原顺序语义。
- 收益：Java 8192×8192 密度整搜 1.19 s → **0.13 s**（UI 口径 N=50，≈9×）。

### 9.3 版本与回归
- 版本号 v3.2.0（main.cpp/GUI/安装脚本）；网格地图特性保持取消。
- 回归：单元测试 [1]~[13]（13 项，含 GPU 引擎等价四场景）；与参考实现
  111/111；GUI 自检 exit 0（GPU 开/关）；GUI 冒烟存活；GPU 不可用自动回退 CPU
  且 CPU 路径性能不回退（并行匹配为纯增量收益）。

### 9.4 v3.2.1 修复补丁（GPU 默认关闭 + 无 OpenCL 驱动环境稳定）
- **问题**：v3.2.0 在无/异常 OpenCL 驱动环境中存在启动即初始化 GPU 并在搜索时崩溃的风险；
  且 GPU 只能经环境变量/配置文件控制，交互不友好。
- **修复（按 v3.2.1 任务书 + 实测反馈修订）**：
  1. **GUI 开关**：主界面“并行计算”组新增复选框「启用 GPU 加速（实验性，默认关闭）」；
     勾选/取消即时保存 `config.json gpu.enabled`（`saveGpuEnabledToConfig`），
     启动时读取并同步复选框（`gpuEnabledFromConfig`，缺省 **false**）；
  2. **不再自动启用**：搜索仅当「勾选 且 检测可用」才注入 `opt.gpuScan`
     （移除 v3.2.0 的配置自动启用/预热初始化），杜绝无驱动环境自动触发；
  3. **启动阶段完全不接触 OpenCL（修订）**：早期方案在构造函数以后台线程探测
     （`startGpuProbe`），实测发现无/异常驱动环境下 **LoadLibrary/驱动初始化可发生
     原生崩溃（非 C++ 异常，try/catch 无法捕获）**，后台线程探测同样会击穿进程——
     因此改为 **`ensureGpuScanOnce()`：仅当用户勾选启用时同步检测一次**（失败记日志、
     标记不可用），默认关闭路径从启动到搜索全程零 OpenCL 接触，保证无驱动环境稳定；
  4. **日志（P2）**：检测开始/结果、勾选启用/关闭、勾选但不可用/未就绪的回退原因
     全部写入 `logs/`；
  5. **引擎空指针/不可用保护**：`runGpuScanStage` 显式区分「未注入」与
     「available=false」并立即返回 CPU 路径（最后防线）；
  6. **GpuSession::init 异常兜底**：外层 try/catch 把任何 C++ 异常转为
     `false + m_lastError`，绝不外抛；
  7. **崩溃现场输出**：main.cpp 注册 VEH，任何原生崩溃打印异常码/地址/调用栈到 stderr
     （配合 crash dump 定位）；
  8. 自检 [9]：复选框存在性、勾选触发检测完成、config 写读往返、GPU 可用时注入
     端到端搜索（2048² ≥ 阈值）；自检 [10]：**真实 GUI 搜索冒烟**（startSearch 全链路：
     后台任务线程 + 进度回调 + 结果渲染 + 日志落盘，512×512 Java 密度无弹窗）。
- **验收**：无驱动环境（模拟：`SLIME_GPU=0` 与勾选后检测失败路径）启动/搜索走 CPU
  无闪退；勾选但不可用自动回退并记录原因；勾选且可用正常 GPU；config 默认 false。
  本机（GPU 可用）回归：单元 13/13、111/111、GUI 自检 exit 0（开/关 ×2，含真实搜索
  冒烟）、冒烟存活。
- **备注**：驱动 DLL 初始化级的原生崩溃在「用户主动勾选」这一单点仍可能发生（进程内
  无法免疫），已通过默认关闭 + 勾选时单次检测把暴露面最小化；如需彻底隔离可后续改为
  子进程探测（超出本版本范围）。

## 10. v3.3.0 升级说明（算法升级，相对 v3.2.1）

### 10.1 新增抽象：`Algorithm` 与模式语义

```cpp
enum class Algorithm : uint8_t { Exact = 0, Layered = 1, Sample = 2, Brute = 3 };
```

* `SearchOptions` 新增 `algorithm` / `layerBlock` / `layerRatio` / `sampleStep` / `sampleExact`；
  `SearchOutcome` 新增 `windowsSkipped`（被剪枝/未采样而未评估的窗口数，精确/保守恒 0）。
* **语义优先级**：`Mode::Strict`（图案匹配）优先于 `Algorithm`；分层/估算属“密度类”算法，
  若调用方误配严格模式，引擎强制按密度语义执行（`algoDensityOnly` 兜底，GUI 侧也已禁用该组合）。
* 扫描/匹配阶段共用的模式判定（`strict` / `sampleAlgo` / `sampleScan`）统一在 `runImpl` 开头
  计算，避免跨阶段重复推导与顺序错误。

### 10.2 P0-3.1 密度滑动窗口

* `evalDensityBlock()`：每个行块维护 `vertSum`（列垂直和，长度 `wordsPerRow*64 + K + 8`），
  行进入/离开时增量更新（AVX2 位展开查表 `vertSumDiffAvx2`，标量回退逐列）；
  窗口计数沿 x 以 `total += vertSum[x+K] - vertSum[x]` O(1) 递推 → 匹配阶段 O(1)/窗口。
* **整数快路径**：`densityWindowMeets(cnt,g,m)` 等价改写为
  `cnt ≥ ceil(m·g²)+2 → 真 / cnt ≤ ceil(m·g²)-2 → 假`，仅 ±2 边界带回退浮点判定
  （结果与浮点判定逐位一致，省去每窗口一次除法；保守模式刻意不启用，保持 v3.2.1 字面语义）。
* 仅当「密度模式 + 非智能范围 + 非提前停止」时启用（智能范围由中心向外、提前停止需要逐行判定）。

### 10.3 P0-3.2 严格匹配：AVX2 内核 + 字级位并行（默认路径）

* `src/core/FastMatch.{h,cpp}`：头文件不含 SIMD 类型（可被任意 TU 安全包含）；
  CMake 仅对该文件加 `/arch:AVX2`（非 MSVC 为 `-mavx2`）。
* `cpuHasAvx2()`：CPUID 0/1/7 + OSXSAVE/XCR0 检测并缓存；`strictRowAvx2()` 用
  `_mm256_srlv_epi64/_mm256_sllv_epi64` 做变长移位生成窗口掩码，一次比较 4 个窗口；
  `vertSumDiffAvx2()` 按 8 位列展开为 int32 做行差分。
* **字级位并行（v3.3.0 默认严格路径，`evalStrictRowBits`）**：对 64 个连续锚点整批判定 ——
  图案行 rr 的约束为 `(窗口位 & enf[rr]) == grn[rr]`；第 cc 列的位向量 = 行位图右移
  （锚点基址 + cc）后的同一字（跨字两个字拼接），每受约束列 1 次移位 + 1 次与/与非，
  按行取与得到 64 锚点命中掩码 → `countr_zero` 逐位记录。成本 ≈ 列数×行数×3 次 64 位运算/64 锚点。
  纯 64 位运算、无需 AVX2，旧 CPU 同样受益；`SLIME_BITS_STRICT=0` 关闭（对照），
  列数 > 64 / 智能范围 / 保守模式自动回退标量路径。
* AVX2 4 窗口内核默认关闭（`SLIME_AVX2_STRICT=1` 强制启用）：同机 A/B 下不如标量早退
  与字级批处理（0.83~0.99×），保留作为对照实现与特定图案实验入口。
* 三条严格路径（字级位并行 / AVX2 / 标量）结果集合与总数完全一致
  （单元测试 [3][9][16] 全图案组合对照）。

### 10.4 P1-3.3 分层剪枝（可证无损）

* 粗块计数表 `layeredCnt`（块边长 `layerL = max(clamp(layerBlock,8,64), gridSize)`）。
* 判据：窗口 ⊆ 覆盖它的至多 4 个粗块之并 ⇒ `窗口史莱姆数 ≤ Σ 粗块计数`；
  若上界 `< ceil(所需数 × layerRatio)` 则该锚点必然不达标 → **整段跳过**。
  按“粗块列”成段判定（段内锚点共用一次列求和；块边界 g-1 列用双列和），避免逐锚点开销。
* `layerRatio ≤ 1.00` 时判据严格成立 ⇒ **零漏检**（结果与精确模式集合相同）；
  `> 1.00` 时门限高于必要条件 ⇒ 可能漏检（界面警告条 + 日志）。
* **自适应回退**：对锚点行抽样估计剪枝通过率，若 ≥90% 通过（剪枝无效）则整个搜索改走
  10.2 的滑动路径 —— 保证分层模式在均匀随机地图上不比精确模式慢（实测 0.96~1.00×）。

### 10.5 P1-3.5 采样估算（两种精度口径）

* 快速估算（默认）：扫描阶段只计算 `(x-x0)%S==0 && (z-z0)%S==0` 的区块
  （`S = sampleStep`），匹配阶段只评估锚点落在采样网格上的窗口，密度 = 窗口内采样格点中
  史莱姆比例（`recordMatchAt(..., densityOverride)` 写入估算值）。锚点按步长直接迭代（无取模）。
* 精算采样（`sampleExact=true`）：扫描采样锚点窗口覆盖的全部区块
  （`(x-x0)%S < g` 且 `(z-z0)%S < g`，g ≥ S 时为全图），匹配阶段逐窗精算密度
  → 报告密度与精确模式同位置完全一致（相关性 1.00，无假阳性）。
* **缓存红线**：估算模式强制 `useCache = false`（采样位图只有 1/S² 的位被计算，
  若写入缓存会被误判为“整片已覆盖”，后续精确搜索将读到缺失位而给出错误结果）。
* 估算模式强制 CPU 采样扫描（GPU 会计算整幅位图，无采样收益）。
* 相关性上界：`corr = sqrt(m)/g`（`m = ceil(g/S)²`），S=8/g=5 时为 0.2（实测 0.194）。

### 10.6 P1-3.4 基岩版 AVX2 批量 MT19937

* `src/core/FastMt.{h,cpp}`：4/8/16/32 通道批量内核（`__m256i` 通道打包 4 个独立 MT 状态，
  多链交织隐藏乘法延迟）+ 连续扫描 `bedrockSlimeScan`（32/16/4 批量 + 标量头尾）+
  纯标量回退与 `SLIME_FORCE_SCALAR=1` 对照开关；独立 `/arch:AVX2` 编译单元。
* 引擎基岩扫描路径改为按 64 区块批量调用（`kBedrockBatch`），位图写入与 `newCacheChunks`
  收集语义不变；判定结果与逐区块标量路径逐位一致（单元测试 [15]）。
* 实测单核 36.24 M 区块/秒（v3.2.1 为 0.85~1.3 M/秒），任务书目标 4 M/s 达标 9×。

### 10.7 第三阶段：界面与日志

* **布局可滚动化（v3.3.0 修订）**：设置区全部放入 `QScrollArea`（`setWidgetResizable(true)`，
  竖向滚动条按需出现），结果区保留在窗口下方并拥有独立滚动条，二者互不遮挡；
  主窗口重写 `resizeEvent` 重新预留换行标签高度（`reserveWrappedHeight`：
  按当前宽度用 `QFontMetrics::boundingRect(..., Qt::TextWordWrap)` 计算所需行高并
  `setMinimumHeight`），解决 QLabel 在滚动容器/嵌套布局中 height-for-width 失准导致的
  末行裁剪；算法单选按钮按字体度量设最小宽度，避免被布局压缩后文字省略。
  设置区各分组宽度已压缩到默认窗口（700px）内，默认状态下不需要横向滚动条。
* 主窗口「搜索模式」组新增算法单选（精确/分层/快速估算/保守）+ 分层级别/阈值比例、
  采样间隔/精算采样控件，参数即时写入 `config.json` 的 `algorithm` 块（`writeConfigFile`
  整体重写 cache/gpu/algorithm 三段，避免脆弱字符串替换；`cfgString/cfgInt/cfgDouble/cfgBool`
  为极简取值解析）。
* 结果区顶部固定「当前使用模式」+ 非精确模式黄色警告条（含跳过窗口数与占比），
  `describeResultsText`（复制文本）同步标注。
* `performance.log` 追加：模式 / 算法 / 参数 / 剪枝跳过率 / 与精确模式差异
  （≤256×256 区块的非精确搜索自动跑一次同范围精确对照，超出范围如实记录未校验）。
* 自检新增 [11]：算法控件存在性、严格模式禁用算法、分层/估算联动与警告条、config 往返、
  估算模式真实搜索冒烟（startSearch 全链路 + 日志字段断言）、
  **布局校验**（单选文字不被截断、换行说明标签高度足够、设置区竖向滚动条可用、
  默认窗口宽度下无横向滚动条）以及 `SLIME_SHOT=<前缀>` 界面截图输出（便于人工核对）。

### 10.8 与任务书的差异说明

1. **严格匹配的实现方式与任务书不同、结果更好**：任务书设想「AVX2 4 行合并」，实测在等价
   算法下仅 0.83~0.99×（标量早退已最优）；改为**字级 64 锚点批处理**后匹配阶段实测 13~15×，
   超 ≥3× 目标，且不依赖 AVX2（AVX2 内核保留为可选对照路径）。
2. **分层判据改为“块级必要条件”**：任务书原判据为“粗块密度 ≥ 目标密度 × 比例”，
   在真实地图（约 10% 史莱姆密度）上会把 99.99% 的块剪掉导致 0 结果（实测）；
   本实现改为可证无损的必要条件判据 `Σ粗块计数 ≥ 所需史莱姆数 × 比例`，
   既保留“阈值比例”参数语义（比例越大越激进），又保证比例 ≤1.00 时结果与精确模式一致。
3. **采样相关性 0.7 不可达（数学上界 1/S）**：保留加速 ≥50× 的默认口径并提供“精算采样”
   达标选项（相关性 1.00），界面/手册明确标注（详见验证报告 §4.3）。

## 11. v3.3.1 升级说明（缓存修复 + 基岩/AVX2 优化；GPU 密度内核暂停，相对 v3.3.0）

### 11.1 缓存扩展名统一（任务书 2.1）

见 §3.8 末段：默认文件 `slime_cache.slimecache`，旧 `slime_cache.cache` 兼容读取并自动迁移；
v3.3.2 起导入对话框**定位到程序目录并预选当前缓存文件**，过滤器五档
（`*.slimecache *.cache *.pkl *.pickle` / 新版 / 旧版二进制 / Python 旧版 / `*.*`）；
`scripts/convert_pkl_cache.py` 默认输出版本同步为 `.slimecache`。自检 [12] 覆盖常量、过滤器、
往返、旧格式兼容、非法文件拒绝、对话框可见与预选、真实端到端缓存一致性。

### 11.2 基岩版种子查表（任务书 2.2）

* **任务书前提澄清**：v3.3.0 起基岩版的“匹配阶段”工作在**位图**上（O(1)/窗口），
  种子乘法 `umul32Lo(cx, 0x1F1F1F1F)` 只出现在**扫描阶段**（区块判定）与单区块判定 API
  （`isSlimeChunk` / 采样扫描）中；且扫描阶段已由 AVX2 批量 MT19937 内核承担
  （`_mm256_mullo_epi32` 一次算 4 条通道），标量乘法不在热点路径上。
* **实现**：`FastMt` 提供 `bedrockChunkSeedTable(cx, czu)`（低 16 位查表 65536×4B=256KB +
  高 16 位拼接 `hi*(K<<16)`）与 `bedrockSlimeScanStrided*` / `bedrockSlimeScanScalarTable`
  等可选路径（默认关闭，`SLIME_MT_TABLE=1` 或显式 API 启用），保证与直接乘法**逐位一致**。
* **实测结论**（详见验证报告 §3）：查表版本相对直接乘法**没有提速**（256KB 表超出 L1、
  每区块仍需一次 32 位乘法用于高位项、而每区块 624 步 MT init+twist 才是绝对瓶颈）；
  因此默认路径保持直接乘法，查表实现作为可选/对照保留并记录实测数据。

### 11.3 基岩版 AVX2 批量窗口密度统计（任务书 3.1）

* `FastMatch` 新增 `densityWindowCountsAvx2(...)`（输入若干行的位图字、输出每个锚点的 g×g
  计数），与朴素 `countBits` 循环**逐位一致**；AVX2 使用 nibble 查表 popcount 与字级滑动。
* 说明：v3.3.0 的精确密度模式已用“列垂直和 + 行块滑动”把窗口计数降到 **O(1)/窗口**
  （≈2 次整数运算），因此新内核对**滑动路径**难以形成 2~3× 优势；它的价值在于
  **无滑动资格的路径**（智能范围 / 提前停止 / 分层 / 采样精算等此前逐窗 `countBits` 的场景）。
  验证报告给出两条基线的实测对比与启用建议。

### 11.4 GPU 密度匹配内核（任务书 3.2）——**按指示暂停，v3.3.1 不交付**

> 状态：**开发暂停**。`src/gpu/` 已裁剪为「仅扫描」（`scanJava` / `scanBedrock`，与 v3.3.0 逐位一致），
> 密度匹配相关的全部代码（含未完成调试代码）原样保留在 `src/gpu/experimental/`，作为 v3.4.0 起点；
> 恢复步骤与已知缺陷见 `src/gpu/experimental/README.md`；`slime_gpu_density_tests` 目标已移除。
> 暂停原因：开发中的密度内核破坏了 Java 判定位图内核的正确性并伴随驱动侧堆损坏，
> 为守住「结果与 v3.3.0 100% 一致」的底线而整体移出本版本交付范围（详见验证报告 §5）。

* **接口**（`src/core/SlimeEngine.h`，**已实现并保留**）：`slime::GpuScan` 新增虚函数
  ```cpp
  virtual bool matchDensity(const DensityMatchParams& p, const std::atomic<bool>& cancel,
                            std::vector<MatchResult>& out, std::string* err) const;
  ```
  默认实现返回 `false`（= 不支持 → 引擎回退 CPU）；`DensityMatchParams` 含版本/种子/锚点范围/
  网格 g/阈值/返回上限 `maxResults`。**因此当前构建下密度匹配恒走 CPU 路径，行为与 v3.3.0 一致。**
* **语义契约（精确性红线，v3.4.0 实现必须遵守）**：命中集合 = `{ 锚点 | 窗口内史莱姆数 ≥ minDensity·g² }`，
  `density = (float)count / (float)(g*g)`（整数计数后一次浮点除法）；实现必须返回**全部**命中，
  若命中数将超过 `maxResults` 必须返回 `false` 让引擎回退 CPU（**不允许截断/抽样**）。
  取消时返回 `true` 且 `out` 只含已完成部分。
* **引擎接入点**（`SlimeEngine::runImpl` 阶段 1a，位于位图分配之前，**已就绪**）：
  条件 = 密度模式 + `Algorithm::Exact` + 非智能范围 + 未启用提前停止 + 非采样模式 +
  未启用基岩缓存（缓存写入需要逐区块位图）+ 已注入且可用 GPU + 区域≥阈值；
  `SLIME_GPU_MATCH=0` 可单独关闭该路径（保留 GPU 判定扫描）。
  命中结果按与 CPU **完全相同**的 `(distance, rank)` 序入 `TopNHeap` →
  `out.results` / `out.totalFound` 与 CPU 逐位一致；失败或超上限自动回退下方 CPU 扫描+匹配路径。
  分块路径（整图级）中每块各自调用一次，锚点行域受 `g_anchorMaxAZ` 限制，跨块不重复评估。
* **验证（已就绪，v3.4.0 可直接复用）**：单元 [17]（真实 GPU 适配器：多版本/多网格/多阈值 × CPU
  逐位对照；当前实现返回 false 时按“不支持即跳过”处理，不产生假阳性）、
  单元 [18]（模拟提供者：堆序、回退、启用条件、分块锚点域，不依赖 OpenCL 设备）。

### 11.5 构建期修复（v3.3.1 实测发现；与 GPU 功能无关，但影响构建正确性）

1. **嵌入 OpenCL 内核被注入换行**（`scripts/embed_kernel.cmake`）：旧脚本在每 24 字节的换行处
   写入了 C 转义 `\n`（而非仅源码换行），使 `kSlimeKernelSource` 每 24 字节多出一个换行字符，
   内核注释被截断成代码、标识符被劈开 → 运行期 `CL_BUILD_PROGRAM_FAILURE`。已修复，并在脚本内
   增加「八进制转义自回读校验」（不一致直接 FATAL）；`slime_kernel_tests` D 段在运行期把嵌入串与
   磁盘 `.cl` 逐字节比对（长度 6058 / 校验和 997496561 / 两个 `__kernel` 签名完整）。
2. **Ninja 头文件依赖跟踪失效**（`CMakeLists.txt`）：中文版 cl.exe 的 `/showIncludes` 前缀是 GBK
   字节，与 `rules.ninja` 里的 `msvc_deps_prefix` 文本不匹配 → Ninja **静默丢弃全部头文件依赖**
   （`ninja -t deps <obj>` 显示 `#deps 0`）→ 改头文件不重编译（实测曾导致旧 OpenCL 内核被静默复用、
   虚表不匹配导致 `0xC0000005`）。已改为把全部项目头文件（含生成的 `slime_kernel_embed.h`）声明为
   各目标源文件的 `OBJECT_DEPENDS`：头文件变更即重编译，与编译器语言/代码页无关
   （`VSLANG=1033` 无法切换该前缀语言，已实测）。

## 12. v3.3.2 修复说明（缓存可用性，相对 v3.3.1）

任务书：`模板：原始文件/V3.3.2.txt`；性质：**P0 Bug 修复 + 状态确认**（不引入新功能、无架构改动）。

### 12.1 缓存 Bug 的状态与根因（确认 + 补齐）

* **v3.3.1 已完成的**：扩展名统一为 `.slimecache`（常量集中、保存/加载/清除/过滤器/转换脚本一致），
  旧 `.cache` 兼容读取并在启动时自动迁移，导入对话框起始目录改为程序目录、过滤器含
  `*.slimecache *.cache` 与 `*.*`。
* **v3.3.2 补齐的（本次根因修复）**：
  1. **对话框预选当前缓存文件**：`importCache()` 由 `QFileDialog::getOpenFileName(...)` 改为
     显式 `QFileDialog` + `selectFile("slime_cache.slimecache")` —— 保存后再导入时文件
     「既可见、又已选中」，一步加载；
  2. **过滤器覆盖全部历史格式**：新增 `*.pkl` / `*.pickle`（Python 时代）与
     `Slime Cache` 英文标识，分组 5 档，任何历史缓存都能在对话框中被看到并选中；
  3. **落盘路径写日志**：搜索后保存 / 导入合并保存 / 启动加载与迁移，日志均带完整路径，
     用户可据此直接定位文件；`.pkl`/`.pickle` 选中后给出转换提示（不再静默失败）。
* **根因说明（为什么 v3.1.1~v3.3.0 会出现“文件存在却看不到”）**：缓存固定写在**程序目录**，
  而导入对话框第三个参数（起始目录）为空 → Qt 回退到进程工作目录/上次访问目录；用户从
  快捷方式或其它目录启动时对话框落在别处。**保存名与过滤器扩展名本身是一致的（.cache）**，
  因此问题不在“扩展名不匹配”，而在**起始目录未定位**（叠加历史 `.pkl` 未列入过滤器）。

### 12.2 验证（可复现）

* GUI 自检 [12] 新增三段（12.5 过滤器覆盖 / 12.6 对话框可见+预选+截图 / 12.7 真实端到端）：
  在临时目录写入 `slime_cache.slimecache` + 干扰文件 `readme.txt`，断言默认过滤器下列表**恰好**
  命中缓存文件而不命中干扰文件、「所有文件」可见任意文件、`QFileDialog.selectFile()` 预选正确，
  并在 `SLIME_SHOT` 下渲染对话框截图 `docs/screenshots/cache-import-dialog-v332.png`；
  端到端跑一次基岩 256×256（启用缓存）搜索：生成缓存 6811 条目 → 重新 `loadFile` → 65536 区块
  逐块与 `SlimeEngine::isSlimeChunkBedrockMT` 比对，**不一致 0**。
* 启动迁移：仅保留 `slime_cache.cache` 启动一次 → 日志「启动加载旧版缓存成功 + 旧版缓存已迁移为
  新格式」，磁盘上旧文件保留、新文件生成（证据 `docs/v332_cache_evidence.txt`）。
* 全量回归：`scripts\release\verify_release.ps1`（clean 重建 + 单元 18 组 + 111/111 + OpenCL 探测 +
  内核自检 + GUI 自检 GPU 开/关 + 基准 + dist/zip 哈希），详见 `docs/verification_report_v332.md`。

## 13. v3.3.3 升级说明（版本号统一 + 交互修复 + UI 重构，相对 v3.3.2）

任务书：`模板：原始文件/v3.3.3 完整任务书.txt`。
**约束（已满足）**：不删除任何功能、保持现有快捷键、结果与 v3.3.2 逐位一致、
折叠/排序等只影响显示与提示，不参与任何判定或匹配计算。

### 13.1 版本号单一来源 + exe 版本资源（任务书 2.1）

* **单一来源**：`src/main.cpp` 的 `kVersion` 与 `src/gui/MainWindow.cpp` 的 `kAppVersion`
  统一为 `v3.3.3`，分别驱动：
  * 主窗口标题（`setWindowTitle(... kAppVersion ...)`）与「关于 / 联系我们」对话框；
  * 应用显示名（`QApplication::setApplicationDisplayName`）与
    `QApplication::setApplicationVersion("3.3.3")`；
  * **日志文件头**（`main.cpp` 启动时写 `========== 史莱姆区块查找器 v3.3.3 启动 ==========`）。
* **config.json 版本字段**：`writeConfigFile()` 现在写入 `"version": "v3.3.3"`
  （读取侧不依赖该字段，缺失或旧值都不影响行为），便于用户与排障时确认配置文件版本。
* **exe 版本资源（新增）**：`src/app.rc` 提供 `VERSIONINFO`
  （`FILEVERSION/PRODUCTVERSION 3,3,3,0`、中文 `080404B0` + 英文 `040904B0` 两个字符串块，
  `#pragma code_page(65001)` 保证中文不乱码），并加入 `SlimeFinder` 目标源文件。
  效果：**资源管理器 → 属性 → 详细信息** 显示 文件版本 `3.3.3.0`、产品版本 `3.3.3`。
  打包脚本 `package_release.ps1` 增加对该字段的断言（`FileVersion/ProductVersion` 前缀匹配）。
* 其余位置（install.nsi、CMake `project(VERSION)`、脚本默认版本、全部文档标题）同步为 3.3.3；
  历史章节中出现的旧版本号属于变更记录，按任务书要求保留。

### 13.2 缓存状态标签即时刷新（任务书 2.2）

* **文案**：`缓存已加载，共 N 个区块（覆盖 R 个区域，约 X MB）`（N = `entryCount()`）。
* **刷新路径**（全部显式调用 `updateCacheStatusUi()`）：启动自动加载、旧格式迁移、
  搜索完成后的整区合并与分块增量合并、搜索范围已被缓存完全覆盖（无新数据）分支、
  导入合并、清除缓存、**勾选/取消「使用缓存」**（v3.3.3 新增连接）、版本切换。
* **磁盘态提示**：内存无数据但程序目录存在缓存文件时显示
  `未加载缓存（磁盘上已有 slime_cache.slimecache，可点「导入缓存」或重启程序载入）`。
* **额外反馈**：搜索写盘后状态栏提示 `缓存已更新：共 N 个区块`，
  避免设置区被滚动到别处时用户看不到变化。

### 13.3 GPU 加速临界值（任务书 3.1，静态预判）

* 设备信息来源：`slime::GpuScan` 新增三个只读查询（默认实现返回空/0，未实现者行为不变）：
  `deviceName()` / `globalMemBytes()` / `computeUnits()`；
  `GpuScanAdapter` 用 `GpuSession::device()` 实现它们（对应
  `CL_DEVICE_NAME` / `CL_DEVICE_GLOBAL_MEM_SIZE` / `CL_DEVICE_MAX_COMPUTE_UNITS`）。
* **估算公式**（源码注释含完整标定说明）：
  `临界区块数 ≈ 显存(MB) × 1000 ÷ (CPU 逻辑核数 × K)`，**K = 0.85**（唯一可调参数）。
  标定依据：本机（10224 MB / 12 线程）→ ≈ **100.2 万区块**，与实测交叉点（0.4~1.0 M）
  及引擎既有启用阈值 `SLIME_GPU_MIN_CHUNKS`（1<<20）一致；显存↑ → 临界值↑，
  CPU 核数↑ → 临界值↑（CPU 越强越晚才值得开 GPU）。缺设备信息时退化为 1<<20。
* **UI**：勾选 GPU 后「并行计算」组内显示设备名与临界值文案；每次「开始搜索」前，
  若 `totalChunks < gpuThresholdChunks()` 则状态栏提示「建议关闭 GPU 加速」（不弹窗、不阻塞），
  并在日志中记录判定依据；为避免被 `正在搜索…` 覆盖，提示文本在 `startSearch()` 末尾统一写入。

### 13.4 进度分阶段 + 结果列表分批（任务书 3.2）

* 引擎仍在**后台线程**运行（`SearchTask` 内 `std::thread`），主线程只处理队列回调；
  扫描与匹配循环内均有取消检查（`cancel.load`）。
* 阶段文字抽成纯函数 `progressStageText(stage, pct, cps, etaSec)`（0 扫描 / 1 匹配引导 /
  2 匹配中 / 3 排序），GUI 与 GUI 自检共用同一实现，自检可直接断言四段文字。
* **「未响应」根因与修复**：匹配完成后，主线程要一次性创建最多 2000 张结果卡片
  （每张含 QLabel + 「复制命令」按钮），这段 UI 构建期间事件循环被占满，用户点击即触发
  Windows「未响应」。v3.3.3 改为**先收集到待创建列表、再由 `flushResultRows()` 每 40 条
  创建一次并 `processEvents()`**，期间进度区显示「正在生成结果列表… i / N」；
  同时排序阶段单独提示「正在排序结果…」。
* 取消响应实测（GUI 自检）：点击调用 0.9 ms 返回，搜索线程 9~17 ms 内停止并显示「已取消」。

### 13.5 帮助 / 使用教程（任务书 3.3）

* 新增 `src/gui/HelpDialog.{h,cpp}`：`helpTutorialHtml()`（教程正文，供对话框与自检共用）
  与 `showHelpDialog(QWidget*)`（模态对话框：`QTextBrowser` 可滚动、可选中复制，
  「复制全部文字」按钮写入剪贴板并就地提示，「关闭」按钮为默认按钮）。
* 入口：状态栏右下角「帮助 / 使用教程」（与「联系我们」并列，随时可点）。
* 内容七章：这是什么 / 怎么用 / 图案怎么画 / 四种模式怎么选 / GPU 加速是什么 / 结果怎么用 /
  常见问题；措辞面向非技术用户（不使用「位掩码」「SIMD」等术语），并在用户手册 §2.0 同步。

### 13.6 UI 重构（任务书 4.2，功能零删减）

* **可折叠分组**：`makeCollapsibleGroup()` 把分组原有布局整体搬入子容器，
  由分组标题勾选框控制可见性 —— 只移动布局条目、不重建控件，因此信号连接、
  快捷键、自检断言全部不受影响；默认展开「游戏版本 / 搜索范围 / 搜索模式 / 并行计算」，
  默认收起「基岩版缓存管理 / 高级选项 / 查询起点」，并提供「全部收起 / 全部展开」一键切换。
  实测（700×920 默认窗口）：设置区竖向滚动量由全部展开的 607 px 降至 **467 px**，横向滚动条为 0。
* **结果区排序**：新增下拉框（按距离 / 按密度）；按密度时对结果副本做 `stable_sort`
  重新生成卡片，但 `m_lastResultText`（「复制结果」内容）始终保持引擎原始顺序，
  结果集合与复制内容不变。
* **保留项**：模式提示条 / 非精确模式警告条、逐结果「复制命令」、快捷键、预设按钮、
  坐标转换、缓存与 GPU 开关等全部保留（GUI 自检 13.8 断言关键控件仍存在且可用）。
* **对比截图**：`docs/screenshots/ui_v332_before_full-window.png`（v3.3.2 发布版，
  所有分组展开）与 `ui_v333_after_groups-default.png`（v3.3.3 默认折叠）、
  `ui_v333_after_groups-expanded.png` / `ui_v333_after_groups-collapsed.png`。




## 14. v3.3.4 升级说明（界面重构 + 四标签页 + 日志/终端 + 更新提醒，相对 v3.3.3）

任务书：`模板：原始文件/v3.3.4 完整任务书（最终版）.txt`。
**硬约束（已满足）**：不做折叠、不搬移布局条目、布局层级 ≤ 8、命令行 dispatch 模式、
命令优先级高于搜索线程、日志按日期分文件 + 节流刷新、更新提醒不联网。

### 14.1 界面结构（任务书 1）

* `QMainWindow` 原生接口：`menuBar()` / `addToolBar()` / `statusBar()`。
* `centralWidget` → `QVBoxLayout` → **垂直 `QSplitter`**：
  * 上：**水平 `QSplitter`** —— 左 `QWidget`→`QScrollArea`→6 个 `QGroupBox`；
    右 `QWidget`→信息栏（模式 + 排序 + 复制结果 + 统计）+ `QTextEdit`（只读结果文本）。
  * 下：`QTabWidget`（进度 / 反馈 / 日志 / 命令终端）。
* 尺寸：窗口 1280×860（最小 1000×650）、左栏 400（最小 340）、底部 180（最小 120）；
  自检在 1280×860 / 1000×650 / 1920×1080 与「splitter 拖到最左（340px）」四种状态下
  断言设置区**横向滚动条为 0**（消除错位）。为达成这一点，范围/算法等长行被拆分为多行
  （预设标签独占一行、密度参数两行、目标数量独立一行、分层/采样参数各两行），
  并把长后缀（如「× 所需史莱姆数」）移入 tooltip。
* v3.3.3 的 `makeCollapsibleGroup()`（把布局条目搬进子容器实现折叠）已**删除**，
  分组内容全部在目标分组内直接创建。

### 14.2 底部四标签页（任务书 2）

* **进度**：进度条 + 阶段文字（复用 v3.3.3 的 `progressStageText()`）+ 已用时 + 预计剩余
  （`QTimer` 200 ms 刷新，仅搜索中）。
* **反馈**（`src/gui/FeedbackPanel.*`）：`QTreeWidget` 四个固定顶级分组（错误/警告/信息/成功），
  组内最新在上、组标题带计数、空组隐藏、四个过滤按钮 + 右键菜单；`addSevere()` 追加错误并发出
  `severeAdded` → 主窗口切到该标签页并在非自检模式下弹窗。
* **日志**（`src/utils/LogBus.*` + `src/gui/LogPanel.*`）：默认 `Off`（不分配、不写文件），
  两级模式（Brief=INFO+ / Detailed=全部）；环形缓冲 10000 条 + 丢弃计数；
  文件 `logs/performance-YYYY-MM-DD.log`（按日期自动重开、约 1 s 缓冲刷盘、`Off` 时 flush 并关闭）；
  面板 500 ms 节流刷新、级别过滤、自动滚动、清空、复制全部；启用入口为「工具 → 启用日志…」
  与面板按钮，对话框文案与任务书逐字一致（详细模式带降速警告）。
* **命令终端**（`src/gui/CommandTerminal.*` + `MainWindow : CommandHost`）：解析在控件内，
  执行同步调用宿主（GUI 线程）→ **命令与搜索线程完全独立，立即生效可插队**；
  历史 100 条（↑/↓，存 config.json）、Tab 补全（命令名）、无系统命令。

### 14.3 更新提醒（任务书 3）

* `build_date.txt`（与 exe 同目录，ISO 日期；`scripts/release/package_release.ps1` 打包时写入，
  缺失/非法时退回内置默认日期）。
* `QDate::daysTo(QDate::currentDate()) > 30` → 工具栏徽章
  `🔔 本版本已发布 N 天，可能有新版本`；点击（或帮助 → 检查更新）打开下载页
  `https://wwben.lanzoul.com/b01d77eebe` 并提示访问密码 `3o8i`；帮助 → 更新历史列出各版本。

### 14.4 默认值与校验（任务书 4）

* 种子默认空：`search.seed` 写入 config.json；Java 版点击搜索时若为空/非法 →
  弹窗 [去填写]/[取消] + 反馈栏错误 + 日志 + 输入框高亮闪烁；`0` 合法、纯空格视为空、
  `123abc` 非法、基岩版不受影响。
* 目标数量默认 1：`search.targetCount` 写入 config.json。

### 14.5 验证

* GUI 自检新增：结构断言（菜单栏/工具栏/状态栏原生、双 Splitter、QScrollArea、6 分组不可折叠、
  右栏信息栏+QTextEdit、4 标签页名称与顺序、尺寸规范）、四种布局状态无横向滚动条、
  命令终端 15 条命令与插队、反馈分级、日志默认关闭与两级切换、更新提醒徽章条件、
  种子空/非法/`0`/空格四种情形。
* 完整回归：`scripts\release\verify_release.ps1`（clean 重建 + 单元 18 组 + 111/111 + OpenCL 探测 +
  内核自检 + GUI 自检 GPU 开/关 + 基准 + dist/zip 哈希），见 `docs/verification_report_v334.md`。


## 15. v3.3.5 修复与优化（相对 v3.3.4）

任务书：`模板：原始文件/v3.3.5 完整任务书（最终版）.txt`。**不新增依赖**（C++20 / Qt6 / OpenMP）。

### 15.1 Bug 修复

1. **Tab 补全抢焦点**：删除 `QCompleter` 弹出候选窗（`Qt::Popup` 会夺取键盘焦点），
   改为**内联补全** `applyInlineCompletion()`：唯一匹配→补全并加空格；多匹配→补到最长公共前缀，
   前缀无法增长则把候选打印到输出区；无匹配→不改内容。Tab 分支显式 `ke->accept(); return true;`
   并再次 `setFocus()`，因此焦点永不外移（自检连续 10 次 Tab 验证）。
2. **工具→GPU 状态误报不可用**：原实现只读 `m_gpuAvailable`（未勾选 GPU 时未探测→恒为不可用）。
   现改为**先复用搜索路径的探测** `ensureGpuScanOnce()`，再显示
   `✅ GPU 可用：设备（显存 MB，CU）` / `❌ GPU 不可用：未找到 OpenCL 设备`，
   并按可用性设置绿色/红色样式。
3. **教程游戏机制错误**：修正为「Y=40 以下、无亮度限制、约 1/10 区块、蘑菇岛/深暗之域不生成、
   沼泽 Y51~69 亮度 ≤7 是另一条途径、Java 依赖种子而基岩版与世界种子无关」
   （依据 zh.minecraft.wiki/w/史莱姆）。
4. **检查更新弹窗被浏览器遮盖**：改为**先弹窗后跳转**，弹窗含页面、密码 `3o8i`、说明与
   [取消] [复制链接] [打开浏览器]，仅用户点「打开浏览器」才 `QDesktopServices::openUrl()`。
5. **日志无法关闭**：工具菜单项动态切换（`updateLogMenuText()`）：未启用「启用日志…」/
   已启用「关闭日志」；关闭走 `LogBus::setMode(Off)`（flush 并关闭文件）+ 面板恢复提示。
6. **GPU 临界值严重失真**：原为静态估算（显存 × 1000 ÷ 核数 × 0.85）。现改为**实测基准**：
   `startGpuBenchmark()` 在后台线程对 CPU/GPU 各测两个规模（10 万与百万级，含预热、3 次取优），
   线性拟合 `t(n)=a+n/r` 后解 `a_cpu+n/r_cpu = a_gpu+n/r_gpu` 得**交叉点**，
   并把 `cpuRate/gpuRate/crossLo/crossHi/device/cores` 缓存进 `config.json`（硬件变化自动失效重测）。
   同时显示实测吞吐率（方案 B 信息）。CPU 端测量放在文件作用域函数 `benchCpuScanSeconds()`
   以使用 OpenMP（MSVC 不支持在 lambda 内用 pragma 引用捕获变量）。
7. **进度条与按钮间隔过宽**：进度标签页改为一行紧凑布局（`QHBoxLayout` + `setSpacing(8)`）：
   `[开始搜索] [取消] 进度: [bar] 65% 阶段文字`；按钮与工具栏按钮同步启用状态。

### 15.2 优化

1. **版本速度差异说明**：教程新增【为什么基岩版比 Java 版慢】（LCG ≈5 亿/秒 vs 完整 MT19937
   ≈3600 万/秒 + 624 步初始化），并给基岩版「开 GPU、开缓存」的建议。
2. **Splitter handle 可见**：两个分割条统一 4 px 样式（`#d0d0d0` + 边框，hover `#0078d7`）。
3. **查看菜单精简**：只保留「恢复默认布局」（Ctrl+0）。
4. **滚轮调整开关**：`installWheelFilter()` 给全部 `QSpinBox/QDoubleSpinBox` 安装事件过滤器，
   `MainWindow::eventFilter()` 拦截 `QEvent::Wheel`：开关关闭或未聚焦时 `accept()` 并 `return true`
   （不改数值）；开启且聚焦时放行。默认关闭。
5. **程序设置**：新增 `src/gui/SettingsDialog.{h,cpp}`（`AppSettings` + 五标签页
   QTabWidget：通用/搜索/缓存/日志/高级，每项含小字描述，[恢复默认][取消][应用]）；
   主窗口负责读写 `config.json` 的 `settings.*` 段并**立即生效**（滚轮开关、更新提醒天数、
   日志模式、缓存目录迁移、默认值），实现与主界面的**双向同步**（`loadAppSettings()` 启动时应用，
   `closeEvent()` 把主界面当前值写回为默认）。
   预留项（夜间模式/语言/提示音/快捷键/调试）以禁用控件占位。

### 15.3 污染审计与证据编码统一（v3.3.5 追加）

* **背景**：本项目历史上出过两次「单字符替换」事故——`MainWindow.cpp` 曾被整篇 `c`→`o`
  （`includes` 被写成 `ino·ludes`）、`install.nsi` 曾被整篇 `!`→`d`（NSIS 命令前缀失效），根因都是
  PowerShell 辅助脚本拿到被展平的嵌套数组后对整篇文本执行 `String.Replace`。
* **难点**：被**一致改名**的局部标识符（声明与全部使用处同错）不会让编译器报错，单元测试也全绿，
  词典式还原同样难以判定，只能靠词法审计兜底。v3.3.5 专项审计在 `MainWindow.cpp` 中发现并修复了
  4 处此类残留（`cacheWasEnabled`、`gpuWasChecked`、`c4`、临时缓存文件名字符串）与 3 处命名异常。
* **工具**：`scripts/audit_corruption.py`（7 类检查：已知坏片段 / 词典判定 / 跨文件还原验证 /
  稀有二元组特征 / 关键字符串常量 / 产物一致性 / 文件卫生），带退出码门禁与注入式灵敏度自测；
  报告见 `docs/corruption_audit_report.md`。
* **编码统一**：诊断输出改为**全链路 UTF-8** —— C++ 侧 `toUtf8()` / `u8string()` /
  `WideCharToMultiByte(CP_UTF8)`（GUI 自检、崩溃与异常报告），Python 侧
  `sys.stdout.reconfigure(encoding="utf-8")`，采集侧 `verify_release.ps1` 固定
  `[Console]::OutputEncoding`/`$OutputEncoding` 与 `PYTHONUTF8=1`；内核自检的「按字节截断」
  改为退回 UTF-8 字符边界。`scripts/export_evidence.ps1` 负责把 `%TEMP%` 日志按 BOM 解码并
  统一写成 UTF-8，同时统计 `U+FFFD`/NUL（非零即失败）。修复前证据文件共 18 处编码缺陷，
  修复后为 0。

## 16. v3.3.6 修复与界面/设置升级（相对 v3.3.5）

### 16.1 Bug 修复

1. **删除重复的搜索按钮**：v3.3.5 在底部「进度」标签页里额外放了一组 `[开始搜索][取消]`，
   与工具栏按钮重复。v3.3.6 删除该组，**全界面只保留工具栏里唯一一个「开始搜索」**
   （自检递归统计 `QPushButton` 文本断言数量 == 1）。
2. **彻底移除 GPU 临界值**：用户判定静态公式与实测交叉点都不准，因此本版本删除
   `gpuThresholdChunks()` / `gpuThresholdText()` / `gpuSmallRangeHintText()` /
   `updateGpuThresholdUi()`、`gpuBench.crossLo/crossHi` 配置字段、状态栏「范围较小建议关闭 GPU」
   分支与全部「临界值/交叉点」文案。保留：
   * GPU 开关 tooltip 与「工具 → GPU 状态」对话框中的统一提醒
     「GPU 加速不一定比纯 CPU 强。实际效果取决于你的硬件配置和搜索范围，建议实际测试后决定是否启用。」；
   * 「GPU 状态」仍显示设备名、显存、计算单元数与**实测吞吐率（CPU vs GPU）**（后台两档测量、
     拟合固定开销 + 吞吐，结果缓存 `gpuBench.cpuRate/gpuRate/device/cores`）。
   自检新增**全界面文本扫描**：递归遍历所有控件与 `QAction` 的 text/tooltip，断言不含该说法。
3. **设置入口移到工具栏**：工具栏最左侧新增 `[⚙ 设置]`（文字 + 齿轮），点击打开五标签页
   `SettingsDialog`；「工具」菜单里的「程序设置…」已删除。自检断言按钮位置（x 坐标小于
   「开始搜索」）、父控件为工具栏、点击后进入设置入口（自检模式只记录不弹模态框）。

### 16.2 界面调整

1. **进度行移到工具栏下方**：中央部件根部新增 `#progressRow`（固定高 28px，横跨整窗），
   内容为 `进度: [QProgressBar] 65% 阶段文字`；已用时/预计剩余/阶段说明移到状态栏
   （`progressElapsed` / `progressEta` / `progressInfo`）。自检断言 y 坐标顺序
   （工具栏 < 进度行 < 主内容区）、高度 28 与宽度覆盖整窗。
2. **底部面板：左竖排导航 + 右内容区**（替换 v3.3.4 的 `QTabWidget`）：
   `#navList`（`QListWidget`，固定宽 120px）+ `#bottomStack`（`QStackedWidget`，四页
   进度/反馈/日志/命令）。当前项 `●` + 加粗 + 高亮背景，未选中 `○`；
   **只有「反馈」显示计数**，文本由 `FeedbackPanel::badgeText(warnings, errors)` 生成
   （`⚠️2❌1` / `⚠️2` / `❌1` / 空），只统计警告与错误。
3. **「进度」分类 = 累积式纯文字流水**（新增 `src/gui/ProgressStreamPanel.{h,cpp}`）：
   格式 `[HH:MM:SS] 已用时 T 秒，扫描 N 区块，找到 M 个`，由 `m_streamTimer`
   （500ms，仅在搜索中）驱动；完成时追加 `[HH:MM:SS] 已完成：共扫描 N 区块，找到 M 个结果，耗时 T 秒`，
   取消/出错各追加一条说明行；面板提供 `[清空] [复制全部]`。
   为使「找到 M 个」是真实数据，`SearchOptions` 新增 `std::atomic<int64_t>* liveMatches`，
   引擎在记录每个命中时以 relaxed 原子自增（**只计数，不参与任何判定**，111/111 对照与全部
   回归实测未变）；「扫描 N 区块」由进度比例 × 本次范围总区块数推算。
4. **「反馈」分类改版**（重写 `FeedbackPanel`）：扁平单列列表（无分组节点）、**时间顺序
   （最新在下方）**、**成功级直接忽略**、行格式 `❌ HH:MM:SS 消息`（保留 ❌/⚠️/ℹ️ 图标与
   严重度颜色）、**取消全部过滤按钮**，只留 `[清空] [复制全部]`；上限 2000 条时丢弃最旧。
5. **「日志」分类**：保持「关闭时只有一行提示」，并在设置里新增自动滚动与界面最大行数
   （`LogPanel::setAutoScroll/setMaxLines`，超过上限从文档开头丢弃最旧行）。
6. **「命令」分类**：功能不变（命令终端可插队）。

### 16.3 设置项扩展（`AppSettings` 25 项，五标签页）

新增 9 项：`theme`（light/dark/system）、`soundOnComplete`、`soundVolume`（0~100）、
`showGpuReminder`、`largeRangeWarnChunks`（默认 1 亿，超过时 `startSearch` 弹窗确认）、
`logAutoScroll`、`logMaxLines`（默认 10000）、`crashDump`、`debugMode`。
全部经 `config.json` 的 `settings.*` 段双向同步（自检断言 9 项 set/写盘/读回一致）。
「高级 → 重置所有设置」把五页恢复出厂默认值（仍需点「应用」才生效）。

### 16.4 夜间模式（新增 `src/utils/ThemeManager.{h,cpp}`）

三档 `ThemeMode{Light, Dark, System}`：`apply(qApp, mode)` 立即设置应用级 QSS（无需重启）；
`systemPrefersDark()` 读取注册表
`HKCU\Software\Microsoft\Windows\CurrentVersion\Themes\Personalize\AppsUseLightTheme`
（0 = 深色，1 = 浅色），并在 `System` 模式下监听 `QStyleHints::colorSchemeChanged`
（Qt 6.5+）自动跟随。深色 QSS 覆盖任务书列出的全部控件类别（含 `QMenuBar/QMenu/QToolBar`、
各滚动条子控件、`QSplitter::handle`）以及本程序的对象名规则
（`#navList` / `#progressRow` / `#updateBadge` / `#feedbackList` / `#progressStream`）。
主配色 `#1e1e1e` / `#e6e6e6`（对比度 13.36:1），强调色 `#0e639c` / 白字（6.40:1），
`ThemeManager::contrastOk()` 供自检做对比度断言。
配套清理：删除会覆盖全局 QSS 的内联样式（分割条、更新徽章），其余状态色（说明文字、
模式警告条、GPU 状态、种子高亮）改为随主题切换的 `applyInlineThemeColors()`。

### 16.5 提示音（新增 `src/utils/SoundPlayer.{h,cpp}`）

Qt Multimedia 未安装，故用 Windows `waveOut`（winmm）在内存中合成 300ms 三音和弦
（C6/E6/G6，44.1kHz/16bit/单声道，含攻击/释放包络与整体淡出），振幅按音量百分比缩放；
异步播放、下次播放前回收上一份缓冲区、失败静默、进程退出时由静态持有者释放。
设置页的音量滑块直接映射到该振幅。

### 16.6 验证

* GUI 自检新增第 [14] 组（搜索按钮数量、全界面无被移除字样、提醒文案、设置入口、进度行位置与尺寸、
  底部导航结构与计数规则、流水格式与追加、反馈顺序与过滤、日志提示、主题三档与 QSS 覆盖与对比度、
  9 项设置往返与立即生效、大范围阈值分支），另有历史各组全部通过。
* 完整回归：clean 重建 0 error、单元 19 组、111/111 参考对照、OpenCL 探测 PASS、内核自检 17/17、
  GUI 自检（GPU 关/开）exit 0、基准、dist/zip 哈希一致、污染审计 CLEAN。
* 截图：`docs/screenshots/v336/`（37 张 = 浅色 27 + 深色 10）。

## 17. v3.3.7 紧急修复（大范围搜索，相对 v3.3.6）

> 靶子：用户实测「基岩版 · 严格图案 · ±104857 区块（439 亿）· 目标 1 · 12 线程 · 未启用 GPU」暴露的
> 4 个 P0 + 2 个 P1 缺陷。根因分析与实测数据见 `docs/v337_rootcause_analysis.md` 与
> `docs/v337_rootcause_evidence.txt`；用户确认的取舍：**B 方案 / 1.5GB 位图预算 / 接受 N/M（xx%）显示**。
> 验证报告：`docs/verification_report_v337.md`。

### 17.1 P0-1 及时反馈（「达到目标数量即停止」在分块路径失效）

* **原因**：`runChunked` 硬编码 `sub.stopAtTarget = false`（v3.1.1/v3.1.2 为保「全局最近 N」而禁用），
  且 worker 循环只检查 `cancel`/块耗尽，`totalFound` 只累加不消费 → 439 亿场景勾选完全失效。
* **修复（B 方案，语义无损）**：
  1. 块内恢复几何提前停止（`sub.stopAtTarget = opt.stopAtTarget`）——块内停止条件
     「本块剩余区域不可能更近」对全局最近 N 是安全的（被跳过的窗口距离 ≥ 本块第 N 名 ≥ 全局第 N 名）；
  2. **跨块剪枝**：worker 取块前判定 `chunkCannotImprove(ci)` —— 堆满且「起点到该块窗口中心矩形的最小距离」
     **严格大于**当前第 N 名距离时跳过该块（`chunksPruned` 计数，`out.stoppedEarly` 置位）。
     第 N 名距离放在 `std::atomic<double>` 中（只会变小 → 陈旧值只会少跳、不会误跳）；
  3. `SLIME_NO_PRUNE=1` 仅供 A/B 对照（关闭剪枝），用于证明结果逐位一致。
* **实测**：63 分块场景 41.0 s → **7.0 s**（跳过 51 块）；另一组 314,572,500 → 267,596,340 区块，
  结果逐字节一致；单元测试第 [19] 组锁定该行为（含「未勾选不剪枝」与「结果逐位一致」断言）。

### 17.2 P0-2 取消后崩溃（生命周期与状态机）

* **修复**：
  1. `~MainWindow` **无条件** `m_task.cancel(); m_task.wait();`（旧写法 `if (running())` 会漏掉
     「线程已置 running=false、尚未投递结果回调」的窗口，从而在成员析构期间回调 GUI）；
  2. 实时计数 `liveMatches`/`liveScanned` 改由 `SearchTask` 以 `shared_ptr<std::atomic<int64_t>>` **持有**，
     工作线程再捕获一份 → 计数对象生命周期与引擎调用绑定，不再受调用方成员析构顺序影响；
  3. 每次运行分配 `runId`，结果事件携带编号并在 `onSearchFinished` 内**二次校验**，陈旧事件直接丢弃
     （避免「取消后立刻重搜」时旧事件把新搜索标记为结束、甚至 join 到新线程）；
  4. 搜索进行中重复点「开始搜索」的提示框在自检/自动化模式下不再弹出（实测该模态框会永久阻塞 GUI 线程）。
* **实测**：连续取消 10 次（全部命中「搜索中」）最差响应 **86.3 ms**、无崩溃；陈旧事件被丢弃后新搜索不受影响。

### 17.3 P0-3 扫描计数停滞（块内无进度）

* **原因**：分块路径只在**每块结束时**上报一次进度（`runImpl` 以 `progress=nullptr` 调用），
  单个 4882 行块体实测耗时 83~106 秒 → 计数/进度条每分钟才跳一次，而 500ms 流水仍按节拍输出。
* **修复**：① `Emitter` 把扫描进度累加到 `SearchOptions::liveScanned`（真实计数，CAS 单调累加以避免
  OpenMP 乱序上报导致的重复计数）；② 分块路径给 `runImpl` 传入节流回调（≤4Hz），
  以 `liveScanned / (plan.totalDataEvals × W)` 作为**全局进度**，块内持续更新；
  ③ GUI 直接读取该计数：状态栏显示「扫描 N/M 区块（xx%）」、流水行同源显示 `N/M（xx%）`；
  ④ 结束统计 `out.stats.scannedChunks = liveScanned`（与显示完全一致）。

### 17.4 P0-4 内存占用（并发 × 128MB 位图）

* **修复**：`chunkConcurrencyFor(threads, perChunkBytes)` 引入**内存预算**（默认 1.5GB，
  `SLIME_CHUNK_BUDGET_MB` 可覆盖）：`conc = clamp(min(threads, 12), 1, budget / perChunkBytes)`；
  基岩版+缓存路径由「收集分块片段（最多保留 1GB）」改为**逐块即时 `mergeRegion` 并立即释放**，
  峰值从「并发 × 单块条目 + 1GB 保留」降为「并发 × 单块条目」。
* **实测**：默认预算 12 并发 → 峰值 **1479 MB**；`SLIME_CHUNK_BUDGET_MB=512` → 4 并发 → **499 MB**
  （耗时 116 s → 284 s，作为「更省内存但更慢」的对照记录在案）。

### 17.5 P1-5 UI 重复显示 + P1-6 崩溃诊断

* **P1-5**：删除 `progressStageText()`（一次返回「百分比+速度+ETA」），拆为
  `progressStageName(stage)`（进度行只显示阶段短语）与 `progressStatsText(scanned,total,cps,found)`
  （状态栏显示统计信息）。百分比唯一来源 = `progressPercent`；「预计剩余」唯一来源 = 状态栏 `progressEta`。
  自检断言：进度行 `%` 计数 == 1、状态栏「预计剩余」计数 == 1。
* **P1-6**：`CrashDump` 新增 `setEnabled()`（由设置接线，默认开）、`writeDumpNow()`（**首次机会**写 dump，
  只写一次）与 `dumpDir()`；`main()` 增加 `_set_invalid_parameter_handler`（CRT 非法参数默认是 fast-fail、
  无 dump）与 `std::set_new_handler`（内存分配失败）；VEH 对
  `ACCESS_VIOLATION / ILLEGAL_INSTRUCTION / IN_PAGE_ERROR / PRIV_INSTRUCTION / STACK_OVERFLOW /
  HEAP_CORRUPTION / FAST_FAIL` 调 `writeDumpNow()`。
  新增 `utils/IncidentLog`（常开关键信息日志 `logs/crash-YYYY-MM-DD.log` + 会话标记）：
  记录启动环境、搜索参数、每 ~5 秒状态环与内存、取消/完成/错误/崩溃事件；上次会话异常结束时
  在下次启动于反馈栏与状态栏提示。为此新增 `SLIME_CRASH_TEST` 自检钩子（默认关闭）用于验证 4 类故障都能留 dump。

### 17.6 验证

* 单元测试新增 [19]（分块提前停止 + 真实扫描计数，含 A/B 对照），共 **19 组**；
* GUI 自检新增 [15]（进度去重、取消压力 10 次、陈旧事件、转储接线、关键日志）；
* 全量回归：clean 重建 0 error、19 组单元、111/111、OpenCL PASS、内核 17/17、GUI 开/关 exit 0、
  基准、dist/zip 哈希一致、污染审计 CLEAN（审计新增「源码版本常量漂移」检查 —— 正是本轮发现
  `kAppVersion` 漏改的那类问题）。

## 18. v3.3.8 缓存正确性修复 + 历史缓存处置 + 性能分解（相对 v3.3.7.2）

> 任务书依据：`docs/v338_eta_and_cache_audit.md`（任务 1 根因 / 任务 2 缓存七问）、
> `docs/v338_cache_bitmap_design*.md`（任务 3+5 设计）；过程与取证：`docs/v338_progress.md`（三-a ~ 三-i）；
> 冷启动交接：`docs/v338_handover.md`；验证报告：`docs/verification_report_v338.md`。
> **严格模式结果与 v3.3.7.2 逐位一致（111/111）**；缓存文件格式仍为 v1（v2 位图化留待下一阶段）。

### 18.1 ④′ 根因与修复（覆盖声明与条目同步）

* **根因（实测修正版）**：`pruneToFit` 整 range 丢条目后，**跨运行**再次登记同一区域时仍写入声明，
  而该声明只剩「与相邻声明重叠的 4 行带」的 ~84k 条支撑（4×209715×10% ≈ 83,886），
  其余 758 行（1.58e8 位置）无支撑却因 `covers()==true` 永不被重扫 ⇒ 该带 ≈99% 史莱姆区块被漏判
  （真实 1.02 GB 缓存：8 个健康声明 + 4 个无支撑声明；E2 一万点仅 96.67%、假阳性 0）。
* **改动 1（引擎分块路径）**：`runChunkBody` 内 `pendingRanges.push_back(...)` **移入**
  `if (!so.newCacheChunks.empty())`；空则计 `hollowSkips`（`search.summary ... hollowSkips=N` +
  `cache.hollow 跳过覆盖声明 N 个块`）。并发安全性已取证：`so` 是 `runChunkBody` 函数体局部，
  且 `runImpl` 的收集先写 per-OpenMP-thread 缓冲、并行区结束后单线程合并。
* **改动 2（GUI 经典路径）**：`onSearchFinished` 的 `mergeRegion` 改为条件调用（0 新增条目不登记 + 日志）。
* **改动 3（`CacheManager::dropHollowRanges`）**：**支撑度判据 B** —— 声明内条目数 ≥ 期望条目数 × 50%
  （期望 = 面积 × 10%），期望 < 100 条的小面积声明放行；两趟扫描（`owned` 首包含早退判定上界 +
  仅对未达标者精算 `anyCnt`）；调用点 = `loadFile()` 末尾 + `saveFile()` 开头（`saveFile` 由 const 转非 const）。
  不删条目、不改格式；被剔区域 `lookup()==-1`（下次重扫）。
* **验收**：1.02 GB 缓存加载后 **正好剔除 #4/#5/#6/#7 ⇒ 8 声明**、条目 127,842,361 **不变**、
  **E2 100.00%**、逐声明命中率 100%（旧#3 99.49%，残留 4 行重叠带 0.51% 记 backlog B-9）；
  尺度实验（6.63 亿区块 / 11 range）命中率 69.86% ≈ 保留**面积** 69.8%、假阳性 0、清扫后 100% ⇒ 判据闭环。
* **改动 4（测试）**：`tests/test_slime.cpp` 新组 `[20]`（旧行为锚点 / 判据边界 / 小面积放行 /
  改动 1 的确定性验证 / 并发收集完整性），单元共 **20 组**。

### 18.2 候选 ②：登记域 = 锚点行域

分块路径的覆盖声明由 `sub.region()`（762 数据行）改为 `[start, start+winRows-1]`（**758 锚点行**），
与 `g_anchorMaxAZ` 限定的匹配域口径一致、相邻块声明不再重叠；代价是重复搜索时每块 0.5% 行重算。
单元 `[12]` 断言同步更新（含新增「相邻块声明不重叠」）。

### 18.3 历史缓存方案 A（`.legacy` 一次性迁移）

`MainWindow.cpp` 新增 `migrateLegacyCachesOnce(dir, keepDays)`（纯文件操作，自检可直接断言）：
标记文件 `slime_cache.migrated` 不存在时把 `slime_cache.slimecache` / `slime_cache.cache` 改名 `*.legacy`、
**本次不加载**、反馈栏/状态栏/弹窗提示「漏存 ~34%，建议重扫」、7 天后自动清理；
导入过滤器加入 `*.legacy`（显式导入即用户确认使用，导入后仍走支撑度清扫）。

### 18.4 任务 6 / E3：填充与密度滑窗独立计时

`SearchStats` 新增 `fillMs` / `slideWindowMs`（滑窗为多线程累加 ⇒ 线程·毫秒），
`search.summary` 新增 `fill=` / `slideWindow=` / `rowsPerChunk=` 字段，并新增 `SLIME_FILL_ONLY=1`
（只填充、跳过匹配）与把 `SLIME_CHUNK_WORDS` 下限放宽到 64 Ki words（E3-3 的 2 MB/块位图需要）。
**实测结论**：填充 47% / 滑窗 53%（墙钟）；**每块位图 20 MB → 2 MB 对滑窗耗时无收益（+3.2%）**
⇒ 任务 6 应走**方向 B（改内核访存）**。

### 18.5 v2 位图缓存格式（任务 3+5 第 2 步）

* **内存**：`m_ranges` + 每 range 一张位图（行对齐 `ceil(W/64)` 字 × H 行，与引擎 grid 的 wpr/rows 同构）
  + `m_outliers`（不在任何声明内的条目，升序去重）+ `m_uniqueEntries`；每个坐标只置位在**第一个包含它的 range**
  （`lookup()` 也只查该 range）；`pruneToFit` 丢声明前把「仍被其它声明覆盖」的位**改挂**过去（保留旧的过滤语义）；
  `dropHollowRanges` 的支撑度判据改由**一次 popcount** 得出（精确、无 O(N×R) 遍历）。
* **磁盘 v2**：`24 B 头（magic / ver=2 / rangeCount / 保留 / outlierCount）+ 每 range（16 B 矩形 + 4 B 字数 + 位图）
  + outlier 坐标段`；**v1 仍可读**（坐标集合读入即转位图，首次保存自动升级为 v2）；写入走**原子替换**
  （`.new` → 回读校验（范围数/条目数 + 每 range 16 点抽样）→ `ReplaceFileW`，失败退 `MoveFileExW`）；
  写入或校验失败 ⇒ 删 `.new`、旧文件不动、返回明确错误。防御：单边 > 2^26 或面积 > 2^33 的范围拒绝加载。
* **实测**（真机 1.02 GB v1 缓存）：文件 **975 → 152.4 MiB（6.40× 更小）**、加载 **5,965 → 251 ms（23.8× 更快）**、
  清扫 1~2.5 s → **0.05 s**、一万点真值一致率仍 **100.00%**；单元 `[21]` 覆盖 v1 兼容与「写入失败不损坏旧文件」。
* **第 3 步（已完成）**：`runImpl` 在填充前按行查缓存位图（同 X 跨度、同 wpr），命中即 `memcpy` 该行并计入`cacheHitPoints`/`cacheFillSkipped`（不收集新条目）；因分块路径 `cacheWrite == cache`，每次按 idx **重新取指针并校验矩形未变**，不一致即回退。
  A/B（2048×20000，12 线程）：**墙钟 223 → 22 ms（10.22×）**、fill 201 → 2 ms、跳过 4096 万点、结果逐位一致；`scannedChunks` 口径不变。
* **任务 4（已完成）**：`runChunked` 在启用提前停止且存在读缓存时，把「锚点行域被缓存覆盖」的块排到处理顺序最前
  （`cacheFirstOrder`/`cacheFirstChunks`，日志 `cacheFirst=1/N`），TopN 堆满后由既有跨块剪枝 `chunkCannotImprove`
  跳过剩余未覆盖块；单元 `[23]`：剪枝 2 块、扫描量 **31.8%**、结果与全量扫描逐位一致。
* **任务 6（未达标，如实声明）**：E3 分解 fill 47% / slide 53% ⇒ 444.8 s 降到 ≤221 s 需两段各约 2×；
  缓存直取只对重复搜索有效。候选路径见 `docs/verification_report_v338.md` §五-e 与 `docs/v338_backlog.md` B-12。
