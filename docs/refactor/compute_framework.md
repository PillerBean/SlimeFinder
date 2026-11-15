# 计算框架重构设计（分支 refactor-compute-core）

## 0. 先回答：不能保证当前框架是算法工程“最优”

不能保证，也不应该这样承诺。

原因不是“设计不用心”，而是“最优”至少同时受以下约束：

- 正确性/可回退；
- 单线程与多线程吞吐；
- GPU/CPU 融合与回读成本；
- 大范围内存上限；
- 取消/进度/缓存语义；
- 可维护性和后续扩展；
- 当前硬件（6C12T + gfx1031）。

这些目标互相冲突，只能找 **当前约束下的 Pareto 点**，不能证明全局最优。

更重要的是：**当前设计的第一版确实存在一个架构风险**——

> 如果把 `Determiner → Bitmap → Matcher` 当成所有路径的强制边界，
> 就会破坏现有 GPU 密度/图案的“判定+匹配融合、只回传命中”路径。
> 这条融合路径正是当前 Bedrock GPU 和 Java GPU 的主要性能来源。

因此 Phase 0 的 Bitmap/Determiner 原型只证明“位图工件可以独立”，
**不能证明它应该成为所有执行计划的必经阶段**。

## 0.1 框架必须支持多执行计划，而不是单一阶段链

修正后的设计原则：

```text
算法内核（FastJava/FastMt/FastMatch/GPU kernels）
        ↑
执行计划（ExecutionPlan）
  ├─ MaterializeCpuThenMatch
  ├─ MaterializeGpuThenMatch
  ├─ FusedGpuPattern
  ├─ FusedGpuDensity
  ├─ CachedBitmapThenMatch
  └─ future: pipelined/async plans
        ↓
公共 Collector / Progress / Cancel / Stats
```

- `Bitmap` 是**可选工件**，不是强制中间层；
- `Matcher` 只用于需要位图的计划；
- 融合计划绕过 Bitmap，直接调用 GPU 流式内核并交给 Collector；
- Planner 选择计划，而不是在热路径里堆 if；
- Collector/Progress/Cancel 是公共合同。

### 0.2 为什么不能强制 Bitmap

当前实测：

```text
Bedrock GPU 8192²：
  CPU 等 GPU 94%~99%，GPU 融合内核主导
Java GPU 密度 8192²：
  GPU 等 CPU 66%，回读/宿主主导
```

如果强制先物化整幅 Bitmap，再读回 CPU 匹配：

- GPU 融合路径的“只回传命中”优势消失；
- 多一次显存→宿主位图传输；
- 大范围内存/带宽成本上升；
- Java GPU 的宿主回读瓶颈可能更严重。

所以 Bitmap 只能作为 **CPU/缓存/需要复用的计划** 的工件，不能被抽象成唯一数据流。

## 0.3 当前设计的评价

| 维度 | 评价 |
|---|---|
| 阶段职责拆分 | 方向正确 |
| Bitmap 作为唯一工件 | **过于强，需修正** |
| 融合 GPU 路径 | 第一版没有作为一等公民 |
| Planner 集中策略 | 正确，但要防止变成新的 god object |
| 保留数学内核 | 正确 |
| 分阶段迁移/对拍 | 正确 |
| “算法工程最优” | **不能保证，只能持续用基准和计划矩阵逼近** |

## 1. 为什么单独开分支

当前 `SlimeEngine.cpp` 已超过 4000 行，`runImpl` 一个函数同时承担：

- 位图分配；
- CPU/GPU 判定；
- 缓存读取/构建；
- 经典/分块/分带路径；
- 严格图案与密度匹配；
- 结果堆、排序、去重；
- 提前停止、智能范围、进度、取消；
- 最近的位图复用。

每加一个优化，都要在多个分支里保持语义一致。继续在当前函数里叠条件，
验证成本会高于算法本身的收益。

因此新开 `refactor-compute-core` 分支，目标不是“再优化一处”，而是：

> **把计算拆成少量、明确的阶段；每个阶段只认一种输入/输出；**
> **策略选择集中在 Planner，不再散落在热循环里。**

主分支保持 3.13.x 现状；重构分支可以失败、回滚，不直接进正式版。

## 2. 新框架的四个阶段

```text
Planner
  ↓
Determiner  →  Bitmap  →  Matcher  →  Collector
  ↑              ↑
  CPU/GPU     唯一共享工件
```

### 2.1 Bitmap

一个明确的位图工件：

```cpp
class Bitmap {
    int x0, z0, w, h, wpr;
    std::vector<uint64_t> words;
public:
    bool init(...);
    uint64_t* row(int z);
    const uint64_t* row(int z) const;
    bool isSlime(int x, int z) const;
    int countBits(int z, int x, int len) const;
    // snapshot/restore 只用于复用，不参与匹配逻辑
};
```

约束：

- 只表示“某坐标是否史莱姆”；
- 不包含缓存、结果、模式、线程数；
- 允许整幅快照，作为复用/流水线的唯一边界。

### 2.2 Determiner

```cpp
struct ScanRequest {
    Version version;
    int64_t worldSeed;
    Rect rect;
};

struct ScanResult {
    Bitmap bitmap;
    ScanStats stats;
};

class Determiner {
public:
    virtual ~Determiner() = default;
    virtual const char* name() const noexcept = 0;
    virtual bool run(const ScanRequest&, const ScanCancel&, Bitmap&, ScanStats&,
                     std::string* err) const = 0;
};
```

实现：

- `CpuDeterminer`：行并行 + `fillSlimeRowJava/Bedrock`；
- `GpuDeterminer`：包 `GpuScan::scanRectWords/scanRect`；
- 可选 `CachedDeterminer`：装饰器，不是另一套判定算法。

Determiner 不负责匹配，不构造 `MatchResult`，不排序。

### 2.3 Matcher

```cpp
struct MatchRequest {
    Version version;
    Mode mode;
    Pattern pattern;
    int gridSize;
    double minDensity;
    int64_t targetCount;
    int64_t startX, startZ;
    bool stopAtTarget;
    bool smartRange;
    Algorithm algorithm;
};

class Matcher {
public:
    virtual bool run(const Bitmap&, const MatchRequest&, const MatchCancel&,
                     ResultCollector&, MatchStats&, std::string* err) const = 0;
};
```

实现：

- `PatternMatcher`：严格图案；
- `DensityMatcher`：精确/分层/采样/保守作为**内部策略**，不是四个分支；
- 未来可加 `GpuMatcher`：同一接口，内部融合判定与匹配，但仍然输出 `ResultCollector`。

Matcher 不做判定，不知道线程实现，不知道 GUI。

### 2.4 ResultCollector

只负责：

- 结果集合；
- `(距离, rank)` 排序；
- TopN 截断；
- 总命中数；
- 线程安全合并。

结果堆不再和匹配热循环耦合。

### 2.5 Planner

集中决定：

- 范围是否需要分块；
- 用 CPU 还是 GPU determiner；
- 用 CPU 还是 GPU matcher；
- 是否使用缓存；
- 是否允许位图复用；
- 线程数和内存预算。

规则是**显式的策略**，不是 `runImpl` 深处的 if。

### 2.6 ExecutionPlan：融合路径必须是一等公民

```cpp
enum class PlanKind {
    MaterializeCpuThenMatch,
    MaterializeGpuThenMatch,
    FusedGpuPattern,
    FusedGpuDensity,
    CachedBitmapThenMatch,
};

struct ExecutionPlan {
    PlanKind kind;
    ScanOptions scan;
    MatchOptions match;
    ResourceBudget budget;
};
```

- `Materialize*` 计划才产生 `Bitmap`，再交给 `Matcher`；
- `FusedGpu*` 计划直接调用 GPU 流式内核，只把命中交给 `Collector`；
- `CachedBitmapThenMatch` 直接复用位图，不重新判定；
- 公共合同只有 `Cancel/Progress/Stats/Collector`，不强制 Bitmap。

这样既保留模块边界，又不会为了“干净”牺牲现有融合 GPU 性能。

## 3. 与现有代码的关系

新框架**不重写数学内核**：

- 继续用 `FastJava`、`FastMt`、`FastMatch`；
- 继续用现有 `GpuSession`；
- 继续用现有缓存格式；
- 先把编排/所有权/阶段边界理清，再逐步迁算法。

这样可以把“重构”与“改算法”分开验证。

## 4. 迁移阶段

### Phase 0：原型（当前分支）

- 新增 `Bitmap`、`Determiner`、最小 `Pipeline`；
- 只实现 `CpuDeterminer`/`GpuDeterminer`；
- 用独立探针对比 CPU/GPU 位图逐位一致；
- 不接入 GUI，不改现有引擎。

验收：

```text
Java/Bedrock 2048²：CPU 与 GPU bitmap 完全一致
随机坐标抽样与 isSlimeChunk 一致
取消/进度不破坏位图
```

### Phase 1：ExecutionPlan + Collector + Materialize 计划

- 定义 `ExecutionPlan`/`ResultCollector`；
- PatternMatcher/DensityMatcher 只服务 Materialize 计划；
- 与旧引擎对拍命中数和排序；
- 性能不得低于旧路径（允许首版 ±5%）。

### Phase 2：融合 GPU 计划

- 把现有 `FusedGpuPattern`/`FusedGpuDensity` 包成显式计划；
- 融合计划不产生 Bitmap，直接写 Collector；
- 对拍融合/物化两条路径的结果和统计；
- 确认没有为了统一抽象强迫 GPU 回读位图。

### Phase 3：Planner/缓存/分块

- 缓存、分块、分带、GPU 阈值进 Planner；
- 旧 `runImpl` 只保留适配器；
- 结果对拍：随机参数矩阵 + GUI 自检。

### Phase 4：切换与清理

- 功能开关切到新 Pipeline；
- 旧路径保留一个版本用于回退；
- 性能/正确性门禁通过后删除旧分支。

## 5. 硬性验收条件

每一阶段必须同时满足：

1. 判定/匹配结果逐位或逐项一致；
2. 取消后不留部分结果，不再继续派发；
3. 进度阶段与分母不跳变；
4. 缓存构建/读取语义不变；
5. 单线程、6 线程、12 线程都不退化超过 5%；
6. 大范围（分块）和小范围（经典）都覆盖；
7. GPU 失败能回退 CPU，且回退原因可记录。

## 6. 非目标

- 首版不追求新算法加速；
- 首版不改 GUI/配置格式；
- 首版不删除旧引擎；
- 首版不把 GPU 异步流水线、top-N 预筛等未验证优化塞进重构。

## 7. 当前分支状态

```text
分支：refactor-compute-core
基线：main @ d199e4e
主分支：保持 3.13.7 测试版状态，正式版仍为 3.12.3
```

Phase 1 已完成的部分：

```text
src/core/next/Plan.h        计划家族/策略/资源/进度数据结构
src/core/next/Planner.cpp   只读 Planner：Search/Build/Banded/Analytic
src/core/next/Collector.*   距离TopN / 密度TopN / CountOnly
tools/refactor/collector_test.cpp
tools/refactor/planner_test.cpp
```

测试：

```text
Collector ALL PASS
Planner ALL PASS
Phase0 CPU/GPU bitmap parity：mismatch=0
```

Phase 1 尚未做：

- 把 Planner 接到 SearchTask；
- Collector 真正供 Matcher 使用；
- 替换任何产品路径；
- 打包/安装包/Release。

Phase 2 已完成的最小切片：

```text
CpuDeterminer -> Bitmap -> CpuExactMatcher -> ResultCollector
  - Java/Bedrock
  - 严格图案（方向锁定）
  - 精确密度
  - target=50，512²，单线程
  - 与旧 SlimeEngine::run 对拍：结果、总数、排序、density 全一致
  - 连续四轮 PASS
```

详见 `docs/refactor/phase2_materialize_parity.md`。

当前仍不接入产品、不打包。


## 8. 从主分支实际流程反推

只看主分支真实入口和分支，不按理想分层想象。

| 实际功能 | 入口/条件 | 实际行为 | 框架必须支持 |
|---|---|---|---|
| 普通图案/密度搜索 | `MainWindow::startSearch` → `SearchTask` → `engine.run` | 归一化严格算法；顶层 GPU 图案；否则 `runImpl` | SearchPlan；可选 GPU fused |
| GPU 图案流式 | 顶层条件：AMD + 方向锁定 + 无缓存/构建 + 阈值 | 判定+匹配融合，直接入堆，不产位图 | `Matcher=FusedGpuPattern`, `Determiner=None` |
| GPU 密度流式 | `runImpl` 内 `gpuMatchWanted` | 判定+窗口统计融合，只回传命中 | `FusedGpuDensity`，不要求 Bitmap |
| 普通 Bedrock 缓存读取 | GUI 设置 `cacheBandLoader`；`SearchTask` 选择 `runBanded` | 按 band 懒加载缓存，halo，跨带合并 | `BandedPlan` 外层；内层 SearchPlan |
| 缓存完整覆盖 | `cacheFillSkipped` | 跳过 GPU/判定，直接缓存位图 | `Determiner=Cache`, `allowGpu=false` |
| 缓存构建 | 构建页/`buildMode=true` | 只判定+写缓存；不匹配；分段续跑；预算停止 | `CacheBuildPlan`，`Matcher=None`, `Collector=CountOnly` |
| 最优候选 | `ignoreThreshold` | 按窗口史莱姆数降序，不走距离 TopN；大范围分块 | `Collector=DensityTopN`；Analytic/搜索混合 |
| 最小密度 0 | `minDensity<=0` 且精确密度 | 解析求最近 N，不扫描全图 | `AnalyticPlan` |
| 提前停止/智能范围 | `stopAtTarget` / `smartRange` | 按距离顺序、跨块剪枝、全局下界 | `Collector=DistanceTopN` + `PartitionPlan.cacheFirst/order` |
| 大范围分块 | 位图字数超限/构建/缓存+提前停止 | 分块并发、halo、跨块剪枝、增量缓存合并 | `Partition=Chunked`；外层归并 |
| 位图复用 | 同范围/种子/版本，无缓存，CPU精确 | 跳过判定，只跑匹配 | 可选 Bitmap artifact；不能强制所有计划 |
| 环形/AFK | `runRingSearch` | 占用图/候选中心/环带覆盖，不是全图搜索 | `RingPlan` 独立；共享内核/取消，不塞进 SearchPlan |
| 搜索估算 | `estimateSearch` | 采样估算，不执行完整搜索 | Planner 的只读估算接口 |
| 进度/取消/统计 | 所有路径 | 阶段0~0.75/0.75~1；构建有 recorded/skipped/phase | `ProgressPlan` 显式化；`Stats` 公共合同 |

### 8.1 反推出来的硬约束

1. **不能把 Bitmap 作为强制边界**：GPU 图案/密度融合路径不产位图。
2. **匹配不一定存在**：缓存构建只判定+写缓存。
3. **判定不一定存在**：缓存命中、解析路径、GPU 融合可能不做独立判定。
4. **结果排序不止一种**：距离 TopN、密度 TopN、仅计数。
5. **缓存是副作用**：读/写/构建影响计划、预算、进度和停止条件，不能藏在 Determiner 里。
6. **分块/分带是外层计划**：内层仍是 SearchPlan；不能把 halo/合并散在 matcher。
7. **采样是稀疏工件**：`Algorithm::Sample` 不是完整位图 + 普通 matcher 的简单组合。
8. **环形搜索是独立产品流程**：可以共享内核，但不能强行套全图搜索管线。
9. **进度权重/阶段可配置**：普通、构建、分带、环形各不相同。
10. **取消必须跨层生效**：band/chunk/GPU 派发/结果合并都要检查。

## 9. 修正后的计划模型

### 9.1 计划家族

```text
Job
 └─ Planner
     ├─ SearchPlan
     │    ├─ Partition: Single / Chunked
     │    ├─ Determiner: Cpu / Gpu / Cache / Sample / None
     │    ├─ Matcher: CpuPattern / CpuDensity / FusedGpuPattern / FusedGpuDensity / None
     │    ├─ Collector: DistanceTopN / DensityTopN / CountOnly
     │    └─ CacheSideEffect: None / ReadOnly / Write
     ├─ BandedPlan
     │    └─ band iterator + halo + child SearchPlan + merge
     ├─ CacheBuildPlan
     │    └─ determiner + cache writer + count-only collector
     ├─ AnalyticPlan
     │    └─ zero-threshold / best-density analytical result
     └─ RingPlan
          └─ coverage/annulus, separate executor
```

`src/core/next/Plan.h` 已把上述计划字段落成数据结构。

### 9.2 统一合同，不统一数据形状

统一的是：

```text
Cancel
ProgressPlan
Stats
ResultCollector policy
Cache request/side-effect
```

不统一的是：

```text
Bitmap         仅 Materialize 计划
StreamingHits  仅 Fused 计划
AnalyticResult 仅解析计划
```

强制统一数据形状会牺牲融合路径性能。

### 9.3 Planner 的输入

Planner 不是“再写一个 if 大山”，而是一个**只读能力评估**：

```text
硬件能力：CPU 核数/GPU 可用/设备厂商
任务形状：版本、模式、算法、范围、目标数
缓存状态：覆盖/可读/可写/预算
资源约束：线程/CPU上限/内存预算
语义开关：停止/智能范围/最优候选/构建
```

输出 `ExecutionPlan`，执行器不再判断“是否 GPU/是否分块”。

### 9.4 构建模式为什么必须单独成计划

构建模式与普通搜索的差异不是“匹配器换一个”：

- 不排序、不返回结果；
- 只写缓存，且达到预算即停止；
- 分段续跑/跳过已覆盖块；
- 进度是 recorded/skipped/phase，不是 scan/match；
- 必须强制分块；
- 普通搜索不允许写缓存。

把这些塞进 SearchPlan 的 `Matcher=None` 可以表达一部分，但预算/续跑/进度需要
`CacheBuildPlan` 自己的生命周期。

### 9.5 环形搜索为什么不能塞进 SearchPlan

环形搜索的核心是：

- 占用图/候选中心；
- 环带覆盖统计；
- AFK 点排序/过滤；
- 不是“整幅位图 + 图案/密度窗口”。

它可以复用判定内核、取消、进度、缓存，但应作为 `RingPlan` 独立编排。

### 9.6 修正后的迁移顺序

```text
Phase 0  Bitmap + Determiner 原型/对拍                 已完成
Phase 1  Plan.h + Collector 策略 + 只读 Planner         当前
Phase 2  Materialize SearchPlan（CPU 经典/分块）对拍
Phase 3  FusedGpuPattern/FusedGpuDensity 计划对拍
Phase 4  BandedPlan / CacheBuildPlan / AnalyticPlan / RingPlan
Phase 5  功能开关切换 + 旧 runImpl 回退适配器
```

Phase 1 不直接替换产品路径，只建立计划数据和结果合同。

## 10. 当前是否“最优”

仍不能保证。

但修正后的框架至少满足：

- 融合 GPU 路径不被强制 Bitmap；
- 缓存构建/分带/解析/环形都有明确计划位置；
- 结果排序和缓存副作用不再藏进 Matching 热循环；
- 进度/取消/统计有公共合同；
- 可以先按计划逐个对拍、逐个切换，而不是一次性重写。

这比“把所有东西塞进统一的 Determiner→Bitmap→Matcher”更接近实际产品约束，
但仍需要用 Phase 2~5 的基准和回归来证明。

