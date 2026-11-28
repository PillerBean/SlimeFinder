# k3 / 环形预筛缓存化（cachePrescanSeeds）

日期：2026-11-28
分支：`flow-k3-cache`（基于 `flow-integration` 1d253e5）
任务来源：`docs/planning/HANDOFF_2026-11-28.md` §5 P1

## 1. 动机

环形四档里的「预筛精修(2)」「环带上界复核(3)」都先做一次 2D 方形密度预筛
（`gridSize=15`、阈值 18%），再用种子驱动后续精修 / k3 上界复核。

v3.15.x 已把 `CacheManager` 接进环形 predicate（Bedrock + 缓存完整覆盖时逐区块读缓存），
但**候选种子仍来自 `GpuScan::prescanDensitySeeds`** —— 该实现沿 z 带把整张正方形再扫一遍
（W×H 个窗口），缓存完全没有加速这一步。

缓存 range 位图本身就是每个区块的确定值：只要缓存完整覆盖搜索区域，窗口密度可以直接
在 CPU 上按行流式滑动统计，不需要 GPU 全图扫描。收益与定位：

- 缓存完整覆盖时，环形/k3 的**预筛种子不再依赖 GPU**（无 GPU 会话/显存/回读；GPU 不可用、
  被占用或驱动异常时该档位仍可用），且结果确定可复现（同输入同输出）；
- 结果（种子集合）与 GPU 语义对齐（窗口/阈值/分带/排序/去重/截断一致），k3 过滤与后续
  精修逻辑完全不变；实测对拍见 §6.2（2048² 完全逐条一致；8192² 仅跨 topPerBand 边界的
  count 平局取舍不同——GPU 本身该处也未定义）；
- 速度取决于场景：小区域/多带配置下 CPU 位图版明显更快（GPU 每带有固定开销）；
  8192² 全 pack + 生产阈值（命中极稀）时 GPU 流式扫描几乎零回传，CPU 版略慢（同为百 ms 级）。
  速度不是该路径的唯一卖点，**免 GPU 依赖 + 确定性结果**才是。

## 2. 接口与调用点

```cpp
namespace slime {
bool cachePrescanSeeds(const CacheManager& cache, const DensityPrescanParams& p,
                       const std::atomic<bool>* cancel,
                       std::vector<DensitySeed>& out, std::string* err);
}
```

- 实现：`src/core/CachePrescan.h` / `src/core/CachePrescan.cpp`（Qt 无关，加入 `slime_core`）；
  头文件在 `src/gui/mainwindow/MainWindow.cpp` 的全局作用域包含（`MainWindowRingMap.inc`
  在 `namespace slime` 内展开，若在 .inc 里包含会形成嵌套 namespace）。
- 调用点：`src/gui/mainwindow/MainWindowRingMap.inc`
  - `RingSearchResult::cachePrescanUsed`：worker 写、`finishRingSearch` 只读，用于日志/状态；
  - `runRingSearch()` 的 `autoPrescan`（档位 2/3）分支：
    1. `ringCache` 非空（Bedrock + 勾选缓存 + CacheStore 完整覆盖，创建条件保持原样）时
       先调 `cachePrescanSeeds`；`ringCache` 按值捕获 `shared_ptr` 进 worker，线程安全；
    2. 成功 → 填 `o.seedCandidates`、`prescanUsed=true`、`state->cachePrescanUsed=true`，
       日志 `[环形] 缓存预筛：%zu 个分散种子（阈值 %.2f）`；
    3. 失败 → 打印警告（含 err），继续走原 GPU `prescanDensitySeeds` 路径（原回退语义/日志不变）；
    4. 两条来源的 k3 过滤完全一致：
       `AfkPlanner::plan` 得 lowerBound → `AfkPlanner::k3UpperBoundSeeds`；
  - `finishRingSearch`：现有 prescan 日志后补一行 `[环形] 预筛来源：缓存位图 / GPU（cachePrescanUsed=…）`；
    **不新增 i18n key、不改任何 UI 文案**。
  - 普通（非 autoPrescan）路径、Java 路径、缓存 miss（`ringCache == nullptr`）行为完全不变。

## 3. 与 GPU `prescanDensitySeeds` 的语义对齐

| 项 | 对齐规则 |
| --- | --- |
| gridSize | 1..15；非法返回 false |
| 窗口左上角范围 | `[minX, maxX-g+1] × [minZ, maxZ-g+1]`；为空返回 false |
| 密度阈值 | `thrCount = max(1, ceil(clamp(minDensity,0,1)·g² − 1e-9))` |
| 分带 | `bandRows = max(64, p.bandRows)`，`[az0, az1]` 闭区间 |
| 每带选取 | `count ≥ thrCount` 的窗口保留 `topPerBand = max(1,p.topPerBand)` 个 |
| 种子中心 | `cx = 窗口x + g/2`、`cz = 窗口z + g/2` |
| 全局排序 | count 降序；count 相同 cx 升序、cz 升序 |
| 去重 | `minSeparation=max(1,p.minSeparation)` 的 3×3 邻格哈希（与 GPU 222–249 行逐行对应） |
| 截断 | `maxSeeds=max(1,p.maxSeeds)` |
| 取消 | `*cancel == true` → false + `err="已取消"`（带内逐行检查） |
| 空结果 | `"正方形预筛没有候选"` / `"正方形预筛没有分散种子"` |
| 前置条件 | `cache.covers({minX,maxX,minZ,maxZ})` 不成立 → false + `err="缓存未完整覆盖"`（调用方回退 GPU） |

## 4. 与 GPU 的差异（有意为之）

1. **不做「命中过多自动抬高阈值重试 / 8M 命中上限」**：GPU 需要把命中窗口回读主机，
   所以要限流；CPU 位图版每窗口 O(1)、只有 topPerBand 条会进堆，无需重试也无需上限。
   高密度场景下 CPU 固定按「每带 topPerBand 内 count 最高」选取，语义即 GPU 正常路径。
2. **每带截断的等价 count 取舍是确定性的**（count 降序后 cx、cz 升序）；GPU 的
   `partial_sort` 只比较 count，等价 count 的边界取舍由实现决定。全局排序/去重/截断
   与 GPU 完全一致，差异只可能出现在「跨 topPerBand 边界的 count 并列」这一极窄场景。
3. **多 range 重叠的正确性**：`CacheManager` 的文档语义是「ranges 顺序中第一个包含该点的 range」。
   若覆盖区域的 range 之前还有 range 与区域重叠，区域内的真实位可能记在更早的 range 位图里。
   CPU 实现检测到这种遮蔽时退化为「按 ranges 顺序读原始位图」的确定性慢路径
   （**不**用 `CacheManager::lookup`：其内部 thread_local「上次命中 range」快查在两个 range
   重叠时会依赖访问顺序，结果不确定；非重叠缓存两者语义相同）。正常单 range 缓存
   （GUI 生产路径）走位图快路径。
4. **多 range「拼接」但没有任何单个 range 包含整个区域时不接受**（`cache.covers` 语义），
   返回 false 回退 GPU。GUI 生成 `ringCache` 时 `CacheStore::fillCacheManagerRange` 总是把
   pack 合并成单个覆盖 range，因此生产路径不会遇到该限制。

## 5. 实现要点

- 按 z 带流式：每带维护「区域行宽」个列的窗口列和（`colSum[x] = Σ_{z∈[z0,z0+g)} isSlime(x,z)`），
  每下移一行加新行、减旧行；每行再对 colSum 做水平滑动窗口 ⇒ 每个窗口 O(1) 算术、
  每个区块位约 1~2 次位测试，**不复制整区域位图**。
- 峰值内存 `O(区域行宽 + topPerBand + 带数×topPerBand)`：8192 宽区域约 32 KiB int 列和。
- 窗口/种子坐标用 int64 中间值计算，支持负坐标，避免 `maxX-g+1` 等 int 溢出。
- 取消：每带开头 + 每个左上角行检查一次 relaxed 原子读，取消后立即返回。

## 6. 验证

- 构建：独立 `build-k3`（Ninja / Release）全量成功（含 `SlimeFinder`、`slime_tests`）。
- 单测：`build-k3\slime_tests.exe --unit` 全部通过，新增 `[39] testCachePrescanSeeds`：
  - 完整覆盖时与独立暴力实现（逐窗口逐点重数 + O(n²) 去重参考）**逐种子一致**（count/顺序/去重/截断）；
  - 分带 `bandRows=64`、`topPerBand=5` 的截断行为；
  - 部分覆盖 → false 且 `err="缓存未完整覆盖"`；取消 → false 且 `err="已取消"`；
  - 负坐标区域；`maxSeeds` 截断 + 大间距去重；
  - 多个 range：大 range 在前的快路径、子 range 在前的遮蔽慢路径、多 range 拼接但无单 range 覆盖 → 拒绝；
  - **局部遮蔽专项**（lead review 要求）：先 `mergeRegion` 一个与目标区域相交的旧 range（交集内点位归属旧
    range 位图），再 `mergeRegion` 目标区域；断言实现结果与「ranges 顺序第一个包含 range」的确定性
    暴力逐种子一致（对拍真值直接读原始位图，原因见 §4.3），并反证「只读目标 range 位图会得到空候选」
    ⇒ 确实走了遮蔽慢路径；
  - 真实 Bedrock 256×96 位图（`SlimeEngine::isSlimeChunk` 生成，种子 2229010591266423951，
    密度 10.4%）与暴力窗口计数一致。
- GUI 自检：offscreen `SLIME_SELFTEST=1` 退出码 0（干净 config 首次运行）。
- i18n：`py -3 scripts\qa\check_ui_text.py` OK，1457 keys × 3 语言（本任务未改 i18n）。
- 对拍探针：`build-k3\k3_prescan_parity_probe.exe`（`tools/bench/k3_prescan_parity_probe.cpp`），
  在同一份真实 Bedrock 位图上比较 GPU `prescanDensitySeeds` 与 `cachePrescanSeeds`。

### 6.1 单测 [39] 的 cachePrescanSeeds 耗时（本机 12 线程）

8192×8192 合成缓存（50% 密度，最坏命中量级；参数与 GUI 一致：g=15、阈值 0.18、bandRows=4096、
topPerBand=512、maxSeeds=4000、minSeparation=16）：
`166.7–183.3 ms`（0.67 亿窗口，约 2.5–2.7 ns/窗口），输出 268 个种子。

### 6.2 GPU vs 缓存位图对拍（gfx1031；同一份 GPU 扫描位图 → CacheManager；3 次取最短）

| 区域 | 参数 | 种子数（GPU/缓存） | 逐条一致 | GPU 耗时 | 缓存耗时 | 加速比 |
| --- | --- | --- | --- | --- | --- | --- |
| 2048² | A 无每带截断：bandRows=64、topPerBand=1e6、阈值 0.15 | 2069 / 2069 | 是 | 435.8 ms | 16.4 ms | 26.5× |
| 2048² | B 生产参数：bandRows=4096、topPerBand=512、阈值 0.18 | 76 / 76 | 是 | 16.2 ms | 7.1 ms | 2.3× |
| 8192² | A 无每带截断 | 4000 / 4000 | 是 | 1832.5 ms | 233.3 ms | 7.9× |
| 8192² | B 生产参数 | 224 / 227 | 否：33 条 count 平局 | 73.5 ms | 117.5 ms | 0.6× |

真实 Bedrock 密度 ≈10%（2048²：421734/4194304=10.05%；8192²：6716287/67108864=10.01%）。
生产阈值 18%（15×15 窗口期望 22.5 个）意味着命中窗口非常稀疏，GPU 流式扫描几乎不需要回传命中，
因此 8192²/生产参数下 GPU 反而更快；缓存版的价值是**缓存完整覆盖时不再依赖 GPU**（无 GPU 会话/
显存/回读，GPU 被占用或不可用也能跑该档位），结果确定可复现，且区域越大/命中越多时明显更快
（8192² 无截断：233 ms vs 1833 ms，7.9×）。小区域、多带配置下 CPU 位图版也更快（2048² A：
26.5×，GPU 每个带都有固定启动/回读开销）。

count 平局差异：8192²/生产参数下每带按 count 取 topPerBand 时，跨边界出现大量 count=43 并列；
GPU 的 `partial_sort` 只比较 count，并列取舍由实现决定 —— 实测同一输入 GPU 每次运行连**种子数**
都不稳定（三次运行分别为 227/226/224），而缓存版确定性输出 227 条。两条路径在并列边界上略有
差异（33/227 条，均 count=43），CPU 版用确定性次序（count 降序后 cx、cz 升序），见 §4.2。

### 6.3 GUI 日志字段 `cachePrescanUsed` 的出现位置

- worker（`MainWindowRingMap.inc`，`Console/stderr`）：
  - 缓存预筛成功：`[环形] 缓存预筛：%zu 个分散种子（阈值 %.2f）`
  - 缓存预筛失败回退：`[环形] 缓存预筛不可用，回退 GPU 预筛：%s`
  - GPU 预筛成功（原日志不变）：`[环形] 正方形预筛：%zu 个分散种子（阈值 %.2f）`
- `finishRingSearch`（GUI 线程，预设日志后）：`[环形] 预筛来源：缓存位图（cachePrescanUsed=true）`
  或 `[环形] 预筛来源：GPU（cachePrescanUsed=false）`
- 状态字段：`MainWindow::RingSearchResult::cachePrescanUsed`（worker 写、GUI 线程只读）；
  反馈文案沿用现有 i18n key，本任务未新增 key、未改 UI 文案。

## 7. 已知限制 / 风险

- CPU 路径耗时与窗口数线性相关（8192² 约 120–240 ms）；实际 Bedrock 缓存 pack ≤ 8192²。
  小区域/多带配置下比 GPU 快，但 8192² + 生产稀疏阈值下 GPU 流式扫描更快（见 §6.2）——
  该路径的定位是免 GPU 依赖 + 确定性，而不是「所有场景都更快」；若后续要继续提速，
  方向是把「逐位展开」换成 AVX2 位展开/行位缓存（每区块从 2 次位测试降到 1 次）。
- 只对 Bedrock 生效（Java 无本地缓存），调用方创建 `ringCache` 的条件完全保持原样。
- 多 range 遮蔽场景走 `lookup` 慢路径（正确但慢；生产缓存布局不会出现）。
- 高密度缓存（命中 ≫ topPerBand）时不再「抬高阈值」，仍按每带 topPerBand 取最高密度窗口；
  与 GPU 正常路径语义一致，差异仅限 GPU 触发重试的异常场景。
- 每带 topPerBand 边界的 count 并列取舍与 GPU 不同（CPU 确定性 vs GPU partial_sort 未定义）；
  极窄场景下两条路径的种子集合会略有差异（数量一致），k3 过滤后可能选到不同的等价候选。

## 8. 复现验证命令

```powershell
# 构建（独立 build-k3；首次 configure 后增量编译）
cmake -S . -B build-k3 -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=C:/Qt/6.11.2/msvc2022_64
cmake --build build-k3

# 单元测试（含新增 [39]）
build-k3\slime_tests.exe --unit
build-k3\slime_tests.exe --unit 39

# GPU vs 缓存位图对拍（需可用 OpenCL 设备；无设备时打印“GPU 不可用”并返回 2）
build-k3\k3_prescan_parity_probe.exe

# GUI 自检（干净 config 首次运行；退出码 0）
$env:PATH="C:\Qt\6.11.2\msvc2022_64\bin;$env:PATH"; $env:QT_QPA_PLATFORM="offscreen"; $env:SLIME_SELFTEST="1"
& .\build-k3\SlimeFinder.exe *> "$env:TEMP\gui_k3.log"; Write-Host "exit=$LASTEXITCODE"

# i18n 文本一致性（本任务未改 i18n）
py -3 scripts\qa\check_ui_text.py
```
