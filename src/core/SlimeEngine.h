// SlimeEngine.h - 史莱姆区块查找核心计算引擎（Qt 无关，可在任意线程运行）
//
// 职责：
//   1. Java 版 / 基岩版区块判定（精确模拟游戏算法）
//   2. 并行扫描区域并生成区块位图（OpenMP，可取消、带进度）
//   3. 严格图案匹配（位掩码优化，支持方向锁定/旋转）
//   4. 密度搜索（积分/滑动窗口优化）
//   5. 查询起点按距离排序、目标数量截断、提前停止（数学上可证明的最优结果）
//   6. 智能范围限定（优先靠近查询起点扫描与匹配）
//   7. 基岩版缓存加速（CacheManager）
//
// 线程模型：run() 内部自行并行；外部通过 std::atomic<bool> 取消，
// 通过进度回调（std::function）汇报。所有成员仅由单个调用者线程使用。
#pragma once

#include <atomic>
#include <mutex> // v3.4.13：SearchOptions::cacheReadMu（并发写缓存时的读锁）
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace slime {

class CacheManager;

// ---------- 基础类型 ----------

enum class Version : uint8_t { Java = 0, Bedrock = 1 };
enum class Mode : uint8_t { Strict = 1, Density = 2 };

// v3.3.0：搜索算法模式（与 Mode 正交；精确模式必须与 v3.2.1 结果 100% 一致）
//   Exact   精确模式（默认）：全部优化（密度滑动窗口 + 严格匹配 AVX2/位并行）
//   Layered 分层模式：粗块密度剪枝后再精搜（更快，可能漏检边缘情况）
//   Sample  快速估算模式：网格采样估算密度（估算值，非精确结果）
//   Brute   保守模式：纯暴力（等价 v3.2.1 旧实现），用于对照/校验
enum class Algorithm : uint8_t { Exact = 0, Layered = 1, Sample = 2, Brute = 3 };

// v3.2.0：GPU 扫描提供者最小接口（引擎不依赖具体实现；实现位于 slime_gpu 模块，
// 通过 SearchOptions::gpuScan 注入。引擎在“可用 + 超过阈值”时自动使用，
// 运行失败自动回退 CPU 扫描路径——两种路径的位图/结果逐位一致）。
// v3.7.6：环形覆盖请求（占用图 + 候选中心网格）
struct AnnulusCoverageRequest {
    const int* pref2d = nullptr;    // (w+1)*(h+1) 的二维前缀和（占用图的史莱姆格数）
    int w = 0, h = 0;               // 占用图尺寸（格）
    int originX = 0, originZ = 0;   // 占用图原点（方块坐标）
    int step = 4;                   // 候选中心步长（格）
    int nx = 0, nz = 0;             // 候选中心网格尺寸
    int firstX = 0, firstZ = 0;     // 第一个候选中心（方块坐标）
    int innerRadius = 0, outerRadius = 128;
};

struct DensitySeed {
    int cx = 0;       // 正方形窗口中心（区块坐标）
    int cz = 0;
    int count = 0;    // 窗口内史莱姆区块数
};

class GpuScan {
public:
    virtual ~GpuScan() = default;
    virtual bool available() const noexcept = 0;

    // v3.7.6：批量计算候选中心的环带覆盖（结果写入 out，尺寸 nx*nz，行主序）。
    // 默认不支持（返回 false）⇒ 调用方回退 CPU 路径。
    virtual bool annulusCoverage(const struct AnnulusCoverageRequest& req, std::vector<int>& out,
                                 std::string* err) const
    {
        (void)req; (void)out; (void)err;
        return false;
    }
    // 扫描 w×h 矩形区域（区块坐标含边界；w,h>0），行主序、x 最快。
    // out 尺寸恰为 w*h，每字节 0/1（1=史莱姆）。与 CPU 判定逐位一致。
    // const：引擎只读使用；实现内部缓冲为可变细节。
    virtual bool scanRect(Version version, int64_t worldSeed, int minX, int minZ, int w, int h,
                          std::vector<uint8_t>& out, std::string* err) const = 0;

    // v3.7.3：Java 版打包字扫描（每 64 区块一个 64 位字，行主序，wpr = ceil(w/64)）。
    // 默认不支持（返回 false）⇒ 引擎回退 scanRect 的字节路径。回读量只有字节版的 1/8。
    virtual bool scanRectWords(Version version, int64_t worldSeed, int minX, int minZ, int w, int h,
                               std::vector<uint64_t>& out, std::string* err) const
    {
        (void)version; (void)worldSeed; (void)minX; (void)minZ; (void)w; (void)h; (void)out; (void)err;
        return false;
    }

    // ---- v3.3.1（任务书 3.2）：GPU 密度匹配（可选增强） ----
    // 在 GPU 上完成「区块判定 + 窗口密度统计」，只回传命中窗口（含密度），
    // 避免把整幅位图搬回主机；实现需自行按显存分块（单块 ≤512MB）并保证单块内可取消（<1s）。
    // 语义必须与 CPU 精确模式逐位一致：命中集合 = { 锚点 | 窗口内史莱姆数 / g² ≥ minDensity }，
    // density = (float)count / (float)(g*g)（整数计数后同一次浮点除法，无浮点累加误差）。
    // 返回 false = 不支持/失败（引擎自动回退 CPU 路径）。cancel 置位后应尽快返回 true，
    // out 允许只含已完成部分（引擎按取消处理）。
    // 注：DensityMatchParams 在文件后部（MatchResult 之后）定义，故此处用 struct 前置声明；
    //     默认实现（返回 false）见文件末尾。
    virtual bool matchDensity(const struct DensityMatchParams& p, const std::atomic<bool>& cancel,
                              std::vector<struct MatchResult>& out, std::string* err) const;

    // v3.8.4：严格图案流式匹配（默认不支持 ⇒ 引擎回退 CPU 位图路径）
    virtual bool matchPattern(const struct PatternMatchParams& p, const std::atomic<bool>& cancel,
                              std::vector<struct MatchResult>& out, std::string* err) const
    {
        (void)p; (void)cancel; (void)out; (void)err;
        return false;
    }

    // v3.10.12.11：正方形密度预筛（按窗口密度取种子，而不是按距离取最近 N 个）。
    //   默认不支持；GPU 实现按 z 带流式扫描，返回全图分散的种子。
    virtual bool prescanDensitySeeds(const struct DensityPrescanParams& p, const std::atomic<bool>& cancel,
                                     std::vector<DensitySeed>& out, std::string* err) const
    {
        (void)p; (void)cancel; (void)out; (void)err;
        return false;
    }

    // ---- v3.3.3（任务书 3.1）：设备信息（供 GUI 静态预判 GPU 加速临界值） ----
    // 均为只读查询，默认实现返回空/0（未实现该接口的提供者不影响任何既有行为）。
    virtual std::string deviceName() const { return {}; }        // 如 "gfx1031 / AMD Radeon RX 6750 GRE"
    virtual uint64_t globalMemBytes() const noexcept { return 0; } // CL_DEVICE_GLOBAL_MEM_SIZE
    virtual uint32_t computeUnits() const noexcept { return 0; }   // CL_DEVICE_MAX_COMPUTE_UNITS
};

// 图案格子状态（UI 颜色）：
//   Ignore  灰色 - 不参与匹配
//   White   白色 - 要求该格不是史莱姆区块
//   Green   绿色 - 要求该格是史莱姆区块
enum class CellState : uint8_t { Ignore = 0, White = 1, Green = 2 };

// 矩形区域（含边界）
struct Rect {
    int minX = 0;
    int maxX = 0;
    int minZ = 0;
    int maxZ = 0;

    bool valid() const noexcept { return minX <= maxX && minZ <= maxZ; }
    bool contains(int x, int z) const noexcept { return x >= minX && x <= maxX && z >= minZ && z <= maxZ; }
    bool contains(const Rect& o) const noexcept
    {
        return o.valid() && minX <= o.minX && maxX >= o.maxX && minZ <= o.minZ && maxZ >= o.maxZ;
    }
    int64_t area() const noexcept { return (static_cast<int64_t>(maxX) - minX + 1) * (static_cast<int64_t>(maxZ) - minZ + 1); }
};

// 可编辑图案：rows x cols 的格子矩阵
class Pattern {
public:
    Pattern() = default;
    Pattern(int rows, int cols) : m_rows(rows), m_cols(cols), m_cells(static_cast<size_t>(rows) * cols, 0) {}

    int rows() const noexcept { return m_rows; }
    int cols() const noexcept { return m_cols; }
    bool empty() const noexcept { return m_rows <= 0 || m_cols <= 0; }

    CellState at(int r, int c) const noexcept { return static_cast<CellState>(m_cells[static_cast<size_t>(r) * m_cols + c]); }
    void set(int r, int c, CellState v) noexcept { m_cells[static_cast<size_t>(r) * m_cols + c] = static_cast<uint8_t>(v); }

    // 非灰色格数量
    int activeCount() const noexcept
    {
        int n = 0;
        for (uint8_t v : m_cells) {
            if (v != 0) ++n;
        }
        return n;
    }

    // 顺时针旋转 90 度
    Pattern rotated() const noexcept
    {
        Pattern p(m_cols, m_rows);
        for (int r = 0; r < m_rows; ++r) {
            for (int c = 0; c < m_cols; ++c) {
                // (r,c) -> (c, rows-1-r)
                p.set(c, m_rows - 1 - r, at(r, c));
            }
        }
        return p;
    }

private:
    int m_rows = 0;
    int m_cols = 0;
    std::vector<uint8_t> m_cells;
};

// 单个匹配结果（覆盖的区块范围）
struct MatchResult {
    int minX = 0; // 窗口左边界（区块坐标）
    int maxX = 0; // 窗口右边界（含）
    int minZ = 0;
    int maxZ = 0;
    float density = 0.0f; // 密度模式：网格内史莱姆密度；严格模式：0
};

// v3.3.1（任务书 3.2）：GPU 密度匹配参数（见 GpuScan::matchDensity 说明）
struct DensityMatchParams {
    Version version = Version::Java;
    int64_t worldSeed = 0;
    int64_t axMin = 0; // 锚点范围（区块坐标，含边界）
    int64_t axMax = -1;
    int64_t azMin = 0;
    int64_t azMax = -1;
    int gridSize = 5;       // 窗口边长 g（1..512）
    double minDensity = 0.3;
    int64_t maxResults = 0; // 0 = 不限（引擎负责最终 TopN）
    // v3.8.10：让 GPU 提供者可以「就地筛掉进不了 TopN 的命中」——命中可能上百万，
    //   逐条构造 MatchResult 是密集场景的主要成本（实测 350 万条约 56 ms）。
    int64_t startX = 0;          // 距离起点（用于裁剪）
    int64_t startZ = 0;
    int64_t keepCandidates = 0;  // 0 = 不做裁剪；>0 = 只回传最多这么多条最可能进榜的
    int64_t* outTotalFound = nullptr;  // 回传「命中总数」（裁剪前），引擎用它算 totalFound
    // v3.10.11.1：结果自优化（仅 GPU 密度流式路径；默认关，行为与旧版一致）
    bool earlyStop = false;        // true = 按距离由近到远派发、凑够 earlyStopTarget 即提前停止
    int64_t earlyStopTarget = 0;   // 需要保留的结果数（通常 = targetCount）
};

// v3.8.4：严格图案的 GPU 流式匹配参数（每个变体一次调用；行掩码长度 = cols）
// v3.10.12.11：正方形密度预筛参数
struct DensityPrescanParams {
    Version version = Version::Java;
    int64_t worldSeed = 0;
    int minX = 0, maxX = 0, minZ = 0, maxZ = 0;   // 区块范围
    int gridSize = 15;           // 正方形窗口边长（区块）
    double minDensity = 0.18;    // 窗口密度阈值
    int bandRows = 4096;         // 每批 z 行数
    int topPerBand = 512;        // 每带最多保留多少条（用于分散到全图）
    int maxSeeds = 4000;         // 最终种子数
    int minSeparation = 16;      // 种子最小间距（区块）
};

struct PatternMatchParams {
    Version version = Version::Java;
    int64_t worldSeed = 0;
    int64_t axMin = 0, axMax = -1, azMin = 0, azMax = -1;
    int rows = 0, cols = 0;
    const uint32_t* enableMasks = nullptr;   // rows 个：该行参与匹配的列
    const uint32_t* greenMasks = nullptr;    // rows 个：该行要求为史莱姆的列
    int64_t maxResults = 0;                  // 0 = 不限
};

// GpuScan::matchDensity 默认实现：不支持的实现返回 false（引擎回退 CPU）
inline bool GpuScan::matchDensity(const DensityMatchParams& p, const std::atomic<bool>& cancel,
                                  std::vector<MatchResult>& out, std::string* err) const
{
    (void)p;
    (void)cancel;
    (void)out;
    if (err) *err = "GPU 密度匹配未实现";
    return false;
}

// ---------- 搜索配置与结果 ----------

struct SearchOptions {
    Version version = Version::Java;
    int64_t worldSeed = 0; // 仅 Java 版使用

    // 搜索范围（区块坐标，含边界）
    int minX = 0, maxX = 0, minZ = 0, maxZ = 0;

    // v3.2.0：GPU 加速（可选）。非空且 available() 且区域超过阈值时，
    // 引擎用 GPU 完成判定扫描（位图结果与 CPU 完全一致）；失败自动回退 CPU。
    // 生命周期由调用者保证（搜索期间保持有效）。GUI 默认按配置自动启用/回退。
    const GpuScan* gpuScan = nullptr;
    // v3.10.11.1（用户要求「结果自优化」开关）：GPU 密度匹配时按距离由近到远派发，
    //   凑够目标数且后续区域不可能更近即提前停止；只影响速度与"已保留数量"统计，结果内容不变。
    bool gpuResultSelfOptimize = false;

    Mode mode = Mode::Strict;

    // v3.3.0：算法模式与参数（见 Algorithm 注释）
    Algorithm algorithm = Algorithm::Exact;
    int layerBlock = 16;       // 分层模式粗块边长（16 或 32 区块）
    double layerRatio = 0.8;   // 分层模式阈值比例（粗密度 ≥ 目标密度 × 该比例才精搜）
    int sampleStep = 8;        // 快速估算模式采样间隔（每 step×step 采 1 块）
    // 估算模式精度开关（仅 Algorithm::Sample 生效）：
    //   false（默认）：只扫描采样网格上的区块，窗口密度按采样点比例估计 —— 最快，
    //                  但估算值与真实密度的相关性上限为 1/step（采样估计的数学上界）。
    //   true（精算采样）：扫描采样锚点窗口所需的全部区块，逐窗精算密度 —— 结果与精确
    //                  模式在同一锚点上的密度完全一致（无假阳性），加速比低于前者。
    bool sampleExact = false;

    // 严格图案匹配参数
    Pattern pattern;             // 0 度朝向（UI 编辑结果）
    bool directionLocked = true; // 方向锁定：不旋转

    // 密度搜索参数
    int gridSize = 5;        // 网格边长（区块）
    double minDensity = 0.3; // 最小密度阈值 0~1

    // 查询起点（区块坐标）
    int64_t startX = 0;
    int64_t startZ = 0;

    // v3.3.6（任务书 三/调整 3）：实时命中计数，供 GUI 进度流水显示「找到 N 个」。
    // 引擎在记录每个命中时以 relaxed 原子自增；为空表示不统计。
    // 语义：至今记录到的命中数（TopN 容量截断时可能略多于最终显示条数）——
    // 只做展示，**不参与任何判定**，因此不影响结果集合与性能基准。
    std::atomic<int64_t>* liveMatches = nullptr;

    // v3.3.7（任务书 P0-3）：真实已扫描区块计数（累计值）。
    // 扫描填充循环按行累加（分块路径下跨块累计），GUI 用它显示「扫描 N/M 区块（xx%）」，
    // 与进度百分比同源 —— 为空表示不统计。**只做展示，不参与任何判定**。
    std::atomic<int64_t>* liveScanned = nullptr;

    // v3.3.7.2（任务书 1.2）：本次搜索的**引擎口径总扫描量**（与 liveScanned 同单位）。
    // 经典路径 = W×H；分块路径 = plan.totalDataEvals × W（含块间重叠行）。
    // GUI 必须用它当分母，否则「已扫描/总量」会因分母口径不同而算出 >100%。
    // 引擎在计划确定后立刻写入（早于第一次进度回调）；为空表示不公开。
    std::atomic<int64_t>* liveTotal = nullptr;

    // v3.9.12（最优候选模式）：忽略最小密度阈值，改为按「窗口内史莱姆数」从高到低返回
    //   targetCount 个最优窗口（并列时按距起点距离、再按光栅序）。属于结果要求，不是加速开关。
    bool ignoreThreshold = false;

    // 结果筛选
    int64_t targetCount = 50;    // 显示最近 N 个
    bool stopAtTarget = false;   // 达到目标数量即停止（数学保证为全局最近 N）
    bool smartRange = false;     // 智能范围限定：以查询起点为中心由近及远扫描/匹配

    // 并行与缓存
    int threads = 0;                 // 0 = 自动（全部核心）
    bool useCache = false;           // 使用基岩版缓存（仅 Bedrock 有效）
    // v3.4.13（任务书第 2/3 步的连锁反应修复）：**工作线程读缓存时必须持有这把锁**。
    //   构建模式下归并线程在写缓存（ensureRange 会重分配 ranges/bitmaps，m_outliers 会被 swap），
    //   而 runImpl 的「缓存命中填充」会在工作线程里读这些结构 ⇒ 数据竞争：
    //   块数 2 → 47 后窗口被放大，实测 6 次里 2 次 0xC0000005 崩溃；
    //   更隐蔽的是「声明已登记、位图尚未置位」的中间态会被读到（会写坏缓存结论）。
    //   分块路径把 cacheMu 传进来（仅构建模式，此时确实存在并发写者）；
    //   读到的行数据会**拷贝**到工作线程自己的缓冲，避免持锁外使用指针。
    //   为 nullptr 时（普通搜索 / 经典路径）不产生任何锁开销。
    std::mutex* cacheReadMu = nullptr;
    // v3.4.13（第 3 步几何改小的配套）：构建模式下**本块不负责收集**的尾部行区间（z 闭区间）。
    //   块的数据域末尾 hMax-1 行属于**下一块**的锚点行域；若本块也收集，同一个史莱姆区块会被
    //   两份条目分别计入（先到的找不到 range ⇒ 进 outliers，后到的置位）⇒ entryCount 虚高且
    //   随归并顺序波动（实测 ±224 条）。末块没有「下一块」⇒ 区间留空（skipZ0 > skipZ1），
    //   其尾部行照旧收集（成为 outliers，与旧行为一致）。
    int collectSkipZ0 = 0;
    int collectSkipZ1 = -1;
    const CacheManager* cache = nullptr; // 缓存只读句柄（生命周期由调用者保证）
    // v3.12.3：基岩版搜索的分带缓存加载器。非空时引擎按 band 调用、
    // 每次返回只包含该 band 相交 pack 的 CacheManager；band 结束即释放。
    using CacheBandLoader = std::function<std::shared_ptr<CacheManager>(const Rect& band)>;
    CacheBandLoader cacheBandLoader;
    // 仅读元数据的快速提示：band 是否可能与缓存相交（用于缓存优先排序）。
    std::function<bool(const Rect& band)> cacheBandMayCover;
    int cacheBandSize = 8192; // 每个核心 band 的边长（区块，与固定 pack 覆盖一致）
    // v3.1.2：分块路径的增量缓存写入（每块完成即合并，避免整图收集导致缓存为空/内存爆炸）
    CacheManager* cacheWrite = nullptr; // 可与 cache 为同一对象；仅分块路径使用
    int64_t cacheBudgetBytes = 0;       // >0：每次合并后按此预算丢弃最旧分块覆盖声明
    // v3.3.7.2（任务书步骤 3 / P0-C）：缓存写入**准入控制**。
    // 分块路径按「已收集条目 vs 预算条目」判断本次范围是否值得写缓存；一旦超限，
    // 后续分块只读缓存、不再收集条目，也不再登记其范围（覆盖声明因此保持正确）。
    bool cacheWriteDisabled = false;

    // v3.3.9（任务书 1.1）：**缓存构建模式** —— 只做史莱姆判定 + 写缓存，不做匹配（唯一可写缓存的模式）。
    //   调用方需设置：version=Bedrock、useCache=true、cache=cacheWrite=目标缓存、cacheBudgetBytes=容量上限。
    //   引擎行为：强制走分块路径（内存有界）；整块已被缓存覆盖 ⇒ 跳过（中断续传）；
    //             达到容量上限 ⇒ 立即停止（out.hitCacheBudget=true），不做「边写边丢」。
    bool buildMode = false;
    std::atomic<int64_t>* liveRecorded = nullptr;       // 本次新判定并写入的区块数（构建模式进度用）
    std::atomic<int64_t>* liveSkippedCovered = nullptr; // 因缓存已覆盖而跳过的区块数（续传进度用）
    // v3.3.9（任务书 1.4）：进度区分「缓存扫描」与「新扩展扫描」——0 = 正在判定新区块，1 = 正在从缓存直取
    std::atomic<int>* livePhase = nullptr;
    // v3.3.9（任务书 2.1）：内存上限（字节；0 = 自动。设置栏 → 分块位图预算/并发收敛）
    int64_t memBudgetBytes = 0;
    // v3.3.9（任务书 2.2）：CPU 占用率上限（10~100；100 = 不限。GUI 据此收敛生效线程数）
    int cpuLimitPercent = 100;

    Rect region() const noexcept { return Rect{ minX, maxX, minZ, maxZ }; }
};

struct SearchStats {
    int64_t scannedChunks = 0;      // 扫描（含缓存命中/计算）区块总数
    int64_t windowsEvaluated = 0;   // 匹配窗口数
    double scanMs = 0.0;            // 扫描耗时（毫秒）
    double matchMs = 0.0;           // 匹配耗时（毫秒）
    double totalMs = 0.0;           // 总耗时（毫秒，分带缓存时包含缓存准备）
    double cacheLoadMs = 0.0;       // 分带缓存准备耗时（毫秒）
    // v3.3.8（任务 6 / E3）：填充与密度滑窗**独立**计时（用于判据「滑窗耗时可随位图缩小而下降吗」）
    double fillMs = 0.0;            // 填充（扫描/史莱姆判定 + 置位）阶段耗时
    double slideWindowMs = 0.0;     // 密度滑动窗口（vertSum 行块滑窗）路径耗时；非滑窗路径恒为 0
    int threadsUsed = 1;
};

// v3.1.1：整图级搜索的耗时估算（基于当前机器实测速率与分块计划）
struct SearchEstimate {
    bool ok = false;         // 估算是否可用（样本区域过小时失败）
    int64_t areaChunks = 0;  // 请求区域区块总数
    int64_t evalChunks = 0;  // 实际需判定的区块数（含分块重叠，≥ areaChunks）
    int64_t windows = 0;     // 匹配窗口总数
    double scanRate = 0.0;   // 实测扫描速率（区块/秒，单线程基准）
    double matchRate = 0.0;  // 实测匹配速率（窗口/秒）
    double scanWallSec = 0.0;   // 预计扫描耗时（并行分块）
    double matchWallSec = 0.0;  // 预计匹配耗时（并行分块）
    double totalSec = 0.0;      // 预计总耗时
    int chunkConcurrency = 1;   // 分块并行数
    int chunkRows = 0;          // 每块数据行数（含重叠）
};

struct SearchOutcome {
    bool ok = false;                 // 是否成功（失败时 error 有内容）
    bool cancelled = false;          // 是否被取消
    bool stoppedEarly = false;       // 是否因达到目标数量提前结束匹配
    // v3.3.7（任务书 P0-1，B 方案）：分块路径中因「不可能更优」而被跳过的分块数
    // （>0 表示跨块剪枝生效；结果仍为全局最近 N）
    int64_t chunksPruned = 0;
    // v3.9.10（过程可视化）：GPU 实际使用情况与回退原因（只做展示，不参与判定）
    bool gpuScanUsed = false;        // 扫描阶段是否真的用了 GPU
    bool gpuMatchUsed = false;       // 匹配阶段是否真的用了 GPU 流式
    std::string gpuFallbackReason;   // 非空 = GPU 尝试失败并回退 CPU 的原因
    bool ignoredThreshold = false;   // v3.9.12：本次是否走了「最优候选（忽略阈值）」模式
    bool cacheHandledIncrementally = false; // v3.1.2：分块路径已在引擎内按块合并缓存
    bool chunkedPath = false;               // v3.3.7.2：本次是否走了分块路径（性能汇总/日志用）
    // v3.3.8（任务 3+5 第 3 步）：整区命中缓存位图 ⇒ 填充阶段整段跳过（不再逐区块判定）
    bool cacheFillSkipped = false;
    int64_t cacheHitPoints = 0;             // 因缓存位图而省掉的填充点数（W×H 累计）
    // v3.3.8（任务 4：缓存优先 + 命中即停）：启用提前停止时是否改为「缓存覆盖块优先」的处理顺序，
    // 以及被归入优先组的块数（>0 表示缓存优先排序生效）
    bool cacheFirstOrder = false;
    int64_t cacheFirstChunks = 0;
    // v3.3.8.1（任务书 1.1）：缓存优先阶段凑够目标数后「立即停」（不再扫描未覆盖区）是否触发
    bool cacheFirstStopped = false;
    // v3.3.9（任务书 1.1）：缓存构建模式 —— 是否因达到缓存容量上限而停止、以及跳过了多少「已缓存」区块
    bool hitCacheBudget = false;
    int64_t buildSkippedChunks = 0;
    std::string error;               // 校验/运行错误信息（中文，可直接展示）

    std::vector<MatchResult> results; // 按距离升序、最近 targetCount 个
    int64_t totalFound = 0;           // 匹配到的总窗口数（early stop 时为已确认数量）
    std::vector<std::pair<int, int>> newCacheChunks; // 本次新计算出的史莱姆区块（Bedrock + 缓存模式）
    int64_t windowsSkipped = 0;       // v3.3.0：分层/估算模式剪枝跳过的窗口数（精确/保守恒 0）
    SearchStats stats;
};

// 进度回调（在工作线程内被调用，须线程安全）
// fraction: 0~1（扫描约占 0~0.75，匹配 0.75~1）
// scanning: 当前是否处于扫描阶段（匹配阶段无速度概念）
// chunksPerSec/etaSec 仅在扫描阶段有效
using ProgressFn = std::function<void(double fraction, bool scanning, double chunksPerSec, double etaSec)>;

// ---------- 引擎 ----------

class SlimeEngine {
public:
    // ---- 单区块判定（公开，便于单元测试/复用） ----
    // v3.9.12：最优候选模式（忽略阈值，按窗口密度取最优 N 个；经典路径 CPU 实现）
    // v3.10.10.1：最小密度=0（全部窗口有效）时的最近 N 快速路径
    void runZeroThresholdFast(const SearchOptions& opt, const std::atomic<bool>& cancel,
                              const ProgressFn& progress, SearchOutcome& out) const noexcept;
    // v3.10.10.1：基岩版整行批量判定（复用 AVX2 批量 MT19937）
    static void fillSlimeRowBedrock(int cz, int cx0, int n, uint8_t* out) noexcept;
    void runBestDensity(const SearchOptions& opt, const std::atomic<bool>& cancel, const ProgressFn& progress,
                        SearchOutcome& out) const noexcept;

    static bool isSlimeChunk(Version v, int64_t worldSeed, int chunkX, int chunkZ) noexcept;
    static bool isSlimeChunkJava(int64_t worldSeed, int chunkX, int chunkZ) noexcept;
    static bool isSlimeChunkBedrockMT(int chunkX, int chunkZ) noexcept;

    // v3.5.15：整行批量判定（Java）—— out[i] = isSlimeChunkJava(seed, cx0 + i, cz)，i ∈ [0, n)。
    //   行基座只算一次、循环体内没有 std::function 间接调用，规划器的占用图快数倍。
    static void fillSlimeRowJava(int64_t worldSeed, int cz, int cx0, int n, uint8_t* out) noexcept;

    // ---- 主入口：执行一次完整搜索 ----
    // cancel：取消标志（外部置 true 后尽快退出，不保存部分结果）
    // progress：进度回调，可为空
    // 内部将 omp_set_num_threads(options.threads)
    void run(const SearchOptions& options,
             const std::atomic<bool>& cancel,
             const ProgressFn& progress,
             SearchOutcome& out) const noexcept;

    // v3.12.3：分带缓存搜索。按 cacheBandSize 切分区域，每带临时加载缓存，
    // 用 halo 保证边界结果不丢，合并后按原排序/目标数截断。
    void runBanded(const SearchOptions& options,
                   const std::atomic<bool>& cancel,
                   const ProgressFn& progress,
                   SearchOutcome& out) const noexcept;

    // v3.1.1：估算一次搜索的耗时（含整图分块模式），不修改任何状态
    static bool estimateSearch(const SearchOptions& options, int threads, SearchEstimate& out) noexcept;

    // 密度窗口判定（独立接口；预留 biomeWeight 权重参数，默认 nullptr = 权重 1.0，
    // 当前仅占位不参与计算，供后续群系权重扩展）
    static bool densityWindowMeets(int slimeCount, int gridSize, double minDensity,
                                   const float* biomeWeight = nullptr) noexcept;

    // ---- 引擎级校验（返回空串表示通过）----
    static std::string validate(const SearchOptions& o) noexcept;

private:
    // allowChunk=false 时强制单次完整位图（分块内部的子调用）
    void runImpl(const SearchOptions& options,
                 const std::atomic<bool>& cancel,
                 const ProgressFn& progress,
                 SearchOutcome& out,
                 bool allowChunk) const noexcept;
    // 整图/超大区域：按 z 分块（有界内存），块内走经典路径，并行执行
    void runChunked(const SearchOptions& options,
                    const std::atomic<bool>& cancel,
                    const ProgressFn& progress,
                    SearchOutcome& out) const noexcept;
};
} // namespace slime
