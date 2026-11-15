# 计算框架重构设计（分支 refactor-compute-core）

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

### Phase 1：Matcher 迁移

- PatternMatcher/DensityMatcher 输出 `ResultCollector`；
- 与旧引擎对拍命中数和排序；
- 性能不得低于旧路径（允许首版 ±5%）。

### Phase 2：Planner/缓存

- 缓存、分块、分带、GPU 阈值进 Planner；
- 旧 `runImpl` 只保留适配器；
- 结果对拍：随机参数矩阵 + GUI 自检。

### Phase 3：切换与清理

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
