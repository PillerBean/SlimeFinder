# v3.3.1 验证报告（缓存扩展名修复 + 基岩/AVX2 优化；GPU 密度内核暂停）

日期：2026-09-20 ｜ 基线：v3.3.0 ｜ 任务书：`模板：原始文件/v3.3.1 最终任务书.txt`
机器：AMD Ryzen 5 5600（6C/12T，AVX2 可用）+ AMD Radeon RX 6750 GRE 10GB
构建：MSVC 14.51 / Release `/O2 /openmp /GL /LTCG /utf-8`；AVX2 内核为独立编译单元（`/arch:AVX2`）

> 全部性能数字为**同机、同构建、同参数**实测；A/B 通过环境变量在同一次运行内切换
> （`SLIME_DENS_KERNEL`、`SLIME_MT_TABLE`、`SLIME_DENSITY_MODE`、`SLIME_GPU_MATCH`），
> 或使用归档二进制 `bench_v321/slime_tests_v321.exe`（v3.2.1）。原始输出见 `docs/` 下同名 txt。

---

## 一、验收结论（速览）

| 任务书验收项 | 目标 | 实测 | 结论 |
|---|---|---|---|
| 缓存 Bug 修复：保存后能正常加载 | 必须 | `.slimecache` 保存→重载往返一致；旧 `.cache` 可加载并自动迁移（日志实证） | ✔ |
| 旧格式缓存能被识别 | 必须 | 启动时仅存在 `slime_cache.cache` → 自动加载 1 区域/1639 区块并另存新版 | ✔ |
| 基岩版匹配提速（密度） | ≥ 2× | **匹配阶段 6.41×**（同二进制 A/B：AVX2 内核 30.3 ms vs 逐窗 countBits 194.2 ms，基岩 g=5 4000²，命中 7491，结果一致） | ✔ |
| 基岩版匹配提速（严格） | ≥ 1.5× | 严格路径未改动；v3.3.0 起同二进制 A/B 为 13~15×（本次回归复测 **13.57~16.91×**） | ✔ |
| 附加：基岩版采样扫描提速 | —（任务书 2.2 意图） | **20.15×**（跨步 AVX2 批量 MT19937；已接入采样模式） | ✔ |
| ~~GPU 密度匹配全图 ≤ 20 分钟~~ | ~~≤ 20 min~~ | **按指示暂停开发，不在 v3.3.1 交付范围**：全部代码（含未完成调试代码）保留在 `src/gpu/experimental/`，作为 v3.4.0 起点，见 §5 | 不适用 |
| 结果与 v3.3.0 100% 一致 | 必须 | 单元 18 组 + 111/111 + A/B 各项「结果一致=是」 | ✔ |
| 单元测试全部通过 | 必须 | 见 §6（含 [17] GPU 密度匹配一致性、[18] 引擎侧集成/分块锚点域） | ✔ |
| GUI 自检通过（含 GPU 开/关） | 必须 | 见 §6（GPU 内核编译 + 真实设备扫描一致性实测通过） | ✔ |
| 111/111 参考对照 | 必须 | 111/111 通过，0 失败 | ✔ |

---

## 二、P0-2.1 缓存扩展名统一（`.slimecache`）

**问题**：保存使用 `slime_cache.cache`，而导入对话框过滤器仅 `*.cache`；用户按文档/旧版本
产出的缓存（不同扩展名）在对话框中不可见 —— 表现为「缓存文件存在但选不中」。

**修复**（`src/gui/MainWindow.cpp`、`scripts/convert_pkl_cache.py`、文档）：
* 常量集中：`kCacheFileName = "slime_cache.slimecache"`、`kCacheLegacyFileName = "slime_cache.cache"`；
  保存/加载/清除/状态显示全部走同一路径；
* 启动：优先加载 `.slimecache`；若不存在而旧 `.cache` 存在 → 自动加载并**另存为新格式**
  （旧文件保留，用户可自行清理）；
* 导入对话框过滤器（`cacheImportFilter()`，与 `importCache` 共用）：
  `史莱姆区块缓存 (*.slimecache *.cache)` / `新版缓存 (*.slimecache)` / `旧版缓存 (*.cache)` /
  `所有文件 (*.*)`；
* `.pkl` 旧缓存提示文案改为「转换为 .slimecache」；`convert_pkl_cache.py` 默认输出扩展名同步为 `.slimecache`；
* 清除缓存同时删除旧扩展名文件（避免下次启动又被当作旧缓存加载）。

**实测证据**（`docs/v331_cache_evidence.txt`）：
```
[INFO] 启动加载旧版缓存成功（slime_cache.cache）：1 区域 / 1639 区块
[INFO] 旧版缓存已迁移为新格式：slime_cache.slimecache
```
运行后目录同时存在 `slime_cache.cache`（旧，保留）与 `slime_cache.slimecache`（新，程序使用）。
GUI 自检 [12] 覆盖：常量与过滤器字符串、`.slimecache` 保存→重载往返内容一致、旧 `.cache` 兼容加载、
非缓存文件被拒绝并给出原因；`.pkl → .slimecache` 转换脚本默认输出亦已实测。

## 三、P0-2.2 基岩版种子查表（结论：不更快，默认关闭）

**实现**（`src/core/FastMt.{h,cpp}`，只增不改语义）：
`bedrockChunkSeedTable(cx, czu)` —— 预计算低 16 位表 `T[lo] = low32(lo*K)`（65536×4B = 256KB）+
高位拼接 `hi * low32(K<<16)`；配套 `setBedrockSeedTableEnabled()`（默认关，`SLIME_MT_TABLE=1` 可开）
与 `bedrockSlimeScanScalarTable()` 对照实现。**与直接乘法逐位一致**（20 万随机 + 80 组边界用例）。

**实测 A/B（同机，单线程，best of 3）**：

| 口径 | 直接乘法 | 查表 | 比值（查表/乘法） |
|---|---|---|---|
| 2000 万区块连续扫描（含 MT） | 1.816 M/s | 1.826 M/s | **1.005×**（另两次 0.985× / 0.993×，属噪声） |
| 纯种子计算 6400 万次（不含 MT） | 907 M/s（1.10 ns） | 551 M/s（1.81 ns） | **0.73×（查表更慢）** |

**原因（如实说明）**：单区块 MT19937 成本 ≈550 ns，种子计算仅 ≈1.1 ns（占比 0.2%）；
查表版多出 `and/shr/imul/add` 与一次 256KB 表（超出 L1）的依赖加载，指令数反而更多。
任务书「匹配阶段每次计算 umul32_lo，提速 30~50%」的前提在本实现中不成立：
v3.3.0 起基岩**匹配阶段工作在位图上**（O(1)/窗口），种子乘法只出现在**扫描阶段**与单区块
判定 API，且扫描已由 AVX2 批量 MT19937 承担。**查表实现保留为可选/对照路径，默认关闭。**

## 四、P0 附加 / P1-3.1：基岩版采样扫描与 AVX2 窗口密度统计

### 4.1 跨步采样扫描（AVX2 批量 MT19937）—— 已接入，实测 20.15×

采样估算模式此前对每个采样区块逐点调用标量判定（≈1.3~1.8 M/s）。新增
`fastmt::bedrockSlimeScanStrided(cx0, czu, count, step, out)`（4/8 通道批量，步长任意），
引擎采样扫描对基岩版改为整行调用（快速估算用跨步版；精算采样用连续段批量版）。

| 口径（2000 万采样点，step=8，单线程） | 吞吐 | 加速 |
|---|---|---|
| 标量逐点 MT（旧路径） | 1.788 M/s | 1× |
| **AVX2 跨步批量（新路径）** | **36.04 M/s** | **20.15×** |
| step=2 / step=64 | 34.2 / 34.9 M/s | ≈19~20× |

一致性：step∈{1,2,3,8,64} × 13 个基址（含负坐标与 int32 回绕）× count 0..1000 共 1560 用例
+ 300 组随机用例，与独立教科书 MT19937 参考**逐位一致**；引擎级测试（单元 [15] 4) 另验证
「快速估算/精算采样」结果在 AVX2 与 `SLIME_FORCE_SCALAR=1` 标量 MT 下完全相同。

### 4.2 AVX2 批量窗口密度计数内核（任务书 3.1）—— 已接入，实测 5.75~31.6×

新增 `slime::densityWindowCountsAvx2(rows, g, wordsPerRow, axBegin, axEnd, out)`（「列和 + 前缀和」
方案：对 g 行做竖直位累加后求列前缀，窗口计数 = `pref[x+g]-pref[x]`），与朴素逐窗口 popcount
**逐位一致**（298 组配置 / 384390 窗口 + 结构化位图 6321 窗口 + 非法参数用例，全部通过）。

**内核微基准**（64 行 × 512 字 = 32768 列，锚点覆盖全部窗口）：

| g | 朴素（逐窗口 popcount） | 交付标量实现 | AVX2 内核 | 加速比 |
|---|---|---|---|---|
| 5 | 16.5 ns/窗口 | 8.5 ns | **0.9 ns** | 18.8× / 9.6× |
| 10 | 20.3 ns | 20.6 ns | **1.1 ns** | 18.1× / 18.4× |
| 32 | 98.7 ns | 75.9 ns | **2.7 ns** | 37.1× / 28.5× |

**引擎级 A/B**（`slime_tests --bench` 新增段；无滑动资格路径 = 提前停止，`SLIME_DENS_KERNEL` 开关；
20000² 单核，两侧均扫全区域不再提前停止；下方为最终构建实测，证据 `docs/bench_v331.txt`）：

| 配置 | 内核 | 逐窗 countBits | 匹配阶段加速 | 合计加速 | 结果 |
|---|---|---|---|---|---|
| Java g=5 | 761.1 ms | 4884.3 ms | **6.42×** | 3.88× | 一致（命中 188366） |
| Java g=10 | 761.9 ms | 9489.4 ms | **12.46×** | 7.17× | 一致（命中 0） |
| Java g=32 | 944.0 ms | 26280.5 ms | **27.84×** | 16.80× | 一致（命中 0） |
| **基岩 g=5（4000²）** | **30.3 ms** | **194.2 ms** | **6.41×** | 1.33× | 一致（命中 7491） |

**接入范围（依据实测）**：内核只用于**无滑动资格**的密度路径（提前停止 / 分层未剪枝段）。
精确模式默认的滑动路径仍保留，因为同一条件下滑动更快：
「密度 g=5 20000² 单核 滑动路径 匹配 602.7 ms | 合计 1258.0 ms」 vs
「内核 匹配 761.1 ms | 合计 1450 ms 量级」（滑动快 ≈1.26×，g=16/32 时滑动优势更大）
——两条路径结果逐位一致（命中数同为 188366），属纯性能取舍。
`SLIME_DENS_KERNEL=0` 可关闭本优化（A/B 用）。

## 五、P1-3.2 GPU 密度匹配内核（按指示暂停，v3.3.1 不交付）

### 5.1 决定与当前状态

**决定（交付指示）**：GPU 密度匹配内核的开发在本版本**暂停**。v3.3.1 只交付
①缓存扩展名修复 ②基岩种子查表（实验结论：不提速，默认关闭）③基岩 SIMD 窗口统计
（AVX2 密度内核 + 跨步采样扫描）④构建期嵌入/依赖跟踪修复（见 §5.3）。
任务书中「GPU 密度匹配全图 ≤ 20 分钟」一项**不再作为 v3.3.1 验收项**。

**代码去向**（不删除、不再继续调试）：

| 文件 | 内容 |
|---|---|
| `src/gpu/experimental/GpuSession.h.wip` / `.cpp.wip` | 含密度匹配的会话实现（缓冲区、分块、去重、TopN） |
| `src/gpu/experimental/slime_kernel.cl.wip` | 密度匹配相关 OpenCL 内核（`scan_*_bitmap` / `density_count_emit*`） |
| `src/gpu/experimental/GpuScanAdapter.cpp.wip` | 覆盖 `matchDensity` 的适配器版本 |
| `src/gpu/experimental/gpu_density_selftest.cpp` | 独立自检（真实设备一致性 + 性能） |
| `src/gpu/experimental/README.md` | 当前状态、已知缺陷、v3.4.0 恢复步骤（含 CMake 片段） |

`src/gpu/` 已裁剪为**仅扫描**（`scanJava` / `scanBedrock` 与 v3.3.0 语义逐位一致），
构建中不再包含任何密度内核代码，`slime_gpu_density_tests` 目标已移除。
**暂停原因（如实记录）**：开发中的密度内核曾破坏 Java 位图判定内核的正确性
（`scan_*_bitmap` 路径 128 行中 127 行与引擎不符），并伴随驱动侧堆损坏；
为避免影响 v3.3.1 的「结果与 v3.3.0 100% 一致」底线，按指示整体移出交付范围。

### 5.2 已就绪的引擎侧接口与测试（v3.4.0 可直接复用）

* 接口：`slime::GpuScan::matchDensity(const DensityMatchParams&, const std::atomic<bool>& cancel,
  std::vector<MatchResult>& out, std::string* err)`，**默认实现返回 false（= 不支持）**，
  因此当前构建下引擎自动走 CPU 匹配路径，行为与 v3.3.0 一致；
  `DensityMatchParams`（版本 / 种子 / 锚点范围 / g / 阈值 / `maxResults`）已定型。
* 引擎接入点：`SlimeEngine::runImpl` 阶段 1a（位图分配前），启用条件 = 密度 + 精确算法 +
  非智能范围 + 未提前停止 + 非采样 + 未启用基岩缓存 + GPU 可用 + 区域达阈值；
  `SLIME_GPU_MATCH=0` 可单独关闭（保留 GPU 判定扫描）。
* 语义契约（v3.4.0 实现必须遵守）：命中集合 = `{锚点 | 窗口内史莱姆数 ≥ minDensity·g²}`，
  `density = (float)count/(float)(g·g)`；必须返回**全部**命中，若将超过 `maxResults`
  必须返回 false（引擎回退 CPU，不允许截断/抽样）。
* 单元测试（已就绪并通过）：
  * **[17]**：真实设备上 GPU 与 CPU 密度匹配结果等价性——当前实现返回 false，
    测试按「不支持即跳过」处理（保证不产生假阳性）；
  * **[18]**：模拟提供者的引擎侧集成——A) GPU 与 CPU 结果（含 TopN 顺序与密度位模式）一致；
    B) 模拟失败自动回退 CPU 且结果一致；C) 启用条件矩阵（提前停止 / 智能范围 / 分层 / 估算 /
    基岩+缓存 均不调用 GPU 匹配）；D) 分块 + GPU 结果集合与经典路径一致、无重复；
    E) 多分块锚点划分 3 块 `[0,x] [x+1,y] [y+1,5995]` 连续无缝（`g_anchorMaxAZ` 生效）。

**因此 v3.4.0 的恢复工作只剩「在 `GpuScanAdapter` 中覆盖 `matchDensity`」+ 修复
`experimental/` 中记录的缺陷**，引擎与测试无需改动。

### 5.3 本次排查中发现并修复的两个构建期缺陷（v3.3.1 实际收益）

暂停过程中定位到两个**与 GPU 功能无关、但会污染任何构建**的缺陷，均已修复并加入回归：

1. **嵌入内核被注入换行（`scripts/embed_kernel.cmake`）**
   旧版脚本在每 24 字节的换行处写入了 C 转义 `\n`（而非仅换行的源码文本），
   于是 `kSlimeKernelSource` 内容每 24 字节多出一个换行字符：内核注释被截断成代码、
   标识符被劈开，OpenCL 编译器在运行期报 `unknown type name` / `not valid UTF-8`
   （实测 20 个错误后 `CL_BUILD_PROGRAM_FAILURE`）。
   修复：换行只作为生成源码的换行，不进入字符串内容；脚本内新增
   **八进制转义自回读校验**（生成后立即把转义解码回字节流与输入比对，不一致直接 FATAL）。
   回归：`slime_kernel_tests` D 段在运行期比对嵌入串与磁盘 `.cl`
   （长度 6058 / 校验和 997496561 / 两个 `__kernel` 签名 / 逐字节一致）。

2. **Ninja 头文件依赖跟踪失效（`CMakeLists.txt`）**
   本机 cl.exe 为中文（本地化）版本，`/showIncludes` 前缀是 GBK 字节，而 `rules.ninja`
   中 `msvc_deps_prefix` 是文本编码，两者不匹配 → Ninja **静默丢弃全部头文件依赖**
   （`ninja -t deps <obj>` 显示 `#deps 0`）→ 修改任何头文件后对象文件不重编译。
   这正是「旧 OpenCL 内核被静默复用」「虚表不匹配导致 `0xC0000005`」等问题的直接原因
   （实测：修好前改 `GpuSession.h` 后 `slime_gpu.lib` 不重建，修好后同样操作触发 29 步重编译）。
   修复：不再依赖 `/showIncludes`（`VSLANG=1033` 无法切换该前缀语言，已实测），改为在
   CMake 中把**全部项目头文件**（含构建期生成的 `slime_kernel_embed.h`）声明为各目标源文件的
   `OBJECT_DEPENDS` —— 头文件变更即重编译，任何语言/代码页下都正确。

> 说明：缺陷 2 也解释了本版本早期「同一二进制行为不一致」的若干假象。修复后
> 「clean 重建 + 全量验证」结果稳定可复现（§6）。

## 六、回归与交付

| 项目 | 命令/方法 | 结果 |
|---|---|---|
| clean 全量重建 | `cmake --build build --clean-first`（校验退出码与 error 行） | 见 §6.1 |
| 单元测试 18 组 | `build\slime_tests.exe --unit` | 见 §6.1 |
| SIMD 内核独立自检 | `build\slime_kernel_tests.exe`（独立教科书参考 + 嵌入内核比对） | 17 项检查 0 失败 |
| ~~GPU 密度内核独立自检~~ | ~~`build\slime_gpu_density_tests.exe`~~ | 目标已移除；源码保留在 `src/gpu/experimental/`（§5.1） |
| 111 参考对照 | `py -3 tests\verify_against_reference.py` | 见 §6.1 |
| OpenCL 探测（真实设备一致 + 吞吐） | `build\slime_ocl_probe.exe` | 见 §6.1 |
| GUI 自检（GPU 开/关） | `SLIME_SELFTEST=1` + `Start-Process -Wait` | 见 §6.1 |
| 性能基准 | `build\slime_tests.exe --bench` | `docs/bench_v331.txt` |
| 一键发布验证 | `scripts\verify_release.ps1 -Zip dist\SlimeFinder-3.3.1-win64.zip` | 见 §6.1 |

### 6.1 最终实测记录（`scripts\verify_release.ps1`，16/16 PASS）

一键脚本在同一次运行内完成「clean 全量重建 → 单元 → 111 → OpenCL 探测 → 内核自检 →
GUI 自检（GPU 关/开）→ 基准（含 v3.2.1 基线）→ dist 自检 → 哈希一致性（build/dist/zip）」，
退出码 **0**，16 项全部 PASS：

| # | 步骤 | 实测 |
|---|---|---|
| 1 | clean 全量重建（`--clean-first`） | 退出码 0，`error` 行数 **0** |
| 2 | 单元测试 18 组（`slime_tests.exe --unit`） | 退出码 0，**全部通过**（含 [13] GPU 等价、[17] GPU 密度一致性、[18] GPU 引擎集成） |
| 3 | 111 参考对照（`verify_against_reference.py`） | 退出码 0，**111 通过 / 0 失败**（证据 `docs/verify111_v331.txt`） |
| 4 | OpenCL 探测（真实设备一致性 + 吞吐） | 退出码 0，**PASS（失败 0 项）**，全部矩形 `mismatch=0`（证据 `docs/ocl_probe_v331.txt`） |
| 5 | SIMD 内核独立自检（`--correct-only`） | **17 项检查 0 失败 / RESULT: ALL PASS**（含 D 段嵌入内核逐字节比对） |
| 6 | GUI 自检 GPU 关 | exit 0（`gui_selftest_v331_gpu_off.txt`） |
| 7 | GUI 自检 GPU 开 | exit 0（`gui_selftest_v331_gpu_on.txt`） |
| 8 | 性能基准（当前 + v3.2.1 基线） | 退出码 0（证据 `docs/bench_v331.txt`） |
| 9 | dist 自检（`dist\SlimeFinder\SlimeFinder.exe`） | exit 0 |
| 10 | 哈希一致性 build == dist | 4 个二进制全部一致（见下表） |
| 11 | 哈希一致性 zip == dist + 包内自检 | 一致；解压包 GUI 自检 exit 0 |

**GPU 判定扫描实测（最终构建，AMD RX 6750 GRE 10GB，端到端含回读）**：

| 场景 | GPU | CPU 单核 | CPU 12 线程 | GPU/CPU1 | GPU/CPU12 |
|---|---|---|---|---|---|
| Java 16384²（2.68 亿区块） | **26.9 ms**（99.9 亿/秒） | 952.1 ms | 206.1 ms | **35.44×** | 7.67× |
| Java 1000²（100 万区块） | 0.3 ms（35.8 亿/秒） | 3.6 ms | 1.6 ms | 12.84× | 5.62× |
| 基岩 2048²（419 万区块） | 72.6 ms（5775 万/秒） | 5008.8 ms | 692.9 ms | **68.97×** | 9.54× |

一致性：9 组矩形（含 ±2^25 超大坐标、非整幂宽、大种子、负坐标）`mismatch=0`；
Java 16.8M 区块、基岩 1.05M 区块**全量**比对一致；引擎级 Java 密度 8192² 与基岩密度 4096²
结果集合 `一致=是`。**CPU 路径无回退**：密度 g=5/g=10 精确 vs 保守 5.65~13.35×、
严格匹配 13.57~16.91×（证据 `docs/bench_v331.txt`）。

**交付哈希（SHA256，最终构建）**：

| 文件 | SHA256 |
|---|---|
| `build\SlimeFinder.exe` = `dist\SlimeFinder\SlimeFinder.exe` | `8FA0300085E2661A82A12C6E67DFEAF5D7D49300295CF589112CE21BCAE093BD` |
| `build\slime_tests.exe` = dist | `6DF0B8C2988536F40838A9C0681A86EFB92D603A54DD32D8A4C5B68D0931B58A` |
| `build\slime_ocl_probe.exe` = dist | `07076E39DABCB38CC458D428AE0A450DD8D631F5B98540BCEE022EE7C3653F08` |
| `build\slime_kernel_tests.exe` = dist | `E3F743B4F4D1C07DEFBB329559D3F3D8D0200F50425F4745EF6BDB0362EEB662` |
| `dist\SlimeFinder-3.3.1-win64.zip` | 见 `dist/SHA256SUMS-3.3.1.txt`（交付以该文件为准） |

**构建可复现性（v3.3.1 新增 `/Brepro`）**：编译与链接均加 `/Brepro`（确定性时间戳），
实测「同一源码两次 clean 重建 + 重新链接」产出**逐字节一致**的 exe
（`SlimeFinder.exe` 两次 SHA256 均为 `8FA03000…`），因此「重建后再打包」也能通过
`build == dist == zip` 哈希检查；脚本 `scripts\package_release.ps1` 完成打包与包内校验。

**证据文件**（`docs/`，均为本次最终构建的原始输出）：
`bench_v331.txt`、`unit_test_v331.txt`、`verify111_v331.txt`、`kernel_selftest_v331.txt`、
`ocl_probe_v331.txt`、`gui_selftest_v331_gpu_off.txt`、`gui_selftest_v331_gpu_on.txt`、
`v331_cache_evidence.txt`。

## 七、已知限制

1. **基岩种子查表不提速**（§3）：默认关闭；保留 API 以便未来出现「只算种子」的热点时启用。
2. **密度内核的启用范围**：仅无滑动资格路径（滑动路径在 g≥5 时仍略快，且结果一致）。
3. **GPU 密度匹配未交付**（§5）：开发暂停，`GpuScan::matchDensity` 默认返回 false →
   密度匹配恒走 CPU 路径（结果与 v3.3.0 一致）；恢复点见 `src/gpu/experimental/README.md`。
4. 严格匹配实现方式沿用 v3.3.0 的字级 64 锚点批处理（同二进制 A/B 13~15×，与本任务书预期一致）。
