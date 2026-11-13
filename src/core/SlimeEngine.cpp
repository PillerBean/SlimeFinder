// SlimeEngine.cpp - 核心计算引擎实现
//
// 设计要点：
//   * 扫描阶段：按行并行（OpenMP），行内分段（缓存覆盖段 / 计算段），
//     取消检查粒度约 65536 区块，进度按区块数 0.5% 步进上报。
//   * 严格匹配：行状态 64 位掩码比较；支持方向锁定与 90° 旋转变体。
//   * 密度搜索：行内窗口按行扫描 + popcount 统计（内存 O(W)，无整幅积分图）。
//   * 结果排序与参考实现（Python v2.9.1 + SlimeChecker.exe）一致：
//     按窗口中心距离升序，平局按光栅枚举序；提前停止使用“剩余集合距离下界”，
//     数学上保证仍返回全局最近 N 个。
#include "core/SlimeEngine.h"

#include <algorithm>
#include <queue>
#include <bit>
#include <intrin.h>   // v3.6.20：SLIME_PROFILE 用的 __rdtsc
#include <array>
#include <chrono>
#include <map>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <condition_variable> // v3.4.13：归并线程 / 背压等待
#include <deque>                // v3.4.13：归并批次队列
#include <mutex>
#include <thread>
#include <tuple>
#include <unordered_set>

#ifdef _OPENMP
#include <omp.h>
#endif

#include "core/CacheManager.h"
#include "core/FastMatch.h" // v3.3.0：AVX2 内核（运行时检测 + 标量回退）
#include "core/FastMt.h"
#include "core/FastJava.h"   // v3.6.16：Java 判定 AVX2    // v3.3.0（3.4）：AVX2 批量 MT19937（基岩版扫描）
#include "core/PerfLog.h"   // v3.3.7.2（任务书步骤 2 / P1-D）：引擎侧性能事件
#include "core/RandomGenerators.h"
#include "utils/IncidentLog.h" // v3.3.7.2：关键信息日志（性能事件与汇总写入同一文件）

namespace slime {

namespace {

// v3.6.16：Java 扫描是否走 AVX2 内核（运行期检测一次；SLIME_FASTJAVA=0 可强制标量对照）
// v3.6.18：密度滑窗是否走 SIMD 批量计数
const bool kUseSimdDensitySums = []
{
    const char* e = std::getenv("SLIME_DENS_SIMD");
    if (e && *e == '0') return false;
    return cpuHasAvx2();
}();

// v3.6.20：SLIME_PROFILE=1 时的行内核计时（rdtsc 周期数）
// v3.7.1：GPU 回读的「每区块 1 字节」→ 64 位位图字。
//   标量写法每区块要一次移位+或（串行依赖），实测 0.97 ns/区块、占了 GPU 路径 60% 时间；
//   这里用 SSE2 的 movemask（x64 基线指令，无需 AVX2）一次出 16 位：0/1 字节比较后取每字节最高位。
inline uint64_t packBytesToBits(const uint8_t* src, int64_t n, int shiftBits) noexcept
{
#if defined(_M_X64) || defined(_M_IX86) || defined(__SSE2__)
    uint64_t acc = 0;
    int64_t i = 0;
    const __m128i one = _mm_set1_epi8(1);
    for (; i + 16 <= n; i += 16) {
        const __m128i v = _mm_loadu_si128(reinterpret_cast<const __m128i*>(src + i));
        const int m = _mm_movemask_epi8(_mm_cmpeq_epi8(v, one));
        acc |= static_cast<uint64_t>(static_cast<uint16_t>(m)) << (shiftBits + i);
    }
    for (; i < n; ++i) {
        if (src[i]) acc |= 1ULL << (shiftBits + i);
    }
    return acc;
#else
    uint64_t acc = 0;
    for (int64_t i = 0; i < n; ++i) {
        if (src[i]) acc |= 1ULL << (shiftBits + i);
    }
    return acc;
#endif
}

// v3.7.1（用户要求）：GPU 占用率上限 —— 环境变量 SLIME_GPU_MAX_PCT（10~100），
//   由界面写进环境；引擎在每个瓦片后按占空比休眠，保持「GPU 忙时间 / 总时间」不超过上限。
int gpuLimitPercentEnv() noexcept
{
    static const int pct = [] {
        const char* e = std::getenv("SLIME_GPU_MAX_PCT");
        int v = (e && *e) ? std::atoi(e) : 100;
        if (v < 10) v = 10;
        if (v > 100) v = 100;
        return v;
    }();
    return pct;
}

// v3.7.1：GPU 阶段两段计时（内核+回读 / 逐行集成），SLIME_PROFILE=1 时输出
std::atomic<int64_t> g_profGpuScanCycles{ 0 };
std::atomic<int64_t> g_profGpuIntegrateCycles{ 0 };
std::atomic<int64_t> g_profGpuChunks{ 0 };

const bool kScanProfileOn = []
{
    const char* e = std::getenv("SLIME_PROFILE");
    return e && *e == '1';
}();
std::atomic<int64_t> g_profKernelCycles{ 0 };
// v3.8.0：密度匹配阶段细分（vertSum 维护 vs 窗口计数/命中收集）
std::atomic<int64_t> g_profDensVertCycles{ 0 };
std::atomic<int64_t> g_profDensMaskCycles{ 0 };
std::atomic<int64_t> g_profDensRows{ 0 };
std::atomic<int64_t> g_profHits{ 0 };
std::atomic<int64_t> g_profPruned{ 0 };
std::atomic<int64_t> g_profKernelChunks{ 0 };

const bool kUseFastJava = []
{
    const char* e = std::getenv("SLIME_FASTJAVA");
    if (e && *e == '0') return false;
    return fastjava::available();
}();

// v3.6.20：扫描整字直路是否启用（SLIME_SCAN_FAST=0 可回退旧循环做 A/B）
const bool kUseScanFastPath = []
{
    const char* e = std::getenv("SLIME_SCAN_FAST");
    return !(e && *e == '0');
}();


// v3.3.7.2：原子取最大（多个工作线程可能并发上报内存峰值）
inline void atomicMaxDouble(std::atomic<double>& a, double v) noexcept
{
    double prev = a.load(std::memory_order_relaxed);
    while (v > prev && !a.compare_exchange_weak(prev, v, std::memory_order_relaxed)) {
    }
}

// ---------- 常量 ----------
// v3.1.1：不再限制总面积/总位图大小；超大区域自动切分为 z 分块（有界内存）。
// 经典单次路径：整区位图 ≤ kClassicWordsLimit words（约 512MB）
constexpr int64_t kClassicWordsLimit = 64'000'000LL;
// v3.4.11（用户拍板：集中常量 + 环境变量覆盖）：经典滑动路径的「提前停止判界分块行数」。
//   为什么是 64：滑动窗口一次 vertSum 初始化的开销按行摊薄（块越大越省），而判界粒度越细越早停；
//   64 行是两者的经验折中（不是 L3/缓存行推导值）。排障或做对照实验时可用
//   SLIME_STOP_BLOCK_ROWS=<n> 覆盖（1..4096）。单元测试用它验证「分块大小不影响结果」。
constexpr int64_t kStopBlockRowsDefault = 64;

// v3.9.4：智能范围的「行带高度」。带内连续升序（滑窗可用），带间按距离排序。
//   必须与并行/顺序滑窗的块大小一致，否则跨块行号跳变会算错。
//   可用 SLIME_SMART_BAND 覆盖（用于 A/B 测试）。
int64_t smartBandRows()
{
    static const int64_t v = []() -> int64_t {
        const char* e = std::getenv("SLIME_SMART_BAND");
        if (e == nullptr || *e == '\0') return 256;
        char* end = nullptr;
        const long long n = std::strtoll(e, &end, 10);
        if (end == e || n <= 0) return 256;
        return std::clamp<long long>(n, 8, 4096);
    }();
    return v;
}

int64_t stopBlockRows()
{
    static const int64_t v = []() -> int64_t {
        const char* e = std::getenv("SLIME_STOP_BLOCK_ROWS");
        if (e == nullptr || *e == '\0') return kStopBlockRowsDefault;
        char* end = nullptr;
        const long long n = std::strtoll(e, &end, 10);
        if (end == e || n <= 0) return kStopBlockRowsDefault;
        return std::clamp<long long>(n, 1, 4096);
    }();
    return v;
}

// 分块路径：单块位图 ≤ kChunkWordsLimit words（约 128MB）；并行使用全部用户线程
// （整图扫描目标 ≤ 50 分钟：12 线程 × 每线程 ~5 亿区块/秒 → ~60 亿区块/秒量级）
constexpr int64_t kChunkWordsLimit = 16'000'000LL;
constexpr int kChunkThreads = 12;
constexpr int64_t kScanStride = 65536;        // 扫描取消检查粒度
constexpr int32_t kBedrockBatch = 64;         // v3.3.0（3.4）：基岩版 AVX2 批量 MT19937 批大小
constexpr double kScanFrac0 = 0.0;            // 扫描进度区间 [0, 0.75)
constexpr double kScanFrac1 = 0.75;
constexpr double kMatchFrac0 = 0.75;          // 匹配进度区间 [0.75, 1.0)
constexpr double kMatchFrac1 = 1.0;

// Java 版单区块判定（任务书 6.1：精确 32 位溢出 + Java LCG）
// v3.1.0 优化：拆分为 行基座(rowBase，仅依赖 z 与种子，每行算一次)
//            + 每区块 x 相关项；二者在 uint64 域相加（加法交换律，mod 2^64 结果一致）。
inline int64_t mul64Wrap(int64_t a, int64_t b) noexcept
{
    return static_cast<int64_t>(static_cast<uint64_t>(a) * static_cast<uint64_t>(b));
}

// 每行常量：worldSeed + (int64)int32Mul(cz,cz)*4392871(64位回绕) + (int64)int32Mul(cz,389711)
inline uint64_t javaRowBase(int64_t worldSeed, int32_t cz) noexcept
{
    const int32_t t3i = int32Mul(cz, cz);
    const int32_t t4 = int32Mul(cz, 389711);
    uint64_t b = static_cast<uint64_t>(worldSeed);
    b += static_cast<uint64_t>(mul64Wrap(static_cast<int64_t>(t3i), 4392871LL));
    b += static_cast<uint64_t>(static_cast<int64_t>(t4));
    return b;
}

// 单个 x 项组合：se32(int32Mul(int32Mul(x,x),4987142)) + se32(int32Mul(x,5947611))
inline uint64_t javaXTail(int32_t x) noexcept
{
    const int32_t t1 = int32Mul(int32Mul(x, x), 4987142);
    const int32_t t2 = int32Mul(x, 5947611);
    uint64_t s = static_cast<uint64_t>(static_cast<int64_t>(t1));
    s += static_cast<uint64_t>(static_cast<int64_t>(t2));
    return s;
}

// 由行基座 + x 尾项判定（LCG 尾段与参考实现一致）
inline bool javaSlimeFromTail(uint64_t seed, uint64_t xTail) noexcept
{
    uint64_t s = seed + xTail;
    s ^= 987234911ULL;
    JavaRandom rng(s);
    return rng.nextInt(10) == 0;
}

inline bool isSlimeJavaFast(int64_t worldSeed, int32_t cx, int32_t cz) noexcept
{
    return javaSlimeFromTail(javaRowBase(worldSeed, cz), javaXTail(cx));
}

// 基岩版单区块判定（参考实现精确算法：MT19937，种子 = x*0x1F1F1F1F 低32位 ^ z）
// v3.1.0 优化：消除 twist 阶段全部取模（i+397 mod 624 == i-227 for i>=227），
// 分支用掩码代替，单函数内联完成 init(624) + twist(624) + 首次加扰输出。
inline uint32_t bedrockFirstDraw(uint32_t seed) noexcept
{
    uint32_t mt[624];
    mt[0] = seed;
    for (int i = 1; i < 624; ++i) {
        mt[i] = (1812433253UL * (mt[i - 1] ^ (mt[i - 1] >> 30)) + static_cast<uint32_t>(i)) & 0xFFFFFFFFUL;
    }
    // twist：y = (mt[i]&0x80000000)|(mt[i+1]&0x7FFFFFFF)
    //      mt[i] = mt[(i+397)%624] ^ (y>>1) ^ (y&1 ? 0x9908B0DF : 0)
    for (int i = 0; i < 227; ++i) {
        const uint32_t y = (mt[i] & 0x80000000UL) | (mt[i + 1] & 0x7FFFFFFFUL);
        mt[i] = mt[i + 397] ^ (y >> 1) ^ (0x9908B0DFUL & static_cast<uint32_t>(-static_cast<int32_t>(y & 1UL)));
    }
    for (int i = 227; i < 623; ++i) {
        const uint32_t y = (mt[i] & 0x80000000UL) | (mt[i + 1] & 0x7FFFFFFFUL);
        mt[i] = mt[i - 227] ^ (y >> 1) ^ (0x9908B0DFUL & static_cast<uint32_t>(-static_cast<int32_t>(y & 1UL)));
    }
    {
        const int i = 623;
        const uint32_t y = (mt[i] & 0x80000000UL) | (mt[0] & 0x7FFFFFFFUL);
        mt[i] = mt[396] ^ (y >> 1) ^ (0x9908B0DFUL & static_cast<uint32_t>(-static_cast<int32_t>(y & 1UL)));
    }
    uint32_t v = mt[0];
    v ^= (v >> 11);
    v ^= (v << 7) & 0x9D2C5680UL;
    v ^= (v << 15) & 0xEFC60000UL;
    v ^= (v >> 18);
    return v;
}

inline bool isSlimeBedrockFast(int32_t cx, int32_t cz) noexcept
{
    const uint32_t s = umul32Lo(static_cast<uint32_t>(cx), 0x1F1F1F1FUL) ^ static_cast<uint32_t>(cz);
    return (bedrockFirstDraw(s) % 10U) == 0U;
}

// v3.1.0：两区块并行判定。两条 MT 初始化链相互独立，融合进同一循环后
// 可被乱序执行核重叠（延迟隐藏），约提升 1.6~2 倍。
inline uint32_t bedrockChunkSeed(int32_t cx, uint32_t czu) noexcept
{
    return umul32Lo(static_cast<uint32_t>(cx), 0x1F1F1F1FUL) ^ czu;
}

inline void bedrockPairDraw(uint32_t seedA, uint32_t seedB, bool& slimeA, bool& slimeB) noexcept
{
    uint32_t mA[624];
    uint32_t mB[624];
    mA[0] = seedA;
    mB[0] = seedB;
    for (int i = 1; i < 624; ++i) {
        mA[i] = (1812433253UL * (mA[i - 1] ^ (mA[i - 1] >> 30)) + static_cast<uint32_t>(i)) & 0xFFFFFFFFUL;
        mB[i] = (1812433253UL * (mB[i - 1] ^ (mB[i - 1] >> 30)) + static_cast<uint32_t>(i)) & 0xFFFFFFFFUL;
    }
    for (int i = 0; i < 227; ++i) {
        const uint32_t yA = (mA[i] & 0x80000000UL) | (mA[i + 1] & 0x7FFFFFFFUL);
        const uint32_t yB = (mB[i] & 0x80000000UL) | (mB[i + 1] & 0x7FFFFFFFUL);
        mA[i] = mA[i + 397] ^ (yA >> 1) ^ (0x9908B0DFUL & static_cast<uint32_t>(-static_cast<int32_t>(yA & 1UL)));
        mB[i] = mB[i + 397] ^ (yB >> 1) ^ (0x9908B0DFUL & static_cast<uint32_t>(-static_cast<int32_t>(yB & 1UL)));
    }
    for (int i = 227; i < 623; ++i) {
        const uint32_t yA = (mA[i] & 0x80000000UL) | (mA[i + 1] & 0x7FFFFFFFUL);
        const uint32_t yB = (mB[i] & 0x80000000UL) | (mB[i + 1] & 0x7FFFFFFFUL);
        mA[i] = mA[i - 227] ^ (yA >> 1) ^ (0x9908B0DFUL & static_cast<uint32_t>(-static_cast<int32_t>(yA & 1UL)));
        mB[i] = mB[i - 227] ^ (yB >> 1) ^ (0x9908B0DFUL & static_cast<uint32_t>(-static_cast<int32_t>(yB & 1UL)));
    }
    {
        const uint32_t yA = (mA[623] & 0x80000000UL) | (mA[0] & 0x7FFFFFFFUL);
        const uint32_t yB = (mB[623] & 0x80000000UL) | (mB[0] & 0x7FFFFFFFUL);
        mA[623] = mA[396] ^ (yA >> 1) ^ (0x9908B0DFUL & static_cast<uint32_t>(-static_cast<int32_t>(yA & 1UL)));
        mB[623] = mB[396] ^ (yB >> 1) ^ (0x9908B0DFUL & static_cast<uint32_t>(-static_cast<int32_t>(yB & 1UL)));
    }
    uint32_t vA = mA[0], vB = mB[0];
    vA ^= (vA >> 11); vB ^= (vB >> 11);
    vA ^= (vA << 7) & 0x9D2C5680UL; vB ^= (vB << 7) & 0x9D2C5680UL;
    vA ^= (vA << 15) & 0xEFC60000UL; vB ^= (vB << 15) & 0xEFC60000UL;
    vA ^= (vA >> 18); vB ^= (vB >> 18);
    slimeA = (vA % 10U) == 0U;
    slimeB = (vB % 10U) == 0U;
}

// ---------- 区块位图（行主序，uint64 位压缩） ----------
class BitGrid {
public:
    bool init(int64_t x0, int64_t z0, int64_t w, int64_t h) noexcept
    {
        if (w <= 0 || h <= 0) return false;
        m_x0 = x0; m_z0 = z0; m_w = w; m_h = h;
        m_wpr = (w + 63) / 64;
        try {
            m_bits.assign(static_cast<size_t>(m_h * m_wpr), 0ULL);
        } catch (...) {
            return false;
        }
        return true;
    }
    int64_t wordsPerRow() const noexcept { return m_wpr; }
    int64_t wordCount() const noexcept { return m_h * m_wpr; }

    inline void setSlime(int64_t x, int64_t z) noexcept
    {
        const int64_t rx = x - m_x0;
        const int64_t rz = z - m_z0;
        m_bits[static_cast<size_t>(rz * m_wpr + (rx >> 6))] |= 1ULL << (rx & 63);
    }
    // 行位图起始指针（v3.1.0：整段寄存器累计后按字写入）
    inline uint64_t* rowBits(int64_t z) noexcept
    {
        return m_bits.data() + static_cast<size_t>((z - m_z0) * m_wpr);
    }
    inline const uint64_t* rowBits(int64_t z) const noexcept
    {
        return m_bits.data() + static_cast<size_t>((z - m_z0) * m_wpr);
    }
    inline bool isSlime(int64_t x, int64_t z) const noexcept
    {
        const int64_t rx = x - m_x0;
        const int64_t rz = z - m_z0;
        return (m_bits[static_cast<size_t>(rz * m_wpr + (rx >> 6))] >> (rx & 63)) & 1ULL;
    }
    // 行内 [x, x+len) 的 len 位（0 < len <= 64），低位对齐
    inline uint64_t segmentBits(int64_t z, int64_t x, int len) const noexcept
    {
        const int64_t rx = x - m_x0;
        const int64_t rz = z - m_z0;
        const uint64_t* row = m_bits.data() + static_cast<size_t>(rz * m_wpr);
        const int64_t word = rx >> 6;
        const int shift = static_cast<int>(rx & 63);
        uint64_t v;
        if (shift == 0) {
            v = row[word];
        } else {
            v = (row[word] >> shift);
            if (word + 1 < m_wpr) v |= (row[word + 1] << (64 - shift));
        }
        if (len < 64) v &= (1ULL << len) - 1ULL;
        return v;
    }
    // 行内 [x, x+len) 中置位个数（任意 len，分块 popcount）
    inline int countBits(int64_t z, int64_t x, int len) const noexcept
    {
        if (len <= 0) return 0;
        const int64_t rx = x - m_x0;
        const int64_t rz = z - m_z0;
        const uint64_t* row = m_bits.data() + static_cast<size_t>(rz * m_wpr);
        int64_t w0 = rx >> 6;
        const int shift0 = static_cast<int>(rx & 63);
        int cnt = 0;
        if (len <= 64) {
            uint64_t v = row[w0] >> shift0;
            if (shift0 != 0 && w0 + 1 < m_wpr) v |= row[w0 + 1] << (64 - shift0);
            if (len < 64) v &= (1ULL << len) - 1ULL;
            return popcount64(v);
        }
        // 首字
        if (shift0 != 0) {
            cnt += popcount64(row[w0] >> shift0);
            len -= 64 - shift0;
            ++w0;
        }
        // 中间整字
        while (len >= 64 && w0 < m_wpr) {
            cnt += popcount64(row[w0]);
            ++w0;
            len -= 64;
        }
        // 尾字
        if (len > 0 && w0 < m_wpr) {
            cnt += popcount64(row[w0] & ((1ULL << len) - 1ULL));
        }
        return cnt;
    }

private:
    static inline int popcount64(uint64_t v) noexcept
    {
#if defined(_MSC_VER) && defined(_M_X64)
        return static_cast<int>(__popcnt64(v));
#else
        return __builtin_popcountll(v);
#endif
    }

    int64_t m_x0 = 0, m_z0 = 0, m_w = 0, m_h = 0, m_wpr = 0;
    std::vector<uint64_t> m_bits;
};

// ---------- 进度发射器：200 步（0.5%）节流 ----------
// v3.10.3：GPU 阶段标记（livePhase=2 扫描中 / 3 匹配中；析构自动清零）——
//   供 GUI 在进度行明确显示「GPU 扫描/GPU 匹配」，GPU 干活时用户能看见。
struct GpuPhaseMark {
    std::atomic<int>* p = nullptr;
    int v = 0;
    GpuPhaseMark(std::atomic<int>* ptr, int value) : p(ptr), v(value)
    {
        if (p) p->store(v, std::memory_order_relaxed);
    }
    ~GpuPhaseMark()
    {
        if (p) p->store(0, std::memory_order_relaxed);
    }
};

class Emitter {
public:
    Emitter(const ProgressFn& cb, double f0, double f1, bool scanning,
            std::atomic<int64_t>* live = nullptr)
        : m_cb(cb), m_f0(f0), m_f1(f1), m_scanning(scanning), m_live(live)
    {
    }

    // done/total → fraction；多线程安全（CAS 抢占报点）
    void report(int64_t done, int64_t total, double speed, double eta) noexcept
    {
        // v3.3.7（任务书 P0-3）：累计「真实已扫描区块数」。
        // 该函数可能被 OpenMP 并行区的多个线程乱序调用，因此这里用 CAS 只累加
        // 「超过历史最大值」的增量（单调、无竞争、不会重复计数）。
        if (m_live) {
            int64_t prev = m_lastDone.load(std::memory_order_relaxed);
            while (done > prev) {
                if (m_lastDone.compare_exchange_weak(prev, done, std::memory_order_relaxed)) {
                    m_live->fetch_add(done - prev, std::memory_order_relaxed);
                    break;
                }
            }
        }
        if (!m_cb) return;
        const int step = (total > 0) ? static_cast<int>((done * 200) / total) : 200;
        int prev = m_lastStep.load(std::memory_order_relaxed);
        if (step > prev && m_lastStep.compare_exchange_strong(prev, step, std::memory_order_relaxed)) {
            const double span = m_f1 - m_f0;
            const double f = m_f0 + span * (static_cast<double>(done) / static_cast<double>(std::max<int64_t>(1, total)));
            m_cb(std::min(0.9999, f), m_scanning, speed, eta);
        }
    }
    void finish() noexcept
    {
        if (m_cb) m_cb(m_f1, m_scanning, 0.0, 0.0);
    }

private:
    const ProgressFn& m_cb;
    double m_f0, m_f1;
    bool m_scanning;
    std::atomic<int> m_lastStep{ -1 };
    std::atomic<int64_t>* m_live = nullptr; // v3.3.7：真实扫描计数（累计）
    std::atomic<int64_t> m_lastDone{ 0 };   // 本发射器已计入的历史最大值（单调）
};

// 点到矩形（轴对齐）最近距离
inline double distToRect(double x, double z, double x0, double x1, double z0, double z1) noexcept
{
    const double dx = std::max({ x0 - x, 0.0, x - x1 });
    const double dz = std::max({ z0 - z, 0.0, z - z1 });
    return std::sqrt(dx * dx + dz * dz);
}

// ---------- 匹配条目（含平局排序键） ----------
struct Scored {
    MatchResult m;
    double dist = 0.0;
    int64_t rank = 0;
    int64_t d2 = 0;   // v3.8.0：2×偏移的整数平方距离，用于「不可能进榜」的廉价剪枝
    int count = 0;    // v3.10.10.3：窗口内史莱姆数（忽略阈值模式按计数排序）
};

inline bool betterThan(const Scored& a, const Scored& b) noexcept
{
    if (a.dist != b.dist) return a.dist < b.dist;
    return a.rank < b.rank;
}

// 容量 N 的最大堆：始终保留“最好”的 N 个
// 以 betterThan 作为堆比较子时，堆顶 = 比较序最大者 = 当前最差者。
class TopNHeap {
public:
    explicit TopNHeap(int64_t cap, bool byCount = false)
        : m_cap(std::max<int64_t>(1, cap)), m_byCount(byCount)
    {
        m_v.reserve(static_cast<size_t>(std::min<int64_t>(m_cap, 1 << 20)));
    }

    bool byCount() const noexcept { return m_byCount; }
    bool betterEq(const Scored& a, const Scored& b) const noexcept
    {
        if (!m_byCount) return betterThan(a, b);
        if (a.count != b.count) return a.count > b.count;
        if (a.d2 != b.d2) return a.d2 < b.d2;
        return a.rank < b.rank;
    }
    void push(const Scored& s)
    {
        auto cmp = [this](const Scored& a, const Scored& b) { return betterEq(a, b); };
        if (static_cast<int64_t>(m_v.size()) < m_cap) {
            m_v.push_back(s);
            std::push_heap(m_v.begin(), m_v.end(), cmp);
        } else if (betterEq(s, m_v.front())) {
            std::pop_heap(m_v.begin(), m_v.end(), cmp);
            m_v.back() = s;
            std::push_heap(m_v.begin(), m_v.end(), cmp);
        }
    }
    double worstDist() const noexcept { return m_v.empty() ? std::numeric_limits<double>::infinity() : m_v.front().dist; }
    // v3.8.0：当前最差者的整数平方距离（INT64_MAX = 还没满，无需剪枝）
    int64_t worstD2() const noexcept
    {
        return m_v.empty() ? std::numeric_limits<int64_t>::max() : m_v.front().d2;
    }
    bool full() const noexcept { return static_cast<int64_t>(m_v.size()) >= m_cap; }
    std::vector<Scored> sorted() const
    {
        std::vector<Scored> r = m_v;
        auto cmp = [this](const Scored& a, const Scored& b) { return betterEq(a, b); };
        std::sort(r.begin(), r.end(), cmp);
        return r;
    }

private:
    int64_t m_cap;
    bool m_byCount = false;
    std::vector<Scored> m_v;
};

// 图案旋转变体（含掩码预计算）
struct VariantMasks {
    int rows = 0, cols = 0;
    std::vector<uint64_t> enf; // 生效掩码（非灰格位）
    std::vector<uint64_t> grn; // 绿色期望位
    bool allIgnore = true;
};

inline VariantMasks makeVariant(const Pattern& p)
{
    VariantMasks v;
    v.rows = p.rows();
    v.cols = p.cols();
    v.enf.assign(static_cast<size_t>(v.rows), 0ULL);
    v.grn.assign(static_cast<size_t>(v.rows), 0ULL);
    for (int r = 0; r < v.rows; ++r) {
        uint64_t e = 0, g = 0;
        for (int c = 0; c < v.cols; ++c) {
            const CellState s = p.at(r, c);
            if (s != CellState::Ignore) {
                e |= 1ULL << c;
                if (s == CellState::Green) g |= 1ULL << c;
                v.allIgnore = false;
            }
        }
        v.enf[static_cast<size_t>(r)] = e;
        v.grn[static_cast<size_t>(r)] = g;
    }
    return v;
}

// ============================ v3.1.1 分块计划（整图搜索） ============================

// 图案/模式在各变体中的最大窗口行高（决定 z 分块重叠）
inline int64_t windowRowsMax(const SearchOptions& o) noexcept
{
    if (o.mode == Mode::Density) return o.gridSize;
    int64_t h = o.pattern.rows();
    if (!o.directionLocked) h = std::max<int64_t>(h, o.pattern.cols()); // 旋转后行数 = 列数
    return h;
}

// 按行数估算整区位图 words（h × ceil(w/64)），保证不溢出
inline int64_t gridWordsEstimate(int64_t w, int64_t h) noexcept
{
    return h * ((w + 63) / 64);
}

struct ChunkPlan {
    int64_t dataRowsCap = 0;  // 每块数据行数上限（含与上一块的 hMax-1 行重叠）
    int64_t anchorStep = 0;   // 每块新覆盖的锚点行数
    int64_t nChunks = 0;      // z 分块数量
    int64_t totalDataEvals = 0; // 全部块需扫描的数据行数（含重叠）
};

// 分块字容量（测试可用 SLIME_CHUNK_WORDS 缩小以便制造多分块）
inline int64_t chunkWordsCap() noexcept
{
    const char* v = std::getenv("SLIME_CHUNK_WORDS");
    if (v) {
        char* end = nullptr;
        const long long x = std::strtoll(v, &end, 10);
        // v3.3.8（任务 6 / E3）：下限从 1 MiB 放宽到 64 Ki words（512 KB 位图）——
        // E3-3 需要「2 MB/块」的位图（262144 words）做「位图变小对滑窗的收益」对照。
        if (end != v && x >= (1 << 16) && x <= kChunkWordsLimit) return static_cast<int64_t>(x);
    }
    return kChunkWordsLimit;
}

// v3.4.13（任务书第 3 步）：构建模式的**块数目标**。
//   * 只有「收集缓存条目」（构建模式）才启用；搜索/即停/旋转路径返回 1（几何不变）；
//   * 默认 = clamp(4×并发, 1, H/(2×hMax))：给每个线程至少 4 块（动态调度足够均衡），
//     同时用 H/(2×hMax) 收口，避免网格很大时重叠行（每块 hMax-1 行）反噬；
//   * 环境变量 SLIME_CHUNK_TARGET：=0 关闭（A/B 对照 / 回滚），=N 显式指定目标块数。
inline int64_t chunkTargetFor(bool collectCache, int conc, int64_t H, int64_t hMax) noexcept
{
    if (!collectCache) return 1;
    if (const char* v = std::getenv("SLIME_CHUNK_TARGET")) {
        if (*v) {
            const long long x = std::strtoll(v, nullptr, 10);
            return (x <= 0) ? 1 : static_cast<int64_t>(x);
        }
    }
    const int64_t maxUseful = std::max<int64_t>(1, H / std::max<int64_t>(1, 2 * hMax));
    return std::clamp<int64_t>(static_cast<int64_t>(std::max(1, conc)) * 4, 1, maxUseful);
}

// 与 runChunked 完全一致的分块几何（估算与执行共用同一常量）
inline ChunkPlan chunkPlanFor(int64_t H, int64_t wpr, int64_t hMax, int64_t capOverride = 0,
                               int64_t targetChunks = 1) noexcept
{
    ChunkPlan p;
    const int64_t cap = (capOverride > 0) ? capOverride : chunkWordsCap();
    const int64_t rowsFit = (hMax > 0) ? (cap / std::max<int64_t>(1, wpr)) : 0;
    p.dataRowsCap = std::clamp<int64_t>(rowsFit, hMax, H);
    // v3.4.13（任务书第 3 步）：**块数目标** —— 单块位图上限只当上限用是不够的：
    //   439.8 亿的构建场景里，段高 1028 行、单块能放 762 行 ⇒ 只切出 2 块，
    //   而并发是 12 ⇒ 10 个线程空闲（实测 CPU 17%）。这里在**构建模式**下按目标块数
    //   主动把块切小（重叠行代价由调用方的 H/(2*hMax) 收口）。
    //   搜索 / 即停 / 旋转路径不传该参数（默认 1）⇒ 几何完全不变。
    if (targetChunks > 1 && H > hMax) {
        const int64_t anchorRows = H - hMax + 1; // 需要覆盖的锚点行数
        const int64_t perChunkAnchors =
            std::max<int64_t>(1, (anchorRows + targetChunks - 1) / targetChunks);
        const int64_t rowsForTarget =
            std::clamp<int64_t>(perChunkAnchors + hMax - 1, hMax, H);
        if (rowsForTarget < p.dataRowsCap) p.dataRowsCap = rowsForTarget;
    }
    p.anchorStep = std::max<int64_t>(1, p.dataRowsCap - hMax + 1);
    int64_t start = 0;
    int64_t n = 0;
    int64_t evals = 0;
    while (start < H) {
        const int64_t rows = std::min(p.dataRowsCap, H - start);
        evals += rows;
        ++n;
        if (rows == p.dataRowsCap) {
            start += p.anchorStep;
        } else {
            start = H; // 末块
        }
    }
    p.nChunks = std::max<int64_t>(1, n);
    p.totalDataEvals = evals;
    return p;
}

// 变体窗口统计（与经典路径同一公式）
inline int64_t windowsTotalFor(const SearchOptions& o, int64_t W, int64_t H) noexcept
{
    int64_t total = 0;
    if (o.mode == Mode::Density) {
        const int64_t nAx = std::max<int64_t>(0, W - o.gridSize + 1);
        const int64_t nAz = std::max<int64_t>(0, H - o.gridSize + 1);
        total = nAx * nAz;
    } else {
        const Pattern p0 = o.pattern;
        std::vector<Pattern> pats;
        pats.push_back(p0);
        if (!o.directionLocked) {
            Pattern q = p0;
            for (int i = 0; i < 3; ++i) {
                q = q.rotated();
                pats.push_back(q);
            }
        }
        for (const Pattern& p : pats) {
            const int64_t nAx = std::max<int64_t>(0, W - p.cols() + 1);
            const int64_t nAz = std::max<int64_t>(0, H - p.rows() + 1);
            total += nAx * nAz;
        }
    }
    return total;
}

// 全局并发数：分块路径 ≤ kChunkThreads 且 ≤ 用户请求线程数，并按**内存预算**再限一次。
// v3.3.7（任务书 P0-4）：单块位图 = dataRowsCap × wordsPerRow × 8 字节（默认约 128MB），
// 12 并发即约 1.5GB；这里用预算（默认 1.5GB，SLIME_CHUNK_BUDGET_MB 可覆盖）保证峰值可控。
inline int64_t chunkBudgetBytesEnv(int64_t userBudgetBytes = 0) noexcept
{
    // v3.3.9（任务书 2.1）：用户设置的内存上限优先（设置栏 → SearchOptions::memBudgetBytes）
    if (userBudgetBytes >= 64LL * 1024 * 1024) return userBudgetBytes;
    const char* v = std::getenv("SLIME_CHUNK_BUDGET_MB");
    if (v) {
        char* end = nullptr;
        const long long mb = std::strtoll(v, &end, 10);
        if (end != v && mb >= 64 && mb <= 65536) return static_cast<int64_t>(mb) * 1024 * 1024;
    }
    return 1536LL * 1024 * 1024; // 默认 1.5GB（验收 ≤4GB；实测 12 × 128MB = 1.5GB）
}

inline int chunkConcurrencyFor(int userThreads, int64_t perChunkBytes = 0,
                               int64_t userBudgetBytes = 0) noexcept
{
#ifdef _OPENMP
    int procs = omp_get_num_procs();
#else
    int procs = 1;
#endif
    const int base = std::max(1, std::min((userThreads <= 0) ? procs : userThreads, kChunkThreads));
    if (perChunkBytes <= 0) return base;
    const int64_t budget = chunkBudgetBytesEnv(userBudgetBytes);
    const int64_t byBudget = std::max<int64_t>(1, budget / perChunkBytes);
    return static_cast<int>(std::max<int64_t>(1, std::min<int64_t>(base, byBudget)));
}

inline bool forceChunkedEnv() noexcept
{
    const char* v = std::getenv("SLIME_FORCE_CHUNKED");
    return v && std::string(v) == "1";
}

// v3.3.7（P0-1）：跨块剪枝的 A/B 对照开关（SLIME_NO_PRUNE=1 关闭剪枝，仅用于测试/排障）
inline bool noPruneEnv() noexcept
{
    const char* v = std::getenv("SLIME_NO_PRUNE");
    return v && std::string(v) == "1";
}

// ============================ v3.2.0 GPU 扫描阶段 ============================
// 单瓦片字节预算 / 最小启用块数（调参用环境变量 SLIME_GPU_TILE_BYTES / SLIME_GPU_MIN_CHUNKS）
inline int64_t gpuTileBytesEnv() noexcept
{
    const char* v = std::getenv("SLIME_GPU_TILE_BYTES");
    if (v) {
        char* end = nullptr;
        const long long x = std::strtoll(v, &end, 10);
        if (end != v && x >= (1 << 20) && x <= (1LL << 30)) return static_cast<int64_t>(x);
    }
    return 1LL << 28; // 默认 256MB/瓦片
}
// v3.8.0（流程优化 P3）：按版本给不同的 GPU 启用门槛 —— 实测交叉点差得很远：
//   Java：GPU 固定开销 ~0.4 ms、峰值 247 亿区块/秒，CPU 12 线程 57 亿 ⇒ 约 3000 万区块才划算；
//   基岩：CPU 每区块要 396 次乘法（12 线程仅 2.45 亿），GPU 16~17 亿 ⇒ 十几万区块就划算。
//   低于门槛自动走 CPU（不再出现「小范围开 GPU 反而慢」）。
inline int64_t gpuMinChunksDefault(Version v) noexcept
{
    return v == Version::Java ? (30LL << 20) : (96LL << 10);
}

inline int64_t gpuMinChunksEnv(Version v) noexcept
{
    const char* e = std::getenv("SLIME_GPU_MIN_CHUNKS");
    if (e) {
        char* end = nullptr;
        const long long x = std::strtoll(e, &end, 10);
        if (end != e && x >= 1 && x <= (1LL << 40)) return static_cast<int64_t>(x);
    }
    return gpuMinChunksDefault(v);
}

// GPU 判定扫描阶段（单线程驱动；由经典路径在 grid 分配后调用）。
// 网格位/缓存覆盖/新缓存收集/进度/取消的语义与 CPU 扫描路径完全一致：
//   - 行主序瓦片扫描（x 最快，瓦片 ≤ 预算字节）
//   - 缓存覆盖区间回读缓存（与 CPU 路径相同判定来源），其余由 GPU 字节置位
//   - 未缓存区间的新史莱姆坐标收集进 newChunks（仅 useCache+需收集时）
// 返回：1 = 完成；0 = GPU 不可用/失败（errOut 说明，调用方回退 CPU，残留位与
// CPU 结果逐位一致故无需清位图）；-1 = 取消。
// v3.7.2：GPU 瓦片描述（供双缓冲流水线使用）
struct GpuTile {
    int64_t xs, zA, tw, tz;
};

static int runGpuScanStage(const SearchOptions& opt,
                           const std::atomic<bool>& cancel,
                           Emitter& scanEm,
                           BitGrid& grid,
                           std::vector<std::pair<int, int>>& newChunks,
                           std::string& errOut) noexcept
{
    using clock = std::chrono::steady_clock;
    const auto t0 = clock::now();
    const auto elapsedMs = [&t0]() {
        return std::chrono::duration<double, std::milli>(clock::now() - t0).count();
    };

    const int64_t x0 = opt.minX, z0 = opt.minZ;
    const int64_t W = static_cast<int64_t>(opt.maxX) - opt.minX + 1;
    const int64_t H = static_cast<int64_t>(opt.maxZ) - opt.minZ + 1;
    const int64_t totalChunks = W * H;
    // v3.4.13：本函数读缓存的路径没有加锁语义（在 GPU 阶段里）⇒ 存在并发写者（构建模式）时
    //   改为不读缓存：collectNew 会退化成「整区未覆盖」⇒ 全量重算（结果正确，只是不用缓存捷径）。
    const bool useCache = (opt.version == Version::Bedrock) && opt.useCache &&
                          (opt.cache != nullptr) && (opt.cacheReadMu == nullptr);
    const bool collectNew = useCache && !opt.cache->covers(opt.region());

    // v3.12.1：缓存已经完整覆盖时不需要 GPU 重新判定；直接让经典 CPU 路径
    //   走「缓存位图填充」快路径（实测 4.19M 区块约 0.2 ms 扫描，而不是 GPU 重算）。
    if (useCache && !collectNew) {
        errOut.clear();
        return 0;
    }

    // v3.2.1：空指针/不可用显式保护——未注入或探测失败一律立即回退 CPU 路径
    // （GUI 默认关闭 GPU，仅在用户勾选且可用时注入；此处是最后一道防线）。
    if (!opt.gpuScan) {
        errOut = "GPU 未注入（gpuScan 为空）";
        return 0;
    }
    if (!opt.gpuScan->available()) {
        errOut = "GPU 不可用（available=false）";
        return 0;
    }
    if (totalChunks < gpuMinChunksEnv(opt.version)) {
        errOut = "区域过小（低于启用阈值）";
        return 0;
    }
    if (W > (1LL << 31) || H > (1LL << 31)) { // 与 validate 一致，双保险
        errOut = "区域过大";
        return 0;
    }

    GpuPhaseMark gpuPhase(opt.livePhase, 2);   // v3.10.3：GUI 显示「GPU 扫描」
    scanEm.report(0, totalChunks, 0.0, 0.0);
    // 瓦片预算：Java 判定极快可大瓦片；基岩（~5600 万/秒）收紧到 32MB，
    // 保证瓦片粒度取消响应 <1s（验收项）。
    int64_t tileCap = gpuTileBytesEnv();
    if (opt.version == Version::Bedrock && tileCap > (1 << 25)) tileCap = 1 << 25;
    const int64_t zRowsBase = std::max<int64_t>(1, tileCap / std::max<int64_t>(1, W));

    std::vector<uint8_t> bytes;
    std::vector<std::pair<int64_t, int64_t>> cis;      // 候选缓存区间（行级，绝对坐标）
    std::vector<std::pair<int64_t, int64_t>> cRuns;    // 行内合并后的缓存段（绝对坐标）
    int64_t chunksDone = 0;

    // 行内 x 游走（相对行坐标 rx = x - x0；位图字索引/移位必须以相对坐标计算）：
    // compute 段按 64 对齐寄存器累计写回（与 CPU Java 路径同构），cached 段逐块回读缓存。
    const auto integrateRowPiece = [&](int64_t absZ, int64_t xA, int64_t xB, const uint8_t* brow) {
        cRuns.clear();
        if (useCache) {
            cis.clear();
            for (const Rect& rc : opt.cache->ranges()) {
                if (absZ >= rc.minZ && absZ <= rc.maxZ) {
                    const int64_t a = std::max<int64_t>(xA, static_cast<int64_t>(rc.minX));
                    const int64_t b = std::min<int64_t>(xB, static_cast<int64_t>(rc.maxX) + 1);
                    if (a < b) cis.emplace_back(a, b);
                }
            }
            if (!cis.empty()) {
                std::sort(cis.begin(), cis.end());
                int64_t cA = cis.front().first, cB = cis.front().second;
                for (size_t i = 1; i < cis.size(); ++i) {
                    if (cis[i].first <= cB) {
                        cB = std::max(cB, cis[i].second);
                    } else {
                        cRuns.emplace_back(cA, cB);
                        cA = cis[i].first;
                        cB = cis[i].second;
                    }
                }
                cRuns.emplace_back(cA, cB);
            }
        }
        const int64_t relA = xA - x0;   // 片起点（相对行）
        const int64_t relB = xB - x0;   // 片终点（相对行，含）
        uint64_t* rowW = grid.rowBits(absZ);
        const int czz = static_cast<int>(absZ);
        int64_t rx = relA;
        size_t ci = 0;
        while (rx < relB) {
            // compute 段（直到下一个缓存段起点，相对换算）
            const int64_t cStartRel = (ci < cRuns.size()) ? (cRuns[ci].first - x0) : INT64_MAX;
            const int64_t runEnd = std::min<int64_t>(relB, cStartRel);
            if (rx < runEnd) {
                while (rx < runEnd) {
                    const int64_t xe = std::min<int64_t>(runEnd, (rx & ~63LL) + 64);
                    // v3.7.1：整段一次算完（SSE2 movemask），避免逐区块移位/或的串行依赖
                    const uint64_t acc =
                        packBytesToBits(brow + (rx - relA), xe - rx, static_cast<int>(rx & 63));
                    rx = xe;
                    if (acc) rowW[(xe - 1) >> 6] |= acc;
                }
            }
            if (rx >= relB) break;
            // cached 段（绝对坐标逐块回读缓存；x 值域受 validate 限制，int 安全）
            const int cEndI = static_cast<int>(std::min<int64_t>(xB, cRuns[ci].second));
            for (int ax = static_cast<int>(std::max<int64_t>(xA, cRuns[ci].first)); ax < cEndI; ++ax) {
                if (opt.cache->isSlime(ax, czz)) grid.setSlime(static_cast<int64_t>(ax), absZ);
            }
            rx = static_cast<int64_t>(cEndI) - x0;
            ++ci;
        }
        // collectNew 第二遍（仅在需要时）：compute 段逐块检查字节
        if (collectNew) {
            rx = relA;
            ci = 0;
            while (rx < relB) {
                const int64_t cStartRel = (ci < cRuns.size()) ? (cRuns[ci].first - x0) : INT64_MAX;
                const int64_t runEnd = std::min<int64_t>(relB, cStartRel);
                for (; rx < runEnd; ++rx) {
                    if (brow[rx - relA]) newChunks.emplace_back(static_cast<int>(x0 + rx), czz);
                }
                if (rx >= relB) break;
                rx = std::min<int64_t>(relB, cRuns[ci].second - x0);
                ++ci;
            }
        }
    };

    // v3.7.2：双缓冲流水线 —— GPU 扫下一块的同时，CPU 集成当前块（此前是「扫完再集成」纯串行）。
    //   只在「不限制 GPU 占用率、且不是 profiling」时启用，避免与占空比休眠/计时逻辑互相干扰。
    std::vector<GpuTile> tiles;
    {
        int64_t pipeCapBytes = 16LL << 20;   // 每缓冲上限（默认 16MB，实测最优）
        if (const char* e = std::getenv("SLIME_GPU_PIPE_BYTES")) {
            const long long v = std::atoll(e);
            if (v > 0) pipeCapBytes = v;
        }
        const int64_t pipeCap = std::min<int64_t>(tileCap, pipeCapBytes);
        int64_t zA = 0;
        while (zA < H) {
            const int64_t tz = std::min<int64_t>(H - zA, zRowsBase);
            const int64_t twFull = std::max<int64_t>(1, pipeCap / std::max<int64_t>(1, tz));
            for (int64_t xs = 0; xs < W; xs += twFull) {
                const int64_t tw = std::min<int64_t>(W - xs, twFull);
                tiles.push_back(GpuTile{ xs, zA, tw, tz });
            }
            zA += tz;
        }
    }
    // v3.7.3：Java 版优先走「打包字」扫描（回读量 1/8，且集成退化为按字 OR）。
    //   仅当瓦片在行内按 64 对齐、且宽度是 64 的整数倍时可用；否则该瓦片回退字节路径。
    // v3.7.4：Java / 基岩都走打包字内核；但**缓存路径必须走字节路径**，
    //   因为逐行集成里包含「缓存覆盖段回读」与 collectNew 的记账，打包字快路径会绕过它们。
    const bool wordsCapable = !useCache && !collectNew;
    const auto tileAlignOk = [&](const GpuTile& t) {
        return wordsCapable && (t.xs % 64 == 0) && (t.tw % 64 == 0);
    };
    // 把打包字瓦片按行 OR 进位图（瓦片起点按 64 对齐 ⇒ 字下标直接相加）
    const auto integrateTileWords = [&](const GpuTile& t, const std::vector<uint64_t>& w) {
        const int64_t wpr = (t.tw + 63) / 64;
        const int64_t dst0 = t.xs >> 6;
        for (int64_t rz = 0; rz < t.tz; ++rz) {
            const uint64_t* src = w.data() + rz * wpr;
            uint64_t* dst = grid.rowBits(z0 + t.zA + rz) + dst0;
#if defined(_M_X64) || defined(_M_IX86) || defined(__SSE2__)
            // v3.7.4：按 128 位批量 OR（位图是内存带宽瓶颈，标量写法每字一次读改写）
            int64_t k = 0;
            for (; k + 2 <= wpr; k += 2) {
                const __m128i a = _mm_loadu_si128(reinterpret_cast<const __m128i*>(src + k));
                const __m128i b = _mm_loadu_si128(reinterpret_cast<const __m128i*>(dst + k));
                _mm_storeu_si128(reinterpret_cast<__m128i*>(dst + k), _mm_or_si128(a, b));
            }
            for (; k < wpr; ++k) dst[k] |= src[k];
#else
            for (int64_t k = 0; k < wpr; ++k) dst[k] |= src[k];
#endif
        }
    };

    const bool pipe = (gpuLimitPercentEnv() >= 100) && !kScanProfileOn && tiles.size() > 1;
    if (pipe) {
        std::vector<uint8_t> buf[2];
        std::vector<uint64_t> wbuf[2];
        bool usedWords[2] = { false, false };
        std::thread th;
        bool thActive = false;
        bool thOk = true;
        bool thUsedWords = false;
        std::string thErr;
        const auto runScan = [&](const GpuTile& t, std::vector<uint8_t>& b, std::vector<uint64_t>& w,
                                 bool& usedWords, std::string& err) -> bool {
            usedWords = false;
            if (tileAlignOk(t)) {
                if (opt.gpuScan->scanRectWords(opt.version, opt.worldSeed, static_cast<int>(x0 + t.xs),
                                               static_cast<int>(z0 + t.zA), static_cast<int>(t.tw),
                                               static_cast<int>(t.tz), w, &err)) {
                    usedWords = true;
                    return true;
                }
            }
            return opt.gpuScan->scanRect(opt.version, opt.worldSeed, static_cast<int>(x0 + t.xs),
                                         static_cast<int>(z0 + t.zA), static_cast<int>(t.tw),
                                         static_cast<int>(t.tz), b, &err);
        };
        const auto integrateTile = [&](const GpuTile& t, const std::vector<uint8_t>& b,
                                       const std::vector<uint64_t>& w, bool usedWords) {
            if (usedWords) {
                integrateTileWords(t, w);
            } else {
                for (int64_t rz = 0; rz < t.tz; ++rz) {
                    integrateRowPiece(z0 + t.zA + rz, x0 + t.xs, x0 + t.xs + t.tw, b.data() + rz * t.tw);
                }
            }
            chunksDone += t.tw * t.tz;
            const double el = elapsedMs();
            const double speed = el > 0.0 ? (static_cast<double>(chunksDone) / el) * 1000.0 : 0.0;
            const double eta = speed > 1.0 ? (static_cast<double>(totalChunks - chunksDone) / speed) : 0.0;
            scanEm.report(chunksDone, totalChunks, speed, eta);
        };
        for (size_t i = 0; i < tiles.size(); ++i) {
            if (cancel.load(std::memory_order_relaxed)) {
                if (thActive) th.join();
                return -1;
            }
            const size_t cur = i & 1;
            if (i == 0) {
                if (!runScan(tiles[i], buf[cur], wbuf[cur], usedWords[cur], thErr)) {
                    errOut = thErr.empty() ? "GPU 扫描失败" : thErr;
                    return 0;
                }
            } else if (thActive) {
                th.join();
                thActive = false;
                usedWords[cur] = thUsedWords;
                if (!thOk) {
                    errOut = thErr.empty() ? "GPU 扫描失败" : thErr;
                    return 0;
                }
            }
            if (i + 1 < tiles.size()) {
                const size_t nxt = (i + 1) & 1;
                thOk = true;
                thErr.clear();
                th = std::thread([&, nxt]() {
                    thOk = runScan(tiles[i + 1], buf[nxt], wbuf[nxt], thUsedWords, thErr);
                });
                thActive = true;
            }
            integrateTile(tiles[i], buf[cur], wbuf[cur], usedWords[cur]);
        }
        if (thActive) th.join();
        return 1;
    }

    // 顺序路径（限制占用率 / profiling / 单瓦片时使用）
    std::vector<uint64_t> seqWords;
    for (const GpuTile& t : tiles) {
        if (cancel.load(std::memory_order_relaxed)) return -1;
        std::string err;
        const auto tScanStart = std::chrono::steady_clock::now();
        const unsigned long long tScan0 = kScanProfileOn ? __rdtsc() : 0ULL;
        bool usedWords = false;
        if (tileAlignOk(t)) {
            if (opt.gpuScan->scanRectWords(opt.version, opt.worldSeed, static_cast<int>(x0 + t.xs),
                                           static_cast<int>(z0 + t.zA), static_cast<int>(t.tw),
                                           static_cast<int>(t.tz), seqWords, &err)) {
                usedWords = true;
            }
        }
        if (!usedWords) {
            const bool ok = opt.gpuScan->scanRect(
                opt.version, opt.worldSeed, static_cast<int>(x0 + t.xs), static_cast<int>(z0 + t.zA),
                static_cast<int>(t.tw), static_cast<int>(t.tz), bytes, &err);
            if (!ok) {
                errOut = err.empty() ? "GPU 扫描失败" : err;
                return 0;
            }
        }
        const unsigned long long tScan1 = kScanProfileOn ? __rdtsc() : 0ULL;
        if (usedWords) {
            integrateTileWords(t, seqWords);
        } else {
            for (int64_t rz = 0; rz < t.tz; ++rz) {
                integrateRowPiece(z0 + t.zA + rz, x0 + t.xs, x0 + t.xs + t.tw, bytes.data() + rz * t.tw);
            }
        }
        // v3.7.1：按 GPU 占用率上限补一段休眠（上限 100% 时完全不睡）
        {
            const int pct = gpuLimitPercentEnv();
            if (pct < 100) {
                const auto tEnd = std::chrono::steady_clock::now();
                const double busyMs = std::chrono::duration<double, std::milli>(tEnd - tScanStart).count();
                const double idleMs = busyMs * (100.0 / pct - 1.0);
                if (idleMs > 0.2) {
                    std::this_thread::sleep_for(std::chrono::microseconds(
                        static_cast<long long>(idleMs * 1000.0)));
                }
            }
        }
        if (kScanProfileOn) {
            g_profGpuScanCycles.fetch_add(static_cast<int64_t>(tScan1 - tScan0), std::memory_order_relaxed);
            g_profGpuIntegrateCycles.fetch_add(static_cast<int64_t>(__rdtsc() - tScan1),
                                               std::memory_order_relaxed);
            g_profGpuChunks.fetch_add(t.tw * t.tz, std::memory_order_relaxed);
        }
        chunksDone += t.tw * t.tz;
        const double el = elapsedMs();
        const double speed = el > 0.0 ? (static_cast<double>(chunksDone) / el) * 1000.0 : 0.0;
        const double eta = speed > 1.0 ? (static_cast<double>(totalChunks - chunksDone) / speed) : 0.0;
        scanEm.report(chunksDone, totalChunks, speed, eta);
    }
    return 1;
}

// 分块路径的匹配锚点行上限（绝对区块 z 坐标；仅分块线程内临时设置，默认不限制）
thread_local int64_t g_anchorMaxAZ = INT64_MAX;

} // namespace

// ============================ 公开判定 ============================

bool SlimeEngine::isSlimeChunkJava(int64_t worldSeed, int chunkX, int chunkZ) noexcept
{
    return isSlimeJavaFast(worldSeed, static_cast<int32_t>(chunkX), static_cast<int32_t>(chunkZ));
}

bool SlimeEngine::isSlimeChunkBedrockMT(int chunkX, int chunkZ) noexcept
{
    return isSlimeBedrockFast(static_cast<int32_t>(chunkX), static_cast<int32_t>(chunkZ));
}

bool SlimeEngine::isSlimeChunk(Version v, int64_t worldSeed, int chunkX, int chunkZ) noexcept
{
    return (v == Version::Java) ? isSlimeChunkJava(worldSeed, chunkX, chunkZ)
                                : isSlimeChunkBedrockMT(chunkX, chunkZ);
}

// v3.5.15：整行批量判定（Java）。行基座只算一次；x 尾项仍逐个算（两次 32 位乘法），
// 但循环很紧、没有 std::function 间接调用，编译器能展开/向量化。
void SlimeEngine::fillSlimeRowJava(int64_t worldSeed, int cz, int cx0, int n, uint8_t* out) noexcept
{
    if (!out || n <= 0) return;
    const uint64_t rowBase = javaRowBase(worldSeed, static_cast<int32_t>(cz));
    int i = 0;
    for (; i + 4 <= n; i += 4) {
        out[i + 0] = javaSlimeFromTail(rowBase, javaXTail(static_cast<int32_t>(cx0 + i + 0))) ? 1 : 0;
        out[i + 1] = javaSlimeFromTail(rowBase, javaXTail(static_cast<int32_t>(cx0 + i + 1))) ? 1 : 0;
        out[i + 2] = javaSlimeFromTail(rowBase, javaXTail(static_cast<int32_t>(cx0 + i + 2))) ? 1 : 0;
        out[i + 3] = javaSlimeFromTail(rowBase, javaXTail(static_cast<int32_t>(cx0 + i + 3))) ? 1 : 0;
    }
    for (; i < n; ++i)
        out[i] = javaSlimeFromTail(rowBase, javaXTail(static_cast<int32_t>(cx0 + i))) ? 1 : 0;
}

// v3.10.10.1：基岩版整行批量判定 —— 与正常扫描同一 AVX2 批量内核（kBedrockBatch=64/@32 lane），
//   逐区块调用 isSlimeChunk 会慢 1~2 个数量级（best 模式曾因此 4M 区块耗时近 1 秒）。
void SlimeEngine::fillSlimeRowBedrock(int cz, int cx0, int n, uint8_t* out) noexcept
{
    int done = 0;
    while (done < n) {
        const int m = (std::min)(n - done, 4096);
        fastmt::bedrockSlimeScan(cx0 + done, cz, m, out + done, false);
        done += m;
    }
}

bool SlimeEngine::densityWindowMeets(int slimeCount, int gridSize, double minDensity, const float* /*biomeWeight*/) noexcept
{
    if (gridSize <= 0) return false;
    if (minDensity <= 0.0) return true; // 阈值为 0：所有窗口均满足
    return static_cast<double>(slimeCount) / static_cast<double>(static_cast<int64_t>(gridSize) * gridSize) >= minDensity;
}

// ============================ 校验 ============================

std::string SlimeEngine::validate(const SearchOptions& o) noexcept
{
    // v3.3.9（任务书 1.1）：缓存构建模式沿用既有缓存语义（基岩版位图），Java 版不写缓存
    if (o.buildMode && o.version != Version::Bedrock) {
        return "缓存构建模式目前仅支持基岩版（缓存位图与既有缓存文件一致，Java 版不写缓存）。";
    }
    constexpr int kCoordLimit = 2147483547; // 2^31-100：图案宽度留余量
    const auto coordOk = [](int v) { return v >= -kCoordLimit && v <= kCoordLimit; };
    if (!coordOk(o.minX) || !coordOk(o.maxX) || !coordOk(o.minZ) || !coordOk(o.maxZ)) {
        return "区块坐标超出支持范围。";
    }
    if (o.minX > o.maxX || o.minZ > o.maxZ) {
        return "搜索范围无效：最小值不能大于最大值。";
    }
    const int64_t w = static_cast<int64_t>(o.maxX) - o.minX + 1;
    const int64_t h = static_cast<int64_t>(o.maxZ) - o.minZ + 1;
    // v3.1.1：移除面积/位图硬限制（整图搜索由分块路径处理），仅保留 int64 溢出护栏
    if (w > (1LL << 31) || h > (1LL << 31)) {
        return "搜索范围过大（单边超过 21 亿区块）。";
    }
    if (w * h > (1LL << 52)) {
        return "搜索范围过大（区块总数超出内部计数上限）。";
    }
    if (o.targetCount <= 0) {
        return "目标数量必须为正整数。";
    }
    if (o.mode == Mode::Density) {
        if (o.gridSize <= 0) return "网格大小必须为正整数。";
        if (o.minDensity < 0.0 || o.minDensity > 1.0) return "密度阈值必须在 0~1 之间。";
        if (o.gridSize > 512) return "网格大小不能超过 512。";
    } else if (o.mode == Mode::Strict) {
        if (o.pattern.empty()) return "图案为空，请先绘制图案。";
        if (o.pattern.rows() > 64 || o.pattern.cols() > 64) return "图案过大（行列不能超过 64）。";
    } else {
        return "未知搜索模式。";
    }
    return std::string();
}

// ============================ 主流程 ============================

void SlimeEngine::run(const SearchOptions& optIn,
                      const std::atomic<bool>& cancel,
                      const ProgressFn& progress,
                      SearchOutcome& out) const noexcept
{
    // v3.9.5（bug）：严格图案模式不支持「分层/估算」算法（它们只在密度模式有意义）。
    //   以前的做法是把这两种算法当成密度语义跑 —— 结果是**静默给出错误结果**（实测严格模式
    //   结果与精确不一致）。现在改为：严格模式下强制按「精确」算法执行，识别参数照旧。
    SearchOptions normalized = optIn;
    if (normalized.mode == Mode::Strict &&
        (normalized.algorithm == Algorithm::Layered || normalized.algorithm == Algorithm::Sample)) {
        normalized.algorithm = Algorithm::Exact;
        normalized.layerRatio = 1.0;
    }
    const SearchOptions& opt = normalized;

    // v3.3.7.2（任务书步骤 2 / P1-D）：经典路径也给出 cpu.sample / search.summary，
    // 使「慢搜索归因」在两种路径下都可用（分块路径的块级事件见 runChunked）。
    slime::perf::CpuSampler sampler;
    double cpu = 0.0, ws0 = 0.0;
    sampler.sample(&cpu, &ws0); // 建立基线（首次调用不产生数值）
    // ---- v3.8.6（流程优化）：严格图案的 GPU 流式匹配（顶层，整区一次调用）----
    //   仅当显式开启（SLIME_GPU_PATTERN=1）、非缓存/非构建、区域够大时启用；任何失败回退 CPU。
    {
        const auto tPat0 = std::chrono::steady_clock::now();
        const auto patMs = [&tPat0]() {
            return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - tPat0).count();
        };
        double tVariantMs = 0.0, tMatchMs = 0.0, tDedupeMs = 0.0;
        const bool strict0 = (opt.mode == Mode::Strict) && opt.algorithm != Algorithm::Layered &&
                             opt.algorithm != Algorithm::Sample;
        const int64_t x0p = opt.minX, z0p = opt.minZ;
        const int64_t Wp = static_cast<int64_t>(opt.maxX) - opt.minX + 1;
        const int64_t Hp = static_cast<int64_t>(opt.maxZ) - opt.minZ + 1;
        const int64_t totalChunksP = Wp * Hp;
        if (const char* patGpu = std::getenv("SLIME_GPU_PATTERN")) {
            // v3.10.12.3：仅单变体（方向锁定）走 GPU 图案流式 —— 旋转模式实测 1043 ms(CPU) vs
            //   2260 ms(GPU)：4 个变体 × 流式调用 + 跨变体去重反而更慢，直接回退 CPU。
            if (*patGpu == '1' && strict0 && opt.directionLocked && !opt.useCache && !opt.buildMode &&
                opt.gpuScan && opt.gpuScan->available() &&
                totalChunksP >= (std::min)(gpuMinChunksEnv(opt.version), int64_t{ 8 } << 20)) {
                std::vector<VariantMasks> gpuVariants;
                gpuVariants.push_back(makeVariant(opt.pattern));
                if (!opt.directionLocked) {
                    Pattern rotated = opt.pattern;
                    for (int i = 0; i < 3; ++i) {
                        rotated = rotated.rotated();
                        gpuVariants.push_back(makeVariant(rotated));
                    }
                }
                tVariantMs = patMs();
                bool usable = !gpuVariants.empty();
                for (const VariantMasks& vm : gpuVariants) {
                    if (vm.rows < 1 || vm.rows > 32 || vm.cols < 1 || vm.cols > 32) usable = false;
                }
                if (usable) {
                    // v3.10.12.2：图案 GPU 流式命中上限提高到 400 万（与密度流式一致）——
                    //   100k² 2x2 图案命中约 100 万，旧上限 20 万会直接回退 CPU（3000 ms）。
                    // v3.10.12.3：固定 400 万容量（100k² 图案约 103 万命中；旧公式对 target=50 只有 100 万，刚好溢出回退）
                    const int64_t cap = 4000000;
                    TopNHeap heap(opt.targetCount);
                    // v3.8.7：单变体（方向锁定）时不会有跨变体重复，直接入堆即可 ——
                    //   实测 std::map 去重占了钩子时间的约 40%（6924 条 ≈ 2.9 ms）。
                    const bool singleVariant = (gpuVariants.size() == 1);
                    std::map<std::array<int, 4>, Scored> seenGpu;
                    int64_t foundGpu = 0;
                    int64_t variantBaseGpu = 0;
                    bool allOk = true;
                    bool anyVariant = false;
                    for (const VariantMasks& vm : gpuVariants) {
                        const int64_t axMaxV = x0p + Wp - vm.cols;
                        const int64_t azMaxV = z0p + Hp - vm.rows;
                        const int64_t nAxV = axMaxV - x0p + 1;
                        const int64_t nAzV = azMaxV - z0p + 1;
                        if (vm.allIgnore || axMaxV < x0p || azMaxV < z0p || cap <= 0) {
                            variantBaseGpu += (std::max)(int64_t{ 0 }, nAxV * nAzV);
                            continue;
                        }
                        anyVariant = true;
                        std::vector<uint32_t> en(static_cast<size_t>(vm.rows)), gr(static_cast<size_t>(vm.rows));
                        for (int r = 0; r < vm.rows; ++r) {
                            en[static_cast<size_t>(r)] = static_cast<uint32_t>(vm.enf[static_cast<size_t>(r)]);
                            gr[static_cast<size_t>(r)] = static_cast<uint32_t>(vm.grn[static_cast<size_t>(r)]);
                        }
                        slime::PatternMatchParams pp;
                        pp.version = opt.version;
                        pp.worldSeed = opt.worldSeed;
                        pp.axMin = x0p;
                        pp.axMax = axMaxV;
                        pp.azMin = z0p;
                        pp.azMax = azMaxV;
                        pp.rows = vm.rows;
                        pp.cols = vm.cols;
                        pp.enableMasks = en.data();
                        pp.greenMasks = gr.data();
                        pp.maxResults = cap;
                        std::vector<MatchResult> gpuRes;
                        std::string gerr;
                        const double mBefore = patMs();
                        const bool mOk = opt.gpuScan->matchPattern(pp, cancel, gpuRes, &gerr);
                        tMatchMs += patMs() - mBefore;
                        if (!mOk) {
                            allOk = false;
                            if (std::getenv("SLIME_DEBUG")) {
                                std::fprintf(stderr, "[engine] GPU 图案流式不可用，回退 CPU：%s\n",
                                             gerr.c_str());
                            }
                            break;
                        }
                        for (const MatchResult& m : gpuRes) {
                            const int64_t off2x = 2 * static_cast<int64_t>(m.minX) + (vm.cols - 1) -
                                                  2 * static_cast<int64_t>(opt.startX);
                            const int64_t off2z = 2 * static_cast<int64_t>(m.minZ) + (vm.rows - 1) -
                                                  2 * static_cast<int64_t>(opt.startZ);
                            const int64_t d2 = off2x * off2x + off2z * off2z;
                            const double dist = 0.5 * std::sqrt(static_cast<double>(d2));
                            const int64_t rank =
                                variantBaseGpu + (m.minX - pp.axMin) * nAzV + (m.minZ - pp.azMin);
                            if (singleVariant) {
                                ++foundGpu;   // 命中总数必须完整统计（剪枝只影响是否入堆）
                                if (heap.full() && d2 > heap.worstD2()) continue;
                                heap.push(Scored{ m, dist, rank, d2 });
                                continue;
                            }
                            const std::array<int, 4> key{ m.minX, m.maxX, m.minZ, m.maxZ };
                            if (seenGpu.find(key) != seenGpu.end()) continue;
                            seenGpu.emplace(key, Scored{ m, dist, rank, d2 });
                        }
                        variantBaseGpu += nAxV * nAzV;
                    }
                    tDedupeMs = patMs() - tMatchMs - tVariantMs;
                    if (allOk && anyVariant) {
                        out.gpuMatchUsed = true;
                        if (cancel.load(std::memory_order_relaxed)) {
                            out.cancelled = true;
                            out.stats.totalMs = patMs();
                            return;
                        }
                        if (!singleVariant) {
                            for (const auto& kv : seenGpu) {
                                ++foundGpu;
                                if (heap.full() && kv.second.d2 > heap.worstD2()) continue;
                                heap.push(kv.second);
                            }
                        }
                        for (const Scored& s : heap.sorted()) out.results.push_back(s.m);
                        out.totalFound = foundGpu;
                        out.stoppedEarly = false;
                        out.stats.scanMs = patMs();
                        out.stats.threadsUsed = opt.threads;
                        out.stats.scannedChunks = totalChunksP;
                        out.stats.matchMs = patMs();
                        out.stats.totalMs = patMs();
                        if (progress) progress(kMatchFrac1, false, 0.0, 0.0);
                        out.ok = true;
                        if (std::getenv("SLIME_DEBUG")) {
                            std::fprintf(stderr,
                                         "[engine] GPU 图案流式完成（顶层，%zu 变体）：命中 %lld，显示 %zu | "
                                         "变体构造 %.2f ms + 匹配 %.2f ms + 去重入堆 %.2f ms + 其他 %.2f ms | "
                                         "总计 %.2f ms\n",
                                         gpuVariants.size(), static_cast<long long>(foundGpu),
                                         out.results.size(), tVariantMs, tMatchMs, tDedupeMs,
                                         patMs() - tVariantMs - tMatchMs - tDedupeMs, patMs());
                        }
                        return;
                    }
                }
            }
        }
    }

    runImpl(opt, cancel, progress, out, true);
    // 分块路径已在自己的收尾处写 search.summary（块级事件在那里），这里只在经典路径补一份
    if (out.chunkedPath) return;
    double cpuEnd = 0.0, cpuAllEnd = 0.0, wsEnd = 0.0;
    const bool sampled = sampler.sample(&cpuEnd, &cpuAllEnd, &wsEnd);
    const double peakMB = slime::IncidentLog::peakWorkingSetMB();
    if (slime::IncidentLog::logDir().empty()) return; // 未初始化日志则不做任何写入
    if (sampled) {
        slime::perf::cpuSample(static_cast<int64_t>(out.stats.totalMs), cpuEnd, wsEnd, peakMB);
    }
    slime::perf::memPeak(static_cast<int64_t>(out.stats.totalMs), peakMB, wsEnd);
    char buf[512];
    std::snprintf(buf, sizeof(buf),
                  "classic chunks=1 pruned=0 scanned=%lld/%lld results=%zu total=%.0fms scan=%.0fms "
                  "match=%.0fms fill=%.0fms slideWindow=%.0fms rowsPerChunk=%lld cacheHitPoints=%lld cpuEnd=%.1f%% cpuAllEnd=%.0f%% cores=%d wsPeak=%.0fMB "
                  "conc=%d cacheWrite=0",
                  static_cast<long long>(out.stats.scannedChunks),
                  static_cast<long long>(opt.liveTotal ? opt.liveTotal->load(std::memory_order_relaxed) : 0),
                  out.results.size(), out.stats.totalMs, out.stats.scanMs, out.stats.matchMs,
                  out.stats.fillMs, out.stats.slideWindowMs,
                  static_cast<long long>(opt.maxZ - opt.minZ + 1), out.cacheHitPoints,
                  sampled ? cpuEnd : 0.0, sampled ? cpuAllEnd : 0.0, slime::perf::logicalCores(), peakMB,
                  out.stats.threadsUsed);
    slime::perf::searchSummary(buf);
}

// ===================== v3.9.12：最优候选模式（CPU 经典路径） =====================
//   语义：忽略 minDensity，按「窗口内史莱姆区块数」从高到低返回 targetCount 个窗口；
//         并列时按距起点距离升序、再按光栅序（与 TopN 的 dist/rank 语义一致，可复现）。
//   说明：v1 仅在经典路径实现（大区域请缩小范围）；结果里 density = count / g² 供展示。
//   与「提前停止 / 智能范围 / GPU 流式 / 缓存」正交：本模式不使用它们。
// v3.10.10.1：最小密度 = 0 时所有窗口都满足阈值 ⇒ 引擎本应返回「离起点最近的 N 个窗口」。
//   旧实现仍完整扫描+匹配（20000² 约 950 ms，GPU 还会因命中上限回退 CPU）。
//   这里直接解析地取最近的 N 个窗口，再只为这 N 个窗口统计密度（O(N·g²) 次判定），
//   结果与完整路径逐条一致（同 d2 → 同 rank 平局序）。
void SlimeEngine::runZeroThresholdFast(const SearchOptions& opt, const std::atomic<bool>& cancel,
                                       const ProgressFn& progress, SearchOutcome& out) const noexcept
{
    using clock = std::chrono::steady_clock;
    const auto t0 = clock::now();
    const auto elapsedMs = [&t0]() {
        return std::chrono::duration<double, std::milli>(clock::now() - t0).count();
    };
    const int64_t x0 = opt.minX, z0 = opt.minZ;
    const int64_t W = static_cast<int64_t>(opt.maxX) - opt.minX + 1;
    const int64_t H = static_cast<int64_t>(opt.maxZ) - opt.minZ + 1;
    const int g = (std::max)(1, opt.gridSize);
    const int64_t totalChunks = W * H;
    out.stats.scannedChunks = totalChunks;
    out.stats.threadsUsed = 1;
    if (W < g || H < g) {
        out.ok = true;
        out.stats.totalMs = elapsedMs();
        if (progress) progress(1.0, false, 0.0, 0.0);
        return;
    }
    const int64_t axMin = x0, axMax = x0 + W - g;
    const int64_t azMin = z0, azMax = z0 + H - g;
    const int64_t nAx = axMax - axMin + 1, nAz = azMax - azMin + 1;
    const int64_t total = nAx * nAz;
    const int64_t N = std::clamp<int64_t>(opt.targetCount, 1, total);

    struct Cand {
        int64_t ax = 0, az = 0, d2 = 0, rank = 0;
    };
    std::vector<Cand> picked;
    const auto consider = [&](int64_t ax, int64_t az) {
        if (ax < axMin || ax > axMax || az < azMin || az > azMax) return;
        Cand c;
        c.ax = ax;
        c.az = az;
        // 与引擎 recordMatchAt 完全同口径：offsets = ax*2 + (g-1) - 2*start（偶数 g 保留 .5）
        const int64_t dx = 2 * ax + (g - 1) - 2 * static_cast<int64_t>(opt.startX);
        const int64_t dz = 2 * az + (g - 1) - 2 * static_cast<int64_t>(opt.startZ);
        c.d2 = dx * dx + dz * dz;
        c.rank = (ax - axMin) * nAz + (az - azMin);   // 与经典路径 rank 口径一致
        picked.push_back(c);
    };
    const auto cmp = [](const Cand& a, const Cand& b) {
        if (a.d2 != b.d2) return a.d2 < b.d2;
        return a.rank < b.rank;
    };

    if (total <= N || total <= 1000000) {
        picked.reserve(static_cast<size_t>(total));
        for (int64_t az = azMin; az <= azMax; ++az)
            for (int64_t ax = axMin; ax <= axMax; ++ax) consider(ax, az);
    } else {
        // 以「离起点最近的合法锚点中心」为原点做方形扩张，直到覆盖的合法锚点 ≥ N。
        // 证明要点：对处在区域外的起点，方形外沿向远离起点方向单增 ⇒ 方形外的锚点 d2 必然更大。
        const int64_t half = (g - 1) / 2;
        const int64_t c0ax = std::clamp<int64_t>(opt.startX - half, axMin, axMax);
        const int64_t c0az = std::clamp<int64_t>(opt.startZ - half, azMin, azMax);
        int64_t M = static_cast<int64_t>(std::ceil(std::sqrt(static_cast<double>(N)))) + 2;
        const int64_t maxM = (std::max)(axMax - axMin, azMax - azMin) + 2;
        for (;;) {
            picked.clear();
            for (int64_t i = -M; i <= M; ++i) {
                for (int64_t j = -M; j <= M; ++j) consider(c0ax + i, c0az + j);
            }
            if (static_cast<int64_t>(picked.size()) >= N || M >= maxM) break;
            M = (std::min)(maxM, M * 2);
        }
    }
    if (cancel.load(std::memory_order_relaxed)) {
        out.cancelled = true;
        out.stats.totalMs = elapsedMs();
        return;
    }
    if (static_cast<int64_t>(picked.size()) > N) {
        std::nth_element(picked.begin(), picked.begin() + static_cast<std::ptrdiff_t>(N), picked.end(), cmp);
        picked.resize(static_cast<size_t>(N));
    }
    std::sort(picked.begin(), picked.end(), cmp);

    // 只为选中的窗口统计密度：按 z 行批量判定整行（Java / 基岩都有 O(n) 行内核）。
    int64_t bx0 = INT64_MAX, bx1 = INT64_MIN, bz0 = INT64_MAX, bz1 = INT64_MIN;
    for (const Cand& c : picked) {
        bx0 = (std::min)(bx0, c.ax);
        bx1 = (std::max)(bx1, c.ax + g - 1);
        bz0 = (std::min)(bz0, c.az);
        bz1 = (std::max)(bz1, c.az + g - 1);
    }
    if (picked.empty()) {
        out.ok = true;
        out.stats.totalMs = elapsedMs();
        if (progress) progress(1.0, false, 0.0, 0.0);
        return;
    }
    const int64_t bw = bx1 - bx0 + 1;
    const int64_t bh = bz1 - bz0 + 1;
    std::vector<std::vector<uint8_t>> rows(static_cast<size_t>(bh));
    for (int64_t r = 0; r < bh; ++r) {
        rows[static_cast<size_t>(r)].resize(static_cast<size_t>(bw));
        const int64_t z = bz0 + r;
        if (opt.version == Version::Bedrock) {
            fillSlimeRowBedrock(static_cast<int>(z), static_cast<int>(bx0), static_cast<int>(bw),
                                rows[static_cast<size_t>(r)].data());
        } else {
            fillSlimeRowJava(opt.worldSeed, static_cast<int>(z), static_cast<int>(bx0),
                             static_cast<int>(bw), rows[static_cast<size_t>(r)].data());
        }
        if ((r & 63) == 0 && cancel.load(std::memory_order_relaxed)) break;
    }
    out.results.reserve(picked.size());
    for (const Cand& c : picked) {
        int count = 0;
        for (int dz = 0; dz < g; ++dz) {
            const uint8_t* row = rows[static_cast<size_t>(c.az - bz0 + dz)].data();
            const int64_t off = c.ax - bx0;
            for (int dx = 0; dx < g; ++dx)
                if (row[off + dx]) ++count;
        }
        MatchResult m;
        m.minX = c.ax;
        m.maxX = c.ax + g - 1;
        m.minZ = c.az;
        m.maxZ = c.az + g - 1;
        m.density = static_cast<float>(count) / static_cast<float>(g * g);
        out.results.push_back(m);
    }
    out.totalFound = total;
    out.ok = true;
    out.stoppedEarly = false;
    out.stats.windowsEvaluated = total;
    out.stats.totalMs = elapsedMs();
    if (progress) progress(1.0, false, 0.0, 0.0);
}

void SlimeEngine::runBestDensity(const SearchOptions& opt, const std::atomic<bool>& cancel,
                                 const ProgressFn& progress, SearchOutcome& out) const noexcept
{
    using clock = std::chrono::steady_clock;
    const auto t0 = clock::now();
    const auto elapsedMs = [&t0]() {
        return std::chrono::duration<double, std::milli>(clock::now() - t0).count();
    };
    const int64_t x0 = opt.minX, z0 = opt.minZ;
    const int64_t W = static_cast<int64_t>(opt.maxX) - opt.minX + 1;
    const int64_t H = static_cast<int64_t>(opt.maxZ) - opt.minZ + 1;
    const int g = (std::max)(1, opt.gridSize);
    if (W < g || H < g) {
        out.ok = false;
        out.error = "搜索范围小于窗口尺寸。";
        out.stats.totalMs = elapsedMs();
        return;
    }
    BitGrid grid;
    if (!grid.init(x0, z0, W, H)) {
        out.ok = false;
        out.error = "内存不足，无法分配区块位图，请缩小范围。";
        out.stats.totalMs = elapsedMs();
        return;
    }

    // 填充位图：按行并行（每行写自己的位图范围，互不重叠）；判定用引擎统一的 isSlimeChunk。
    const int64_t totalChunks = W * H;
    int nthreadsBest = opt.threads;
#ifdef _OPENMP
    if (nthreadsBest <= 0) nthreadsBest = omp_get_num_procs();
    nthreadsBest = std::clamp(nthreadsBest, 1, 1024);
    omp_set_num_threads(nthreadsBest);
#else
    nthreadsBest = 1;
#endif
    std::atomic<bool> fillCancel{ false };
#ifdef _OPENMP
#pragma omp parallel for schedule(dynamic, 8)
#endif
    for (int64_t r = 0; r < H; ++r) {
        if (cancel.load(std::memory_order_relaxed) || fillCancel.load(std::memory_order_relaxed)) continue;
        const int64_t z = z0 + r;
        const int64_t wprRow = grid.wordsPerRow();
        uint64_t* rowBits = grid.rowBits(z);
        if (opt.version == Version::Java) {
            // v3.10.12.6：与正常扫描共用 fastjava 的 64 区块位掩码内核，直接把结果写成位图字。
            //   旧路径（fillSlimeRowJava + 逐字节打包）20k² 填充约 245 ms，是「最优 N 个」的最大头。
            if (kUseFastJava && kUseScanFastPath) {
                const uint64_t rowBase = javaRowBase(opt.worldSeed, static_cast<int32_t>(z));
                for (int64_t w = 0; w < wprRow; ++w) {
                    const int64_t bit = w * 64;
                    const int64_t n = (std::min)(int64_t{ 64 }, W - bit);
                    uint64_t mask = fastjava::slimeMask64(rowBase, static_cast<int32_t>(x0 + bit));
                    if (n < 64) mask &= ((1ULL << n) - 1ULL);
                    rowBits[w] = mask;
                }
                continue;
            }
            // 回退路径（SLIME_FASTJAVA=0 / 无 AVX2 环境）：整行批量内核 + 逐字节打包
            static thread_local std::vector<uint8_t> rowBytes;
            if (static_cast<int64_t>(rowBytes.size()) < W) rowBytes.resize(static_cast<size_t>(W));
            fillSlimeRowJava(opt.worldSeed, static_cast<int>(z), static_cast<int>(x0), static_cast<int>(W),
                             rowBytes.data());
            if (wprRow < W) {
                for (int64_t w = 0; w < wprRow; ++w) {
                    uint64_t acc = 0;
                    const int64_t base = w * 64;
                    for (int b = 0; b < 64 && base + b < W; ++b) {
                        if (rowBytes[static_cast<size_t>(base + b)]) acc |= (1ULL << b);
                    }
                    rowBits[w] = acc;
                }
            } else {
                int64_t bit = 0;
                for (int64_t w = 0; w < wprRow; ++w) {
                    uint64_t acc = 0;
                    for (int b = 0; b < 64 && bit < W; ++b, ++bit) {
                        if (rowBytes[static_cast<size_t>(bit)]) acc |= (1ULL << b);
                    }
                    rowBits[w] = acc;
                }
            }
        } else {
            // v3.10.10.1：基岩版改用 AVX2 批量 MT19937 行内核（此前逐区块调用慢 1~2 个数量级）
            static thread_local std::vector<uint8_t> bedRow;
            if (static_cast<int64_t>(bedRow.size()) < W) bedRow.resize(static_cast<size_t>(W));
            fillSlimeRowBedrock(static_cast<int>(z), static_cast<int>(x0), static_cast<int>(W), bedRow.data());
            const int64_t wprB = grid.wordsPerRow();
            for (int64_t w = 0; w < wprB; ++w) {
                uint64_t acc = 0;
                const int64_t base = w * 64;
                for (int b = 0; b < 64 && base + b < W; ++b) {
                    if (bedRow[static_cast<size_t>(base + b)]) acc |= (1ULL << b);
                }
                rowBits[w] = acc;
            }
        }
    }
    if (cancel.load(std::memory_order_relaxed)) {
        out.cancelled = true;
        out.stats.totalMs = elapsedMs();
        return;
    }
    // v3.10.3：把本块已扫描区块计入实时计数（分块最优候选模式的统计/进度可见）
    if (opt.liveScanned) opt.liveScanned->fetch_add(totalChunks, std::memory_order_relaxed);
    if (progress) progress(0.5, true, 0.0, 0.0);
    const double fillMs = elapsedMs();

    struct BestCand {
        int count = 0;
        int64_t d2 = 0;
        int64_t rank = 0;
        int64_t x = 0;
        int64_t z = 0;
    };
    const auto better = [](const BestCand& a, const BestCand& b) {
        if (a.count != b.count) return a.count > b.count;      // 计数高者优先
        if (a.d2 != b.d2) return a.d2 < b.d2;                  // 并列 → 距起点近者优先
        return a.rank < b.rank;                                // 再并列 → 光栅序
    };
    const int64_t N = (std::max)(int64_t{ 1 }, opt.targetCount);
    const int64_t anchorRows = H - g + 1;
    const int64_t anchorCols = W - g + 1;
    const int64_t half = (g - 1) / 2;
    std::atomic<int64_t> evaluated{ 0 };
    std::vector<std::vector<BestCand>> threadWinners(static_cast<size_t>(nthreadsBest));

    // v3.10.12.7：按 g 选择列和类型 —— g ≤ 180 时列和 ≤ g（窗口计数 ≤ g² ≤ 32761）可用 int16，
    //   把「垂直和更新 + 窗口计数」的内存流量减半（20k² 选择阶段约 214 ms，已经贴近内存带宽下限）。
    const auto runSelect = [&](auto* typeTag, int tid) {
        using VT = std::remove_pointer_t<decltype(typeTag)>;
        const int nt = (std::max)(1, static_cast<int>(threadWinners.size()));
        std::priority_queue<BestCand, std::vector<BestCand>, decltype(better)> heap(better);
        std::vector<VT> vertSum(static_cast<size_t>(W), 0);
        std::vector<VT> blk(2048);   // 分块计数缓冲（每线程复用）
        const int64_t r0 = anchorRows * tid / nt;
        const int64_t r1 = anchorRows * (tid + 1) / nt;
        int64_t localEval = 0;
        if (r1 > r0) {
            // 初始化本段第一窗口的 g 行列和
            for (int rr = 0; rr < g; ++rr) {
                const uint64_t* row = grid.rowBits(z0 + r0 + rr);
                const int64_t wprI = grid.wordsPerRow();
                int64_t x = 0;
                for (int64_t w = 0; w < wprI; ++w) {
                    const uint64_t v = row[w];
                    const int64_t cnt = (std::min)(int64_t{ 64 }, W - x);
                    for (int64_t b = 0; b < cnt; ++b, ++x) {
                        if ((v >> b) & 1ULL) ++vertSum[static_cast<size_t>(x)];
                    }
                }
            }
            for (int64_t az = r0; az < r1; ++az) {
                if (cancel.load(std::memory_order_relaxed)) break;
                int win = 0;
                for (int k = 0; k < g; ++k) win += static_cast<int>(vertSum[static_cast<size_t>(k)]);
                constexpr int64_t kBlk = 2048;
                for (int64_t base = 0; base < anchorCols; base += kBlk) {
                    const int64_t n = (std::min)(kBlk, anchorCols - base);
                    int wmax = 0;
                    int wcur = win;
                    for (int64_t i = 0; i < n; ++i) {
                        blk[static_cast<size_t>(i)] = static_cast<VT>(wcur);
                        if (wcur > wmax) wmax = wcur;
                        const int64_t ax = base + i;
                        if (ax + g < W) {
                            wcur += static_cast<int>(vertSum[static_cast<size_t>(ax + g)]);
                            wcur -= static_cast<int>(vertSum[static_cast<size_t>(ax)]);
                        }
                    }
                    win = wcur;
                    localEval += n;
                    const bool full = static_cast<int64_t>(heap.size()) >= N;
                    if (full && wmax < heap.top().count) continue;   // 整块都进不了榜
                    for (int64_t i = 0; i < n; ++i) {
                        const int winCnt = static_cast<int>(blk[static_cast<size_t>(i)]);
                        const int64_t ax = base + i;
                        bool worth = false;
                        if (static_cast<int64_t>(heap.size()) < N) {
                            worth = true;
                        } else {
                            const BestCand& top = heap.top();
                            if (winCnt > top.count) {
                                worth = true;
                            } else if (winCnt == top.count) {
                                BestCand c;
                                c.count = winCnt;
                                const int64_t cx = x0 + ax + half;
                                const int64_t cz = z0 + az + half;
                                const int64_t dx = cx - opt.startX;
                                const int64_t dz = cz - opt.startZ;
                                c.d2 = dx * dx + dz * dz;
                                c.rank = az * W + ax;
                                c.x = x0 + ax;
                                c.z = z0 + az;
                                if (better(c, top)) {
                                    heap.pop();
                                    heap.push(c);
                                }
                            }
                        }
                        if (worth) {
                            BestCand c;
                            c.count = winCnt;
                            const int64_t cx = x0 + ax + half;
                            const int64_t cz = z0 + az + half;
                            const int64_t dx = cx - opt.startX;
                            const int64_t dz = cz - opt.startZ;
                            c.d2 = dx * dx + dz * dz;
                            c.rank = az * W + ax;
                            c.x = x0 + ax;
                            c.z = z0 + az;
                            if (static_cast<int64_t>(heap.size()) < N) {
                                heap.push(c);
                            } else if (better(c, heap.top())) {
                                heap.pop();
                                heap.push(c);
                            }
                        }
                    }
                }
                if (az + 1 < r1) {
                    const int64_t zOut = z0 + az;
                    const int64_t zIn = z0 + az + g;
                    const uint64_t* rowOut = grid.rowBits(zOut);
                    const uint64_t* rowIn = grid.rowBits(zIn);
                    const int64_t wprL = grid.wordsPerRow();
                    int64_t x = 0;
                    for (int64_t w = 0; w < wprL; ++w) {
                        const uint64_t o = rowOut[w];
                        const uint64_t i = rowIn[w];
                        const int64_t cnt = (std::min)(int64_t{ 64 }, W - x);
                        for (int64_t b = 0; b < cnt; ++b, ++x) {
                            if ((o >> b) & 1ULL) --vertSum[static_cast<size_t>(x)];
                            if ((i >> b) & 1ULL) ++vertSum[static_cast<size_t>(x)];
                        }
                    }
                }
            }
        }
        auto& w = threadWinners[static_cast<size_t>(tid)];
        w.reserve(static_cast<size_t>(heap.size()));
        while (!heap.empty()) {
            w.push_back(heap.top());
            heap.pop();
        }
        evaluated.fetch_add(localEval, std::memory_order_relaxed);
    };

#ifdef _OPENMP
#pragma omp parallel num_threads(nthreadsBest)
#endif
    {
#ifdef _OPENMP
        const int tid = omp_get_thread_num();
#else
        const int tid = 0;
#endif
        // v3.10.12.7 实测：int16 列和在 20k² 上 223 ms vs int32 214 ms（无收益，可能受指令/填充开销限制）
        //   ⇒ 仍统一用 int32；模板保留便于将来换更宽类型。
        runSelect(static_cast<int32_t*>(nullptr), tid);
    }

    // 合并各线程 TopN（总数 ≤ 线程数 × N，很小），再取全局前 N。
    std::vector<BestCand> winners;
    for (const auto& w : threadWinners) winners.insert(winners.end(), w.begin(), w.end());
    std::sort(winners.begin(), winners.end(), better);
    if (static_cast<int64_t>(winners.size()) > N) winners.resize(static_cast<size_t>(N));
    for (const BestCand& c : winners) {
        MatchResult m;
        m.minX = c.x;
        m.maxX = c.x + g - 1;
        m.minZ = c.z;
        m.maxZ = c.z + g - 1;
        m.density = static_cast<float>(c.count) / static_cast<float>(g * g);
        out.results.push_back(m);
    }
    const double ms = elapsedMs();
    if (std::getenv("SLIME_DEBUG")) {
        std::fprintf(stderr, "[best] W=%lld H=%lld fill %.1f ms | select %.1f ms | total %.1f ms | threads %d\n",
                     (long long)W, (long long)H, fillMs, ms - fillMs, ms, nthreadsBest);
        std::fflush(stderr);
    }
    const int64_t evaluatedTotal = evaluated.load(std::memory_order_relaxed);
    out.totalFound = evaluatedTotal;
    out.ok = true;
    out.stoppedEarly = false;
    out.stats.threadsUsed = 1;
    out.stats.scannedChunks = totalChunks;
    out.stats.scanMs = ms;
    out.stats.matchMs = ms;
    out.stats.windowsEvaluated = evaluatedTotal;
    out.stats.totalMs = ms;
    if (progress) progress(1.0, false, 0.0, 0.0);
}

void SlimeEngine::runImpl(const SearchOptions& opt,
                          const std::atomic<bool>& cancel,
                          const ProgressFn& progress,
                          SearchOutcome& out,
                          bool allowChunk) const noexcept
{
    using clock = std::chrono::steady_clock;
    out = SearchOutcome();
    const auto t0 = clock::now();
    const auto elapsedMs = [&t0]() {
        return std::chrono::duration<double, std::milli>(clock::now() - t0).count();
    };

    // ---- 0. 校验与基础量 ----
    out.error = validate(opt);
    if (!out.error.empty()) return;
    if (cancel.load(std::memory_order_relaxed)) { out.cancelled = true; return; }

    const Version version = opt.version;
    const int64_t worldSeed = opt.worldSeed;
    // v3.3.0：算法/模式语义在函数开头统一确定（扫描阶段与匹配阶段共用）。
    // 分层/估算为“密度类”算法：若调用方误配严格图案模式，则强制按密度语义执行
    //（GUI 已限制该组合，此处为引擎级兜底）。
    const bool algoDensityOnly = (opt.algorithm == Algorithm::Layered || opt.algorithm == Algorithm::Sample);
    const bool strict = (opt.mode == Mode::Strict) && !algoDensityOnly;
    const bool sampleAlgo = (opt.algorithm == Algorithm::Sample);
    const bool sampleScan = sampleAlgo && !strict;
    // 估算模式绝不读写区块缓存：采样扫描只计算 1/S² 的区块，若写入缓存会被误认为
    // “整片已覆盖”，后续精确搜索将读到缺失位而给出错误结果（正确性红线）。
    const bool useCache = (version == Version::Bedrock) && opt.useCache && (opt.cache != nullptr) &&
                          !sampleAlgo;
    if (std::getenv("SLIME_DEBUG")) {
        std::fprintf(stderr, "[engine] runImpl start cache=%d\n", useCache ? 1 : 0);
        std::fflush(stderr);
    }
    // v3.4.13：构建模式下工作线程读缓存必须与归并线程互斥（见 SearchOptions::cacheReadMu）
    const bool cacheReadOk = useCache;
    const bool cacheCovers = useCache && [&]() {
        std::unique_lock<std::mutex> lk;
        if (opt.cacheReadMu) lk = std::unique_lock<std::mutex>(*opt.cacheReadMu);
        return opt.cache->covers(opt.region());
    }();
    // v3.3.7.2（任务书步骤 3 / P0-C）：cacheWriteDisabled 由分块路径的准入控制置位
    // v3.3.9（任务书 1.2）：只有缓存构建模式收集条目；普通搜索一律只读缓存
    const bool collectNew = opt.buildMode && useCache && !cacheCovers && !opt.cacheWriteDisabled;

    const int64_t x0 = opt.minX, z0 = opt.minZ;
    const int64_t W = static_cast<int64_t>(opt.maxX) - opt.minX + 1;
    const int64_t H = static_cast<int64_t>(opt.maxZ) - opt.minZ + 1;
    const int64_t totalChunks = W * H;

    // v3.9.12 / v3.10.3：最优候选模式（忽略阈值取最优 N 个）
    //   小区域：经典单次（runBestDensity）；
    //   大区域：走分块路径 —— 每块由子 runImpl 走 runBestDensity（该块最优 N），
    //   父层按 (密度降序, 距离升序, 光栅序) 合并 ⇒ 结果与「设定范围」一致（不再受经典范围限制）。
    if (opt.ignoreThreshold && !strict && !opt.buildMode) {
        const int64_t wordEstBest = gridWordsEstimate(W, H);
        if (!allowChunk || wordEstBest <= kClassicWordsLimit) {
            runBestDensity(opt, cancel, progress, out);
            out.ignoredThreshold = true;
            return;
        }
        // 交给下方分块路径
    }
    if (opt.ignoreThreshold && !strict && !opt.buildMode) {
        out.ignoredThreshold = true;
        // 继续走普通密度路径（matching 内部 detectCountMode）
    }

    // v3.10.10.1：最小密度 = 0 时全部窗口有效 ⇒ 最近 N 个窗口可直接解析求出（快路径）。
    //   SLIME_NO_FAST0=1 可关闭（回归/A-B 对比用）。
    if (!strict && !opt.buildMode && opt.mode == Mode::Density && opt.algorithm == Algorithm::Exact &&
        !sampleScan && !opt.ignoreThreshold && opt.minDensity <= 0.0 && !std::getenv("SLIME_NO_FAST0")) {
        runZeroThresholdFast(opt, cancel, progress, out);
        out.ignoredThreshold = false;
        return;
    }

    // v3.1.1：整图级/超大区域 → 有界内存的 z 分块执行（结果与单次路径一致）
    const int64_t wordEst = gridWordsEstimate(W, H);
    if (allowChunk && (wordEst > kClassicWordsLimit || forceChunkedEnv() || opt.buildMode ||
                    // v3.3.9（任务书 1.3 根因①）：「缓存 + 及时反馈」只在分块路径实现 ⇒
                    // 启用提前停止且带缓存时也走分块路径（否则中小范围永远走经典路径、特性永不触发）
                    (opt.stopAtTarget && opt.useCache && opt.cache != nullptr))) {
        runChunked(opt, cancel, progress, out);
        if (opt.ignoreThreshold && !strict && !opt.buildMode) out.ignoredThreshold = true;
        return;
    }

    int nthreads = opt.threads;
#ifdef _OPENMP
    if (nthreads <= 0) nthreads = omp_get_num_procs(); // 注意：不能用 omp_get_max_threads（会受此前 omp_set_num_threads 影响）
    nthreads = std::clamp(nthreads, 1, 1024);
    omp_set_num_threads(nthreads);
#else
    nthreads = 1;
#endif

    // ---- 1a. v3.3.1（任务书 3.2）：GPU 密度匹配（可选增强） ----
    //
    // 条件：密度模式 + 精确算法 + 非智能范围 + 未启用提前停止 + 未使用基岩缓存
    //       （缓存读写需要逐区块位图，GPU 匹配内核只回传命中窗口）+ 已注入且可用 GPU + 区域达阈值。
    // 语义：GPU 返回**全部**命中（实现若因返回上限无法完整返回则返回 false），引擎按与 CPU
    //       完全相同的 (距离, rank) 序入堆 → 结果与 CPU 精确模式逐位一致；失败自动回退下方 CPU 路径。
    // 环境变量 SLIME_GPU_MATCH=0 可单独关闭「GPU 密度匹配」而保留 GPU 判定扫描（排障/对照用）。
    const char* gpuMatchEnv = std::getenv("SLIME_GPU_MATCH");
    const bool gpuMatchAllowed = !(gpuMatchEnv && *gpuMatchEnv == '0');
    // v3.9.6：GPU 流式匹配现在也支持「智能范围 / 提前停止」——按锚点行带由近到远分批派发，
    //   每批后按与 CPU 相同的无损下界判停（结果仍为全局最近 N）。
    // v3.10.10.1：分层模式在「阈值比例 ≤ 1.00」时结果与精确模式逐位一致（无损），
    //   因此同样可以走 GPU 精确匹配 —— 否则会退化为「GPU 扫描 + CPU 分层匹配」（20k² 195ms vs 16ms）。
    const bool layeredLossless = opt.algorithm == Algorithm::Layered && opt.layerRatio <= 1.0 + 1e-9;
    const bool gpuMatchWanted = gpuMatchAllowed && opt.mode == Mode::Density &&
                                (opt.algorithm == Algorithm::Exact || layeredLossless) &&
                                !sampleScan &&
                                !(version == Version::Bedrock && useCache) && opt.gpuScan &&
                                opt.gpuScan->available() && totalChunks >= gpuMinChunksEnv(opt.version);
    if (gpuMatchWanted) {
        DensityMatchParams gp;
        gp.version = version;
        gp.worldSeed = worldSeed;
        gp.gridSize = std::max(1, opt.gridSize);
        gp.axMin = x0;
        gp.axMax = x0 + W - gp.gridSize;      // 窗口不越界（与 CPU 锚点范围一致）
        gp.azMin = z0;
        // v3.1.1 分块路径：本块只负责 [start, start+anchorStep-1] 的锚点行（避免跨块重复评估）
        gp.azMax = std::min<int64_t>(z0 + H - gp.gridSize, g_anchorMaxAZ);
        gp.minDensity = opt.minDensity;
        // v3.8.1：流式匹配每命中只占 12 字节，上限放宽到 400 万（超过则回退 CPU 保证语义）
        // v3.10.12.8 实测：小区域把上限降到 200 万对耗时无可测收益（差异在噪声内），保持 400 万。
        const int64_t gpuCap =
            (std::min)(int64_t{ 8000000 }, (std::max)(int64_t{ 4000000 }, opt.targetCount * 1000));
        gp.maxResults = gpuCap;
        gp.startX = opt.startX;
        gp.startZ = opt.startZ;
        // v3.10.12.1：GPU 只回传「最近的 keep 个」(d2,rank) 候选，最终 TopN 一定在其中；
        //   之前 200×target（20k² 时 1 万条）会把大量命中回传给宿主做二次排序。
        gp.keepCandidates = (std::min)(int64_t{ 200000 }, (std::max)(int64_t{ 256 }, opt.targetCount * 4));
        // v3.10.11.1：结果自优化（用户开关）——仅密度精确 GPU 流式匹配生效
        gp.earlyStop = opt.gpuResultSelfOptimize;
        gp.earlyStopTarget = opt.targetCount;
        int64_t gpuTotalFound = 0;
        gp.outTotalFound = &gpuTotalFound;
        if (gp.axMax >= gp.axMin && gp.azMax >= gp.azMin) {
            std::vector<MatchResult> gpuResults;
            std::string gerr;
            GpuPhaseMark gpuMatchPhase(opt.livePhase, 3);   // v3.10.3：GUI 显示「GPU 匹配」
            if (progress) progress(kScanFrac0, true, 0.0, 0.0);
            const auto tMd0 = std::chrono::steady_clock::now();

            // v3.9.6：智能范围/提前停止 时按行带由近到远派发；凑够目标数且剩余带不可能更近即停。
            const int64_t bandRowsTotal = gp.azMax - gp.azMin + 1;
            // v3.9.6：行带分批只在区域足够大时启用（小块走单次派发，避免多次派发开销）：
            //   分块路径里每块都分批会让中等区域反而变慢（实测 12000² 单次更快）。
            // v3.9.8：行带分批只在「仅提前停止」时启用；与智能范围组合时实测有并列距离选取差异，
            //   改为一次派发全量（结果与 CPU 完全一致，牺牲一点早停收益）。
            // v3.9.8（正确性兜底）：行带分批派发暂时关闭 —— 实测在部分区域与 CPU 的最近 N 名单
            //   存在并列距离选取差异；先保证 GPU 一次派发全量的结果与 CPU 完全一致。
            // v3.10.2 曾对大区域一律行带派发以提供进度；v3.10.4（用户反馈：降速副作用明显）：
            //   **恢复为只在使用「提前停止」时行带派发** —— 普通 GPU 搜索回到单次全量派发，
            //   避免每带排序/合并与命中上限导致的额外开销或整体回退 CPU。
            //   ⇒ GPU 搜索期间进度条可能长时间停在 0（单次派发无法上报中间进度），完成后直接到 100%。
            // v3.10.5：提前停止时（含智能范围）都按行带由近到远派发，才能在凑够 N 后停下；
            //   无早停时保持单次全量派发（速度优先）。
            const bool bandForProgress = opt.stopAtTarget && !opt.smartRange && bandRowsTotal >= 4096;
            if (bandForProgress) {
                const int64_t rowLo = gp.azMin, rowHi = gp.azMax;
                const int64_t rowCount = rowHi - rowLo + 1;
                const int64_t maxBands = (std::min)(int64_t{ 256 }, (std::max)(int64_t{ 1 }, rowCount / 4096));
                int64_t bandH = (std::max)(int64_t{ 256 }, rowCount / maxBands);
                std::vector<std::pair<double, std::pair<int64_t, int64_t>>> bands;
                for (int64_t lo = rowLo; lo <= rowHi; lo += bandH) {
                    const int64_t hi = (std::min)(rowHi, lo + bandH - 1);
                    double d = 0.0;
                    if (opt.startZ < lo) d = static_cast<double>(lo - opt.startZ);
                    else if (opt.startZ > hi) d = static_cast<double>(opt.startZ - hi);
                    bands.push_back({ d, { lo, hi } });
                }
                std::stable_sort(bands.begin(), bands.end(),
                                 [](const auto& a, const auto& b) { return a.first < b.first; });
                TopNHeap bandHeap(opt.targetCount);
                int64_t bandTotal = 0;
                bool bandOk = true;
                double lastL = 0.0;
                for (size_t bi = 0; bi < bands.size(); ++bi) {
                    if (cancel.load(std::memory_order_relaxed)) break;
                    DensityMatchParams bp = gp;
                    bp.azMin = bands[bi].second.first;
                    bp.azMax = bands[bi].second.second;
                    int64_t partTotal = 0;
                    bp.outTotalFound = &partTotal;
                    std::vector<MatchResult> part;
                    if (!opt.gpuScan->matchDensity(bp, cancel, part, &gerr)) {
                        bandOk = false;
                        break;
                    }
                    bandTotal += partTotal > 0 ? partTotal : static_cast<int64_t>(part.size());
                    // v3.9.9：按 (d2, z, x) 排序后再入堆 —— GPU 命中顺序由原子计数决定（每次运行不同），
                    //   并列距离时的 TopN 裁决会不确定；排序后与 CPU 光栅顺序一致、可复现。
                    std::sort(part.begin(), part.end(), [&](const MatchResult& a, const MatchResult& b) {
                        const int64_t ax2 = 2 * static_cast<int64_t>(a.minX) + (gp.gridSize - 1) -
                                            2 * static_cast<int64_t>(opt.startX);
                        const int64_t az2 = 2 * static_cast<int64_t>(a.minZ) + (gp.gridSize - 1) -
                                            2 * static_cast<int64_t>(opt.startZ);
                        const int64_t bx2 = 2 * static_cast<int64_t>(b.minX) + (gp.gridSize - 1) -
                                            2 * static_cast<int64_t>(opt.startX);
                        const int64_t bz2 = 2 * static_cast<int64_t>(b.minZ) + (gp.gridSize - 1) -
                                            2 * static_cast<int64_t>(opt.startZ);
                        const int64_t da = ax2 * ax2 + az2 * az2, db = bx2 * bx2 + bz2 * bz2;
                        if (da != db) return da < db;
                        if (a.minZ != b.minZ) return a.minZ < b.minZ;
                        return a.minX < b.minX;
                    });
                    for (const MatchResult& m : part) {
                        const int64_t off2x = 2 * static_cast<int64_t>(m.minX) + (gp.gridSize - 1) -
                                              2 * static_cast<int64_t>(opt.startX);
                        const int64_t off2z = 2 * static_cast<int64_t>(m.minZ) + (gp.gridSize - 1) -
                                              2 * static_cast<int64_t>(opt.startZ);
                        const int64_t d2 = off2x * off2x + off2z * off2z;
                        if (bandHeap.full() && d2 > bandHeap.worstD2()) continue;
                        Scored s;
                        s.m = m;
                        s.d2 = d2;
                        s.dist = 0.5 * std::sqrt(static_cast<double>(d2));
                        bandHeap.push(s);
                    }
                    // v3.10.2：真实扫描量（本带覆盖 blocked 数）+ 速率 + 预计剩余，让 GPU 干活可见
                    const int64_t bandChunks =
                        static_cast<int64_t>(bands[bi].second.second - bands[bi].second.first + 1) * W;
                    if (opt.liveScanned) opt.liveScanned->fetch_add(bandChunks, std::memory_order_relaxed);
                    if (opt.liveTotal && allowChunk) opt.liveTotal->store(totalChunks, std::memory_order_relaxed);
                    if (progress) {
                        const double frac = static_cast<double>(bi + 1) / static_cast<double>(bands.size());
                        const double elSec = elapsedMs() / 1000.0;
                        const double doneChunks =
                            static_cast<double>(bi + 1) * static_cast<double>(bandRowsTotal) /
                            static_cast<double>(bands.size()) * static_cast<double>(W);
                        const double rate = elSec > 0.01 ? doneChunks / elSec : 0.0;
                        const double eta = rate > 1.0 ? (static_cast<double>(totalChunks) - doneChunks) / rate : 0.0;
                        progress(kScanFrac0 + (kMatchFrac1 - kScanFrac0) * frac, true, rate, eta);
                    }
                    if (opt.stopAtTarget && bandHeap.full() && bi + 1 < bands.size()) {
                        lastL = std::numeric_limits<double>::infinity();
                        for (size_t rj = bi + 1; rj < bands.size(); ++rj) {
                            // v3.9.8：排名距离按「窗口中心」计，锚点要映射到中心再算下界
                            //   （漏掉这一步会让下界偏大 (g-1)/2，提前停止会丢掉更近的结果）
                            const double half = (gp.gridSize - 1) * 0.5;
                            lastL = (std::min)(lastL,
                                               distToRect(static_cast<double>(opt.startX),
                                                          static_cast<double>(opt.startZ),
                                                          static_cast<double>(gp.axMin) + half,
                                                          static_cast<double>(gp.axMax) + half,
                                                          static_cast<double>(bands[rj].second.first) + half,
                                                          static_cast<double>(bands[rj].second.second) + half));
                        }
                        const double margin = gp.gridSize * 0.5 + 1.0;
                        if (bandHeap.worstDist() + margin <= lastL) break;   // 留余量，避开并列/边界歧义
                    }
                }
                if (bandOk && !cancel.load(std::memory_order_relaxed)) {
                    out.gpuMatchUsed = true;
                    out.stats.scanMs = elapsedMs();
                    out.stats.threadsUsed = nthreads;
                    out.stats.scannedChunks = totalChunks;
                    for (const Scored& s : bandHeap.sorted()) out.results.push_back(s.m);
                    out.totalFound = bandTotal;
                    out.stoppedEarly = opt.stopAtTarget && bandHeap.full();
                    out.stats.matchMs = elapsedMs();
                    out.stats.totalMs = out.stats.matchMs;
                    if (progress) progress(kMatchFrac1, false, 0.0, 0.0);
                    out.ok = true;
                    return;
                }
                if (bandOk && cancel.load(std::memory_order_relaxed)) {
                    out.cancelled = true;
                    out.stats.totalMs = elapsedMs();
                    return;
                }
                if (std::getenv("SLIME_DEBUG")) {
                    std::fprintf(stderr, "[engine] GPU 行带匹配失败，回退 CPU：%s\n", gerr.c_str());
                    std::fflush(stderr);
                }
            }
            const bool gpuOk = opt.gpuScan->matchDensity(gp, cancel, gpuResults, &gerr);
            const auto tMd1 = std::chrono::steady_clock::now();
            if (gpuOk) {
                out.gpuMatchUsed = true;
                // v3.10.4：单次派发没有中间进度 ⇒ 完成时一次性补记「已扫描区块数」，
                //   保证结果统计/过程摘要不出现「扫描 0 区块」的误导。
                if (opt.liveScanned) opt.liveScanned->fetch_add(totalChunks, std::memory_order_relaxed);
                if (opt.liveTotal && allowChunk) opt.liveTotal->store(totalChunks, std::memory_order_relaxed);
                if (cancel.load(std::memory_order_relaxed)) {
                    out.cancelled = true;
                    out.stats.totalMs = elapsedMs();
                    return;
                }
                out.stats.scanMs = elapsedMs();
                out.stats.threadsUsed = nthreads;
                out.stats.scannedChunks = totalChunks;
                const auto tConv0 = std::chrono::steady_clock::now();
                TopNHeap heap(opt.targetCount);
                const int64_t nAz = gp.azMax - gp.azMin + 1;
                for (const MatchResult& m : gpuResults) {
                    // v3.8.1：先用整数平方距离判断能否进榜（命中可能上百万，逐次 hypot + 堆操作很贵）
                    const int64_t off2x = 2 * static_cast<int64_t>(m.minX) + (gp.gridSize - 1) -
                                          2 * static_cast<int64_t>(opt.startX);
                    const int64_t off2z = 2 * static_cast<int64_t>(m.minZ) + (gp.gridSize - 1) -
                                          2 * static_cast<int64_t>(opt.startZ);
                    const int64_t d2 = off2x * off2x + off2z * off2z;
                    if (heap.full() && d2 > heap.worstD2()) continue;
                    const double dist = 0.5 * std::sqrt(static_cast<double>(d2));
                    const int64_t rank = (m.minX - gp.axMin) * nAz + (m.minZ - gp.azMin);
                    heap.push(Scored{ m, dist, rank, d2 });
                }
                const auto tConv1 = std::chrono::steady_clock::now();
                for (const Scored& s : heap.sorted()) out.results.push_back(s.m);
                out.totalFound = gpuTotalFound > 0 ? gpuTotalFound
                                                   : static_cast<int64_t>(gpuResults.size());
                if (std::getenv("SLIME_DEBUG")) {
                    const auto ms = [](auto a, auto b) {
                        return std::chrono::duration<double, std::milli>(b - a).count();
                    };
                    std::fprintf(stderr,
                                 "[engine] GPU 密度匹配：matchDensity %.2f ms | 结果转换+入堆 %.2f ms | "
                                 "命中 %zu（锚点域 x[%lld,%lld] z[%lld,%lld]，g=%d）\n",
                                 ms(tMd0, tMd1), ms(tConv0, tConv1), gpuResults.size(),
                                 static_cast<long long>(gp.axMin), static_cast<long long>(gp.axMax),
                                 static_cast<long long>(gp.azMin), static_cast<long long>(gp.azMax),
                                 gp.gridSize);
                }
                out.stoppedEarly = false;
                out.stats.matchMs = elapsedMs();
                out.stats.totalMs = out.stats.matchMs;
                if (progress) progress(kMatchFrac1, false, 0.0, 0.0);
                out.ok = true;
                if (std::getenv("SLIME_DEBUG")) {
                    std::fprintf(stderr, "[engine] GPU 密度匹配完成：命中 %lld，显示 %zu\n",
                                 static_cast<long long>(out.totalFound), out.results.size());
                    std::fflush(stderr);
                }
                return;
            }
            if (out.gpuFallbackReason.empty() && !gerr.empty()) {
                out.gpuFallbackReason = "匹配：" + gerr;
            }
            if (std::getenv("SLIME_DEBUG")) {
                std::fprintf(stderr, "[engine] GPU 密度匹配不可用，回退 CPU：%s\n", gerr.c_str());
                std::fflush(stderr);
            }
        }
    }

    // ---- 1. 扫描阶段：生成区块位图 ----
    BitGrid grid;
    if (!grid.init(x0, z0, W, H)) {
        out.error = "内存不足，无法分配区块位图，请缩小范围。";
        return;
    }
    out.stats.threadsUsed = nthreads;
    const int64_t wpr = grid.wordsPerRow();

    // v3.2.0：GPU 可选加速。成功(1)则跳过下方 CPU 扫描块；0=失败回退 CPU；-1=取消
    // v3.3.7（P0-3）：扫描阶段的真实计数统一由本发射器累加（CPU/GPU 两条扫描路径共用）
    Emitter scanEm(progress, kScanFrac0, kScanFrac1, true, opt.liveScanned);
    // v3.3.7.2（任务书 1.2）：经典路径的总扫描量 = W×H（新区块数，无重叠）→ 同源分母。
    // 采样（快速估算）模式下扫描量不是全量，故不公开（界面会退化为只显示「扫描 N 区块」）。
    // 注意：**只有顶层调用**才写（分块路径的子块 runImpl 是块内局部范围，
    // 若也写入会把全局分母覆盖成「单块总量」——自检 16.2 已抓到过这个错误）。
    if (opt.liveTotal && allowChunk) {
        opt.liveTotal->store(sampleScan ? 0 : totalChunks, std::memory_order_relaxed);
    }
    scanEm.report(0, totalChunks, 0.0, 0.0);
    std::vector<std::vector<std::pair<int, int>>> threadNew(static_cast<size_t>(nthreads));

    // ---- v3.3.8（任务 3+5 第 3 步）：**整行命中 ⇒ 直接用缓存位图该行（跳过填充判定）** ----
    // 条件：缓存里存在与本区域 **X 跨度完全相同**、且位图几何一致（wpr 相同）的声明；
    //       该行落在它的 Z 区间内。这样「填充（LCG/MT19937 逐点判定）」整行可省 —— E3 实测填充占墙钟约 47%。
    // 安全性：缓存索引可能在本轮运行中被剪枝/合并改写（分块路径 cacheWrite==cache），
    //       故每次按 idx 重新取指针并**校验矩形未变**；不一致就回退到正常计算（绝不复制错行）。
    struct FillRowSrc {
        size_t idx = 0;
        Rect rect{};
    };
    std::vector<FillRowSrc> fillRowSrcs;
    if (cacheReadOk && !sampleScan && opt.cache != nullptr) {
        // v3.4.13：构造「缓存命中行来源」时整段持锁（读 ranges/bitmaps 向量本身）
        std::unique_lock<std::mutex> lk;
        if (opt.cacheReadMu) lk = std::unique_lock<std::mutex>(*opt.cacheReadMu);
        const std::vector<Rect>& crs = opt.cache->ranges();
        for (size_t i = 0; i < crs.size(); ++i) {
            if (crs[i].minX != x0 || crs[i].maxX != static_cast<int>(x0 + W - 1)) continue;
            int64_t wprC = 0, rowsC = 0;
            if (opt.cache->rangeBitmap(i, &wprC, &rowsC) == nullptr || wprC != wpr) continue;
            fillRowSrcs.push_back(FillRowSrc{ i, crs[i] });
        }
    }
    std::atomic<int64_t> skippedFillRows{ 0 };
    // v3.4.13：把缓存位图的一行**拷贝**到本线程缓冲（持锁期间完成），返回拷贝指针。
    //   此前返回的是缓存内部指针，既可能与归并线程的置位竞争，也会在向量重分配后失效。
    // 注意：runImpl 的填充在 OpenMP 线程里并行执行 ⇒ 该缓冲必须是**线程局部**的，
    //   否则多个线程会互相覆盖对方的行数据（v3.4.13 实测：命中缓存的搜索会读到错行）。
    static thread_local std::vector<uint64_t> cacheRowScratch;
    const auto fillFromCacheRow = [&](int64_t absZ) -> const uint64_t* {
        std::unique_lock<std::mutex> lk;
        if (opt.cacheReadMu) lk = std::unique_lock<std::mutex>(*opt.cacheReadMu);
        for (const FillRowSrc& s : fillRowSrcs) {
            if (absZ < s.rect.minZ || absZ > s.rect.maxZ) continue;
            // 校验该下标当前仍是同一矩形（防剪枝/合并导致的下标漂移）
            const std::vector<Rect>& now = opt.cache->ranges();
            if (s.idx >= now.size()) return nullptr;
            const Rect& cur = now[s.idx];
            if (cur.minX != s.rect.minX || cur.maxX != s.rect.maxX || cur.minZ != s.rect.minZ ||
                cur.maxZ != s.rect.maxZ) {
                return nullptr;
            }
            int64_t wprC = 0, rowsC = 0;
            const uint64_t* bits = opt.cache->rangeBitmap(s.idx, &wprC, &rowsC);
            if (bits == nullptr || wprC != wpr || rowsC <= 0) return nullptr;
            const uint64_t* row = bits + (absZ - cur.minZ) * wprC;
            cacheRowScratch.assign(row, row + wprC); // 持锁拷贝（wprC 为本图每行字数）
            return cacheRowScratch.data();
        }
        return nullptr;
    };

    int gpuStage = 0;
    std::string gpuStageErr;
    int64_t sampleChunks = totalChunks; // v3.3.0：采样模式实际计算的区块数（统计/进度用）
    const bool sampleExact = sampleScan && opt.sampleExact; // 精算采样：扫描采样窗口所需全部区块
    const int sampleS = std::clamp(opt.sampleStep, 1, 64);
    const int sampleG = std::max(1, opt.gridSize);
    // 估算模式强制 CPU 采样扫描：GPU 会计算整幅位图（无采样收益），且采样扫描本身已快 64 倍。
    // v3.8.0：缓存构建模式一律走 CPU —— 构建要逐区块位图去重后写缓存，GPU 扫描阶段
    //   不产出 newCacheChunks（实测多段构建会「扫了 1.4 亿区块却一条缓存都没写」）。
    // v3.10.10.2：图案模式（GPU 图案流式默认关）在中小区域用 GPU 扫描反而更慢
    //   （20k²：110 vs 89 ms；40k² 基本持平；100k² 起 GPU 明显占优 2125 vs 2711 ms）。
    //   因此图案模式仅在区域 ≥ 3e9 区块时启用 GPU 扫描；密度模式不受影响。
    const bool patternGpuScanOk = (opt.mode != Mode::Strict) || totalChunks >= 3000000000LL;
    if (!sampleScan && !opt.buildMode && opt.gpuScan && opt.gpuScan->available() && patternGpuScanOk &&
        totalChunks >= gpuMinChunksEnv(opt.version)) {
        gpuStage = runGpuScanStage(opt, cancel, scanEm, grid, threadNew[0], gpuStageErr);
        if (gpuStage == 0 && std::getenv("SLIME_DEBUG")) {
            std::fprintf(stderr, "[engine] GPU 扫描不可用，回退 CPU：%s\n", gpuStageErr.c_str());
            std::fflush(stderr);
        }
    }
    out.gpuScanUsed = (gpuStage == 1);
    if (gpuStage == 0 && !gpuStageErr.empty() && out.gpuFallbackReason.empty()) {
        out.gpuFallbackReason = "扫描：" + gpuStageErr;
    }
    if (gpuStage != 1) {
    // v3.1.0：x 尾项预计算表（Java 版，x 项与 z 无关，所有行共享），
    // 每区块扫描由“若干次 32 位回绕乘法”降为 1 次内存读取 + 1 次加法。
    // 宽度超过限值时退回即时计算（表本身会超出合理内存）。
    constexpr int64_t kXTableMaxCols = 1 << 21; // 2,097,152 列 → 16MB
    std::vector<uint64_t> xTailTable;
    const uint64_t* xTailPtr = nullptr;
    const bool javaVersion = (version == Version::Java);
    if (javaVersion && W <= kXTableMaxCols) {
        xTailTable.resize(static_cast<size_t>(W));
        for (int64_t xr = 0; xr < W; ++xr) {
            xTailTable[static_cast<size_t>(xr)] = javaXTail(static_cast<int32_t>(x0 + xr));
        }
        xTailPtr = xTailTable.data();
    }

    // 行执行顺序：智能范围模式按距起点近的行优先
    std::vector<int64_t> rowOrder(static_cast<size_t>(H));
    for (int64_t r = 0; r < H; ++r) rowOrder[static_cast<size_t>(r)] = r;
    if (opt.smartRange) {
        std::stable_sort(rowOrder.begin(), rowOrder.end(), [&](int64_t a, int64_t b) {
            const int64_t da = std::llabs(z0 + a - opt.startZ);
            const int64_t db = std::llabs(z0 + b - opt.startZ);
            if (da != db) return da < db;
            return (z0 + a) < (z0 + b);
        });
    }

    // v3.3.0（3.5）：快速估算模式 —— 仅对采样网格上的区块判定（其余位保持 0，缓存已禁用）。
    // 匹配阶段只评估锚点落在采样网格上的窗口。
    if (sampleScan) {
        const int S = sampleS;
        const int g = sampleG;
        // 需要计算的坐标数量：快速估算 = 采样网格点；精算采样 = 采样锚点窗口覆盖的区块
        const auto neededCount = [&](int64_t n) -> int64_t {
            if (sampleExact) {
                if (g >= S) return n;
                return (n / S) * g + std::min<int64_t>(n % S, g);
            }
            return (n + S - 1) / S;
        };
        sampleChunks = neededCount(W) * neededCount(H);
        std::atomic<int64_t> sDone{ 0 };
        const auto sampleTask = [&](int64_t r) {
            if (cancel.load(std::memory_order_relaxed)) return;
            int64_t got = 0;
            const int64_t rz = z0 + r;
            const int32_t cz = static_cast<int32_t>(rz);
            // 快速估算：仅采样网格行（r % S == 0）；
            // 精算采样：还需覆盖采样锚点窗口的行（r % S < g，g ≥ S 时全部行）。
            const bool rowNeeded = sampleExact ? (g >= S || (r % S) < g) : ((r % S) == 0);
            if (rowNeeded) {
                // v3.3.1：基岩版采样扫描改用 AVX2 批量 MT19937（此前为逐区块标量 MT，约 1.3M/s）
                //   快速估算：采样列构成步长 S 的等差序列 → bedrockSlimeScanStrided（每批 4/8 点）
                //   精算采样：采样列为「连续 g 个 + 间隔 S-g 个」→ 按连续段批量 bedrockSlimeScan
                // 结果与标量路径逐位一致（内核保证；SLIME_FORCE_SCALAR=1 可强制标量对照）。
                if (!javaVersion) {
                    static thread_local std::vector<uint8_t> sBuf;
                    if (!sampleExact) {
                        const int64_t cnt = (W + S - 1) / S;
                        if (static_cast<int64_t>(sBuf.size()) < cnt) sBuf.resize(static_cast<size_t>(cnt));
                        fastmt::bedrockSlimeScanStrided(static_cast<int32_t>(x0), cz,
                                                        static_cast<int32_t>(cnt), S, sBuf.data());
                        for (int64_t i = 0; i < cnt; ++i) {
                            if (sBuf[static_cast<size_t>(i)]) grid.setSlime(x0 + i * S, rz);
                        }
                        got = cnt;
                    } else {
                        const int64_t run = (g >= S) ? W : g;   // 每段连续区块数
                        const int64_t step = (g >= S) ? W : S;  // 段起点间隔
                        for (int64_t xr = 0; xr < W; xr += step) {
                            const int64_t n = std::min<int64_t>(run, W - xr);
                            if (static_cast<int64_t>(sBuf.size()) < n) sBuf.resize(static_cast<size_t>(n));
                            fastmt::bedrockSlimeScan(static_cast<int32_t>(x0 + xr), cz,
                                                     static_cast<int32_t>(n), sBuf.data(), false);
                            for (int64_t i = 0; i < n; ++i) {
                                if (sBuf[static_cast<size_t>(i)]) grid.setSlime(x0 + xr + i, rz);
                            }
                            got += n;
                        }
                    }
                } else {
                    for (int64_t xr = 0; xr < W; ++xr) {
                        if (sampleExact ? !(g >= S || (xr % S) < g) : (xr % S) != 0) continue;
                        const int64_t ax = x0 + xr;
                        if (isSlimeJavaFast(worldSeed, static_cast<int32_t>(ax), cz)) grid.setSlime(ax, rz);
                        ++got;
                    }
                }
            }
            const int64_t cd = sDone.fetch_add(got, std::memory_order_relaxed) + got;
            const double el = elapsedMs();
            const double speed = el > 0.0 ? (static_cast<double>(cd) / el) * 1000.0 : 0.0;
            const double eta = speed > 1.0 ? (static_cast<double>(sampleChunks - cd) / speed) : 0.0;
            scanEm.report(cd, std::max<int64_t>(1, sampleChunks), speed, eta);
        };
#ifdef _OPENMP
        if (nthreads > 1) {
#pragma omp parallel for schedule(dynamic, 8)
            for (int64_t r = 0; r < H; ++r) {
                sampleTask(r);
            }
        } else
#endif
        {
            for (int64_t r = 0; r < H; ++r) sampleTask(r);
        }
    }

    const int64_t rowBatch = std::max<int64_t>(1, kScanStride / std::max<int64_t>(1, W));
    const int64_t nTasks = (H + rowBatch - 1) / rowBatch;
    if (std::getenv("SLIME_DEBUG")) {
        std::fprintf(stderr, "[engine] grid ok rows=%lld wpr=%lld tasks=%lld nthreads=%d\n",
                     static_cast<long long>(H), static_cast<long long>(wpr),
                     static_cast<long long>(nTasks), nthreads);
        std::fflush(stderr);
    }

    std::atomic<int64_t> chunksDone{ 0 };

    // 行内“缓存覆盖/计算”分段结构
    struct XSeg { int64_t a, b; bool cached; };

    const auto scanOneRow = [&](int64_t absZ, std::vector<XSeg>& segs, std::vector<std::pair<int, int>>& localNew, bool wantNew) {
        // 构建 [0, W) 的完整分段（缓存覆盖部分 + 计算部分）
        segs.clear();
        std::vector<std::pair<int64_t, int64_t>> cis; // 缓存覆盖区间
        if (useCache) {
            for (const Rect& rc : opt.cache->ranges()) {
                if (absZ >= rc.minZ && absZ <= rc.maxZ) {
                    const int64_t a = std::max<int64_t>(0, static_cast<int64_t>(rc.minX) - x0);
                    const int64_t b = std::min<int64_t>(W, static_cast<int64_t>(rc.maxX) - x0 + 1);
                    if (a < b) cis.emplace_back(a, b);
                }
            }
            if (!cis.empty()) {
                std::sort(cis.begin(), cis.end());
                int64_t cur = 0;
                int64_t cA = cis.front().first, cB = cis.front().second;
                for (size_t i = 1; i < cis.size(); ++i) {
                    if (cis[i].first <= cB) { cB = std::max(cB, cis[i].second); }
                    else { if (cA > cur) segs.push_back(XSeg{ cur, cA, false }); segs.push_back(XSeg{ cA, cB, true }); cur = cB; cA = cis[i].first; cB = cis[i].second; }
                }
                if (cA > cur) segs.push_back(XSeg{ cur, cA, false });
                segs.push_back(XSeg{ cA, cB, true });
                cur = std::max(cur, cB);
                if (cur < W) segs.push_back(XSeg{ cur, W, false });
            }
        }
        if (segs.empty()) segs.push_back(XSeg{ 0, W, false });

        const int32_t cz = static_cast<int32_t>(absZ);
        const bool java = javaVersion;
        // 行基座（Java）：仅依赖 z 与种子，本行所有区块共享（v3.1.0）
        const uint64_t rowBase = java ? javaRowBase(worldSeed, cz) : 0;
        const uint64_t* tbl = java ? xTailPtr : nullptr;
        for (const XSeg& s : segs) {
            if (s.cached) {
                // v3.4.13：命中段的逐区块查询整段持锁（与归并线程互斥；段长通常远小于一行）
                std::unique_lock<std::mutex> lk;
                if (opt.cacheReadMu) lk = std::unique_lock<std::mutex>(*opt.cacheReadMu);
                for (int64_t xr = s.a; xr < s.b; ++xr) {
                    const int cx = static_cast<int>(x0 + xr);
                    const int czz = static_cast<int>(absZ);
                    if (opt.cache->isSlime(cx, czz)) grid.setSlime(x0 + xr, absZ);
                }
            } else if (java) {
                // v3.1.0：寄存器内累计判定位，整字(64 区块)写回，消除每区块内存读改写。
                // v3.6.16：整字落在段内时改走 AVX2 16-lane 内核（fastjava::slimeMask16），
                //   4 次调用凑满一个 64 位字；段边界上的残字仍走标量（结果逐位一致）。
                uint64_t* rowW = grid.rowBits(absZ);
                int64_t xr = s.a;
                const int64_t segEnd = s.b;
                if (tbl) {
                    if (kUseFastJava && kUseScanFastPath) {
                        // v3.6.20：把「对齐分支 + 逐字 min() + if(acc)」从热循环里摘出去：
                        //   前导残字 → 整字直路（无分支）→ 尾部残字。
                        while (xr < segEnd && (xr & 63) != 0) {
                            if (javaSlimeFromTail(rowBase, tbl[xr])) rowW[xr >> 6] |= 1ULL << (xr & 63);
                            ++xr;
                        }
                        const int64_t wEnd = segEnd & ~63LL;
                        const bool prof = kScanProfileOn;
                        const unsigned long long tProf0 = prof ? __rdtsc() : 0ULL;
                        const int64_t profXr0 = xr;
                        for (; xr < wEnd; xr += 64) {
                            if ((xr & 0x3FFF) == 0 && cancel.load(std::memory_order_relaxed)) return;
                            rowW[xr >> 6] |= fastjava::slimeMask64(rowBase, static_cast<int32_t>(x0 + xr));
                        }
                        if (prof) {
                            g_profKernelCycles.fetch_add(static_cast<int64_t>(__rdtsc() - tProf0),
                                                         std::memory_order_relaxed);
                            g_profKernelChunks.fetch_add(wEnd - profXr0, std::memory_order_relaxed);
                        }
                        for (; xr < segEnd; ++xr) {
                            if (javaSlimeFromTail(rowBase, tbl[xr])) rowW[xr >> 6] |= 1ULL << (xr & 63);
                        }
                    } else {
                        while (xr < segEnd) {
                            if (((xr - s.a) & 0x3FFF) == 0 && cancel.load(std::memory_order_relaxed)) return;
                            const int64_t wBase = xr & ~63LL;
                            const int64_t wEnd = std::min(segEnd, wBase + 64);
                            uint64_t acc = 0;
                            for (; xr < wEnd; ++xr)
                                acc |= static_cast<uint64_t>(javaSlimeFromTail(rowBase, tbl[xr])) << (xr & 63);
                            if (acc) rowW[wBase >> 6] |= acc;
                        }
                    }
                } else {
                    while (xr < segEnd) {
                        if (((xr - s.a) & 0x3FFF) == 0 && cancel.load(std::memory_order_relaxed)) return;
                        const int64_t xe = std::min(segEnd, (xr & ~63LL) + 64);
                        uint64_t acc = 0;
                        for (; xr < xe; ++xr) {
                            acc |= static_cast<uint64_t>(javaSlimeFromTail(rowBase, javaXTail(static_cast<int32_t>(x0 + xr)))) << (xr & 63);
                        }
                        if (acc) rowW[(xe - 1) >> 6] |= acc;
                    }
                }
            } else {
                // 基岩版：v3.3.0（3.4）AVX2 批量 MT19937 —— 一次判定 32 个连续区块
                // （AVX2 不可用时内核内部自动回退标量；结果与逐区块判定逐位一致，
                //   亦可用 SLIME_FORCE_SCALAR=1 强制标量做 A/B 对照）。
                const uint32_t czu = static_cast<uint32_t>(cz);
                int64_t xx = s.a;
                uint8_t batch[kBedrockBatch];
                while (xx < s.b) {
                    if (((xx - s.a) & 0x3FFF) == 0 && cancel.load(std::memory_order_relaxed)) return;
                    const int32_t n = static_cast<int32_t>(std::min<int64_t>(kBedrockBatch, s.b - xx));
                    fastmt::bedrockSlimeScan(static_cast<int32_t>(x0 + xx), cz, n, batch, false);
                    for (int32_t i = 0; i < n; ++i) {
                        if (!batch[i]) continue;
                        const int64_t ax = x0 + xx + i;
                        grid.setSlime(ax, absZ);
                        if (wantNew) {
                            localNew.emplace_back(static_cast<int>(ax), cz);
                        }
                    }
                    xx += n;
                }
            }
        }
    };

    const auto scanTask = [&](int64_t taskIdx) {
        if (cancel.load(std::memory_order_relaxed)) return;
        const int64_t rBegin = taskIdx * rowBatch;
        const int64_t rEnd = std::min(H, rBegin + rowBatch);
        int64_t done = 0;
        std::vector<XSeg> segs;
        std::vector<std::pair<int, int>> localNew;
        localNew.reserve(4096);
#ifdef _OPENMP
        const int tid = omp_get_thread_num();
#else
        const int tid = 0;
#endif
        bool wantNew = collectNew;
        for (int64_t ri = rBegin; ri < rEnd; ++ri) {
            if (cancel.load(std::memory_order_relaxed)) return;
            const int64_t r = rowOrder[static_cast<size_t>(ri)];
            const int64_t absZ = z0 + r;
            // v3.3.8（任务 3+5 第 3 步）：整行被缓存位图覆盖 ⇒ 直接 memcpy 该行，跳过填充判定
            // v3.3.9.1（任务书 1.2）：构建模式下**不做位图直取** —— 位图读取会与其它线程的归并
            // 并发访问同一个 CacheManager（无内部锁）⇒ 数据竞争；整块已覆盖的块在上层已跳过，
            // 剩余行重算即可（写入时由 setBit 去重，不会产生重复条目）。
            const uint64_t* cachedRow = opt.buildMode ? nullptr : fillFromCacheRow(absZ);
            // v3.3.9（任务书 1.4）：阶段指示 —— 本行来自缓存位图（缓存扫描）还是新判定（新区域扫描）
            if (opt.livePhase) opt.livePhase->store(cachedRow != nullptr ? 1 : 0, std::memory_order_relaxed);
            if (cachedRow != nullptr) {
                std::memcpy(grid.rowBits(absZ), cachedRow, static_cast<size_t>(wpr) * sizeof(uint64_t));
                skippedFillRows.fetch_add(1, std::memory_order_relaxed);
                done += W;
                continue; // 该行完全来自缓存 ⇒ 不收集新条目（与既有「命中段不收集」语义一致）
            }
            // v3.4.13：本块不负责收集的尾部行（属下一块锚点行域）⇒ 本行不收集条目
            const bool rowCollects =
                wantNew && !(absZ >= opt.collectSkipZ0 && absZ <= opt.collectSkipZ1);
            scanOneRow(absZ, segs, localNew, rowCollects);
            done += W;
        }
        chunksDone.fetch_add(done, std::memory_order_relaxed);
        if (!localNew.empty()) { // v3.4.13：收集与否已逐行判定（wantNew 仅作整块开关）
            auto& dst = threadNew[static_cast<size_t>(tid)];
            dst.insert(dst.end(), localNew.begin(), localNew.end());
        }
        const int64_t cd = chunksDone.load(std::memory_order_relaxed);
        const double el = elapsedMs();
        const double speed = el > 0.0 ? (static_cast<double>(cd) / el) * 1000.0 : 0.0;
        const double eta = speed > 1.0 ? (static_cast<double>(totalChunks - cd) / speed) : 0.0;
        scanEm.report(cd, totalChunks, speed, eta);
    };

#ifdef _OPENMP
    if (!sampleScan && nthreads > 1) {
#pragma omp parallel for schedule(dynamic, 4)
        for (int64_t t = 0; t < nTasks; ++t) {
            scanTask(t);
        }
    } else
#endif
    {
        if (!sampleScan) {
            for (int64_t t = 0; t < nTasks; ++t) scanTask(t);
        }
    }
    } // v3.2.0：CPU 扫描块结束（gpuStage==1 时跳过本块）

    if (cancel.load(std::memory_order_relaxed)) {
        out.cancelled = true;
        out.stats.totalMs = elapsedMs();
        return;
    }
    out.stats.scannedChunks = sampleScan ? sampleChunks : totalChunks;
    out.stats.scanMs = elapsedMs();
    if (kScanProfileOn && g_profGpuChunks.load(std::memory_order_relaxed) > 0) {
        const int64_t ck = g_profGpuChunks.load(std::memory_order_relaxed);
        const int64_t sc = g_profGpuScanCycles.load(std::memory_order_relaxed);
        const int64_t ic = g_profGpuIntegrateCycles.load(std::memory_order_relaxed);
        std::fprintf(stderr, "[profile-gpu] 内核+回读 %.3f ns/区块 | 逐行集成 %.3f ns/区块 | 区块 %lld\n",
                     sc / 4.4 / static_cast<double>(ck), ic / 4.4 / static_cast<double>(ck),
                     static_cast<long long>(ck));
        g_profGpuScanCycles.store(0, std::memory_order_relaxed);
        g_profGpuIntegrateCycles.store(0, std::memory_order_relaxed);
        g_profGpuChunks.store(0, std::memory_order_relaxed);
    }
    if (kScanProfileOn) {
        const int64_t cyc = g_profKernelCycles.load(std::memory_order_relaxed);
        const int64_t chk = g_profKernelChunks.load(std::memory_order_relaxed);
        const double ghz = 4.4;   // 5600 典型全核/单核睿频，仅用于换算
        std::fprintf(stderr, "[profile] 行内核: %.3f ns/区块（%.2f 周期/区块，%lld 区块），扫描总 %.3f ns/区块\n",
                     chk > 0 ? cyc / ghz / static_cast<double>(chk) : 0.0,
                     chk > 0 ? static_cast<double>(cyc) / static_cast<double>(chk) : 0.0,
                     static_cast<long long>(chk),
                     out.stats.scannedChunks > 0 ? out.stats.scanMs * 1e6 / static_cast<double>(out.stats.scannedChunks) : 0.0);
        g_profKernelCycles.store(0, std::memory_order_relaxed);
        {
            const int64_t mc = g_profDensMaskCycles.load(std::memory_order_relaxed);
            const int64_t rows = g_profDensRows.load(std::memory_order_relaxed);
            if (rows > 0) {
                std::fprintf(stderr, "[profile] 密度匹配（行内向量段）: %lld 行，共 %.2f ms 线程时间，平均 %.2f us/行\n",
                             static_cast<long long>(rows), mc / 4.4e6, mc / 4.4e3 / static_cast<double>(rows));
            }
            g_profDensMaskCycles.store(0, std::memory_order_relaxed);
            g_profDensRows.store(0, std::memory_order_relaxed);
            std::fprintf(stderr, "[profile] 命中记录：入榜 %lld 次，剪枝 %lld 次\n",
                         static_cast<long long>(g_profHits.load(std::memory_order_relaxed)),
                         static_cast<long long>(g_profPruned.load(std::memory_order_relaxed)));
            g_profHits.store(0, std::memory_order_relaxed);
            g_profPruned.store(0, std::memory_order_relaxed);
        }
        g_profKernelChunks.store(0, std::memory_order_relaxed);
        std::fflush(stderr);
    }
    // v3.3.8（任务 3+5 第 3 步）：统计「整行由缓存位图直取」的填充点数（计数口径不变）
    out.cacheHitPoints = skippedFillRows.load(std::memory_order_relaxed) * W;
    out.cacheFillSkipped = (skippedFillRows.load(std::memory_order_relaxed) >= H);
    if (std::getenv("SLIME_DEBUG")) {
        std::fprintf(stderr, "[engine] scan done rows=%lld collectNew=%d fillSkippedRows=%lld\n",
                     static_cast<long long>(H), collectNew ? 1 : 0,
                     static_cast<long long>(skippedFillRows.load(std::memory_order_relaxed)));
        std::fflush(stderr);
    }
    if (collectNew) {
        for (auto& v : threadNew) {
            out.newCacheChunks.insert(out.newCacheChunks.end(), v.begin(), v.end());
        }
        if (std::getenv("SLIME_DEBUG")) {
            std::fprintf(stderr, "[engine] newCache merged=%zu\n", out.newCacheChunks.size());
            std::fflush(stderr);
        }
    }
    scanEm.finish(); // 0.75
    out.stats.fillMs = out.stats.scanMs; // v3.3.8（E3）：填充阶段 = 扫描/判定阶段

    // v3.3.9（任务书 1.1）：缓存构建模式 —— 只做史莱姆判定 + 写缓存，**跳过整个匹配阶段**
    //（这就是「应比搜索快 3~4 倍」的来源；也是唯一允许写缓存的模式，见 1.2）。
    // 计数口径：整行由缓存位图直取的行不计入「本次新记录」，只计入「已覆盖跳过」。
    if (opt.buildMode) {
        const int64_t coveredRows = skippedFillRows.load(std::memory_order_relaxed);
        const int64_t recorded = (H - coveredRows) * W;
        if (opt.liveRecorded) opt.liveRecorded->fetch_add(recorded, std::memory_order_relaxed);
        if (opt.liveSkippedCovered) opt.liveSkippedCovered->fetch_add(coveredRows * W, std::memory_order_relaxed);
        out.stats.windowsEvaluated = 0;
        out.stats.matchMs = 0.0;
        out.stats.totalMs = elapsedMs();
        out.ok = !cancel.load(std::memory_order_relaxed);
        if (cancel.load(std::memory_order_relaxed)) out.cancelled = true;
        return;
    }

    // v3.3.8（任务 6 / E3-1「只填充」）：调试开关 —— 只测填充阶段，跳过整个匹配阶段。
    // 仅用于收益分解实验（不改产品行为）：设置了 SLIME_FILL_ONLY 时结果集为空、windowsEvaluated=0。
    if (std::getenv("SLIME_FILL_ONLY")) {
        out.stats.windowsEvaluated = 0;
        out.stats.matchMs = out.stats.fillMs;
        out.stats.totalMs = elapsedMs();
        out.ok = !cancel.load(std::memory_order_relaxed);
        if (cancel.load(std::memory_order_relaxed)) out.cancelled = true;
        return;
    }

    // ---- 2. 匹配阶段 ----
    // （strict / algoDensityOnly / sampleAlgo 已在函数开头定义，扫描阶段共用）
    std::vector<VariantMasks> variants;
    if (strict) {
        variants.push_back(makeVariant(opt.pattern));
        if (!opt.directionLocked) {
            Pattern p = opt.pattern;
            for (int i = 0; i < 3; ++i) {
                p = p.rotated();
                variants.push_back(makeVariant(p));
            }
        }
    }

    // 窗口锚点范围（各变体）
    struct AD { int64_t axMin, axMax, azMin, azMax, nAx, nAz; };
    std::vector<AD> dims;
    int64_t totalWindows = 0;
    const auto addDims = [&](int64_t wSize, int64_t hSize) {
        AD d;
        d.axMin = x0;
        d.axMax = static_cast<int64_t>(opt.maxX) - wSize + 1;
        d.azMin = z0;
        d.azMax = static_cast<int64_t>(opt.maxZ) - hSize + 1;
        d.nAx = std::max<int64_t>(0, d.axMax - d.axMin + 1);
        d.nAz = std::max<int64_t>(0, d.azMax - d.azMin + 1);
        totalWindows += d.nAx * d.nAz;
        dims.push_back(d);
    };
    if (strict) {
        for (const auto& v : variants) addDims(v.cols, v.rows);
    } else {
        addDims(opt.gridSize, opt.gridSize);
    }

    Emitter matchEm(progress, kMatchFrac0, kMatchFrac1, false);
    // v3.10.10.3：忽略阈值（最优 N 个）在精确密度模式下复用本内核（计数 TopN，AVX2/多线程/分块）
    const bool countMode = false;   // v3.10.10.3 试验已回退（保留占位避免误用）
    TopNHeap heap(opt.targetCount);
    const double effectiveMinDensity = opt.minDensity;
    int64_t totalFound = 0;
    bool earlyStop = false;
    // v3.10.5（用户反馈：数量够就停没生效）：提前停止只有在「先扫近处」时才有意义。
    //   用户没勾「由近及远」时，这里自动按距离排序行（仅影响扫描顺序，结果仍为最近 N）。
    // v3.10.10.2：图案模式仅按用户勾选「由近及远」排序行 —— 自动排序会禁掉图案 AVX2 快路径
    //   （stop+smart 时 20k² 从 92 ms 劣化到 858 ms）。密度模式保持自动（提前停止需要近处优先）。
    const bool smartOrder = opt.smartRange || (opt.stopAtTarget && !strict);
    std::atomic<int64_t> winDone{ 0 };

    // 去重集合（仅旋转模式需要；哈希集避免 O(n²) 线性查找）
    struct QuadKey {
        int a, b, c, d;
        bool operator==(const QuadKey& o) const noexcept { return a == o.a && b == o.b && c == o.c && d == o.d; }
    };
    struct QuadHash {
        size_t operator()(const QuadKey& k) const noexcept
        {
            uint64_t h = static_cast<uint64_t>(static_cast<uint32_t>(k.a));
            h = h * 0x100000001B3ULL ^ static_cast<uint64_t>(static_cast<uint32_t>(k.b));
            h = h * 0x100000001B3ULL ^ static_cast<uint64_t>(static_cast<uint32_t>(k.c));
            h = h * 0x100000001B3ULL ^ static_cast<uint64_t>(static_cast<uint32_t>(k.d));
            return static_cast<size_t>(h);
        }
    };
    std::unordered_set<QuadKey, QuadHash> seen;

    const auto reportWin = [&](int64_t doneWin) {
        winDone.store(doneWin, std::memory_order_relaxed);
        matchEm.report(doneWin, totalWindows, 0.0, 0.0);
    };

    int64_t variantBase = 0;
    int64_t globalWinDone = 0;
    int64_t totalSkipped = 0; // v3.3.0：被剪枝/未采样而未评估的窗口数（分层/估算模式）
    // v3.3.8.1（任务书 1.1）：缓存优先阶段凑够目标数后「立即停」是否触发
    int64_t slideUsTotal = 0; // v3.3.8（E3）：跨变体累加的滑窗耗时（微秒）

    for (size_t vi = 0; vi < dims.size() && !earlyStop; ++vi) {
        if (strict && variants[vi].allIgnore) { variantBase += dims[vi].nAx * dims[vi].nAz; continue; }
        const AD& d = dims[vi];
        if (d.nAx <= 0 || d.nAz <= 0) { variantBase += 0; continue; }
        const int wSize = strict ? variants[vi].cols : opt.gridSize;
        const int hSize = strict ? variants[vi].rows : opt.gridSize;

        // 锚点空间起点（窗口中心对准查询起点）
        const double sxA = static_cast<double>(opt.startX) - (wSize - 1) / 2.0;
        const double szA = static_cast<double>(opt.startZ) - (hSize - 1) / 2.0;

        // 行顺序（v3.1.1：分块路径经 g_anchorMaxAZ 限制本块锚点行域）
        std::vector<int64_t> azRows;
        azRows.reserve(static_cast<size_t>(d.nAz));
        for (int64_t i = 0; i < d.nAz; ++i) {
            const int64_t az = d.azMin + i;
            if (az <= g_anchorMaxAZ) azRows.push_back(az);
        }
        // 严格图案 + 提前停止时保持「逐行按距离」排序（行带排序会改变提前停止的截断点，
        //   与参考实现的最近 N 名不一致；密度模式已实测一致）。
        if (smartOrder && !(strict && opt.stopAtTarget)) {
            // v3.9.2：智能范围按「行带」排序（带高 = stopBlockRows()，与滑窗块严格对齐）。
            //   带内连续升序 ⇒ 滑窗路径可用；带间按「带到起点的距离」由近到远。
            //   注意：滑窗路径要求「每个块内行号连续」，所以带高必须与滑窗块大小一致。
            const int64_t smartBand = smartBandRows();
            std::vector<std::pair<double, std::vector<int64_t>>> bands;
            for (size_t i = 0; i < azRows.size(); i += static_cast<size_t>(smartBand)) {
                const size_t j = (std::min)(azRows.size(), i + static_cast<size_t>(smartBand));
                std::vector<int64_t> b(azRows.begin() + static_cast<std::ptrdiff_t>(i),
                                       azRows.begin() + static_cast<std::ptrdiff_t>(j));
                const double lo = static_cast<double>(b.front());
                const double hi = static_cast<double>(b.back());
                double d = 0.0;
                if (szA < lo) d = lo - szA;
                else if (szA > hi) d = szA - hi;
                bands.emplace_back(d, std::move(b));
            }
            std::stable_sort(bands.begin(), bands.end(),
                             [](const auto& x, const auto& y) { return x.first < y.first; });
            azRows.clear();
            for (const auto& bd : bands) azRows.insert(azRows.end(), bd.second.begin(), bd.second.end());
        }

        const auto windowsDoneRow = d.nAx;
        bool rowCancelled = false;

        // v3.3.0（3.3）：分层搜索块表（仅密度模式；智能范围/严格模式自动退化为精确）
        //
        // 剪枝判据（块级必要条件，可证无损）：
        //   窗口 ⊆ 覆盖它的至多 4 个粗块之并 ⇒ 窗口内史莱姆数 ≤ 这些粗块计数之和（上界）。
        //   若上界 < 块级门限（= 窗口所需史莱姆数 × 阈值比例），该锚点必然达不到阈值，
        //   可安全跳过 —— 结果与精确模式 100% 一致（ratio ≤ 1 时零漏检）。
        //   ratio > 1 时门限高于必要条件 → 剪枝更激进、可能漏检（界面显示警告条）。
        const bool layeredActive = !strict && opt.algorithm == Algorithm::Layered && !smartOrder;
        // 粗块不得小于窗口：否则块计数只覆盖窗口的一部分，上界不再成立。
        const int layerL = std::max(std::clamp(opt.layerBlock, 8, 64), opt.gridSize);

        // 整数门限 densNeed = ceil(minDensity·g²)：精确/分层/估算（精算采样）共用；
        // 保守模式（Brute）刻意不使用快路径以保持 v3.2.1 字面语义。
        const bool fastDensity = !strict && opt.algorithm != Algorithm::Brute;
        int densNeed = 0;
        int densThr = 0;   // v3.8.0：与 densityWindowMeets 严格等价的整数门限（命中 ⟺ 计数 ≥ densThr）
        if (fastDensity) {
            const double t = effectiveMinDensity * static_cast<double>(opt.gridSize) *
                             static_cast<double>(opt.gridSize);
            densNeed = static_cast<int>(std::ceil(t));
            // 浮点判定是「计数单调」的（计数越大越容易满足），因此存在唯一整数门限：
            //   直接扫描 0..g² 找到第一个满足 densityWindowMeets 的计数，之后全部用整数比较，
            //   既省掉每个候选的浮点调用，也让 SIMD 掩码门限可以取到恰好精确的值。
            const int g2 = opt.gridSize * opt.gridSize;
            densThr = g2;
            for (int c = 0; c <= g2; ++c) {
                if (densityWindowMeets(c, opt.gridSize, effectiveMinDensity)) {
                    densThr = c;
                    break;
                }
            }
        }

        int64_t layeredNBx = 0;
        int layerNeed = 0;
        std::vector<int32_t> layeredCnt;
        bool layeredPrune = false; // 剪枝是否有效（无效则回退滑动路径，与精确模式等价同速）
        if (layeredActive) {
            layeredNBx = (W + layerL - 1) / layerL;
            const int64_t nBz = (H + layerL - 1) / layerL;
            layeredCnt.assign(static_cast<size_t>(layeredNBx * nBz), 0);
            for (int64_t rz = 0; rz < H; ++rz) {
                const int64_t brow = (rz / layerL) * layeredNBx;
                for (int64_t bc = 0; bc < layeredNBx; ++bc) {
                    const int64_t xa0 = x0 + bc * layerL;
                    const int len = static_cast<int>(std::min<int64_t>(layerL, x0 + W - xa0));
                    layeredCnt[static_cast<size_t>(brow + bc)] +=
                        static_cast<int32_t>(grid.countBits(z0 + rz, xa0, len));
                }
            }
            layerNeed = std::max(1, static_cast<int>(
                                        std::ceil(static_cast<double>(densNeed) * opt.layerRatio)));
            // 抽样估计剪枝有效率：若绝大多数段都通过判定（剪枝无效），分层只会增加开销，
            // 此时回退到滑动路径（结果与精确模式完全一致，速度与精确模式相当）。
            int64_t pass = 0, total = 0;
            const int64_t step = std::max<int64_t>(1, d.nAz / 32);
            for (int64_t i = 0; i < static_cast<int64_t>(azRows.size()); i += step) {
                const int64_t az = azRows[static_cast<size_t>(i)];
                const int64_t bz0 = (az - z0) / layerL;
                const int64_t bz1 = (az + opt.gridSize - 1 - z0) / layerL;
                for (int64_t bx = 0; bx < layeredNBx; ++bx) {
                    int v = 0;
                    for (int64_t bz = bz0; bz <= bz1; ++bz) {
                        v += layeredCnt[static_cast<size_t>(bz * layeredNBx + bx)];
                    }
                    ++total;
                    if (v >= layerNeed) ++pass;
                }
            }
            layeredPrune = (total > 0) && (static_cast<double>(pass) / static_cast<double>(total) < 0.9);
            if (std::getenv("SLIME_DEBUG")) {
                std::fprintf(stderr, "[engine] layered L=%d densNeed=%d layerNeed=%d NBx=%lld prune=%d pass=%.3f\n",
                             layerL, densNeed, layerNeed, static_cast<long long>(layeredNBx),
                             layeredPrune ? 1 : 0,
                             total > 0 ? static_cast<double>(pass) / static_cast<double>(total) : 1.0);
                std::fflush(stderr);
            }
        }
        int64_t variantSkipped = 0;

        // v3.2.0：单行全部锚点窗口的评估；结果推入调用方提供的堆（并行时线程局部）。
        // 语义与 v3.2.1 完全一致：匹配计数含被堆容量拒绝的窗口；旋转去重仅顺序路径使用。
        // v3.3.0：窗口评估提取为带 az 参数的公共 lambda，供逐行路径与滑动块共用。
        const auto evalWindowAt = [&](int64_t az, int64_t xa, int preCount) -> bool {
            if (strict) {
                const VariantMasks& v = variants[vi];
                for (int rr = 0; rr < v.rows; ++rr) {
                    const uint64_t seg = grid.segmentBits(az + rr, xa, v.cols);
                    if ((seg & v.enf[static_cast<size_t>(rr)]) != v.grn[static_cast<size_t>(rr)]) return false;
                }
                return true;
            } else {
                const int g = opt.gridSize;
                int cnt = preCount;
                if (cnt < 0) {
                    cnt = 0;
                    for (int rr = 0; rr < g; ++rr) cnt += grid.countBits(az + rr, xa, g);
                }
                if (fastDensity) return cnt >= densThr;   // v3.8.0：精确整数门限
                return densityWindowMeets(cnt, g, effectiveMinDensity);
            }
        };

        // v3.3.6（任务书 三/调整 3）：实时命中计数（relaxed 原子自增，不参与判定）
        // v3.8.0：改成线程本地累加 + 每 4096 次批量提交 —— 命中密集时原来每个命中都要打一次
        //   全局原子变量，12 线程会互相踩缓存行（实测命中成本 ~125 ns 的主要来源之一）。
        int64_t livePending = 0;
        const auto bumpLiveMatches = [&]() {
            if (opt.liveMatches && ++livePending >= 4096) {
                opt.liveMatches->fetch_add(livePending, std::memory_order_relaxed);
                livePending = 0;
            }
        };
        const auto flushLiveMatches = [&]() {
            if (opt.liveMatches && livePending != 0) {
                opt.liveMatches->fetch_add(livePending, std::memory_order_relaxed);
                livePending = 0;
            }
        };

        // v3.8.0：命中记录路径重写（原实现每次命中约 125 ns，是命中密集场景的主要成本）：
        //   1) 先用「整数平方距离」判断能不能进榜，不能就直接返回（省掉 hypot / MatchResult / 堆操作）；
        //   2) hypot 只在真正要入堆时算；
        //   3) 实时命中计数改线程本地批量提交（见 bumpLiveMatches）。
        const auto recordMatchAt = [&](int64_t az, int64_t xa, TopNHeap& h, int64_t& foundCnt, int preCount,
                                       float densityOverride = -1.0f) {
            // v3.10.10.3：计数模式沿用 runBestDensity 的平局口径（光栅 z 优先）
            const int64_t rank = h.byCount()
                                     ? (az - d.azMin) * d.nAx + (xa - d.axMin)
                                     : variantBase + (xa - d.axMin) * d.nAz + (az - d.azMin);
            // 候选窗口中心相对起点的 2 倍偏移（一定是整数：中心带 .0 或 .5）
            const int64_t off2x = static_cast<int64_t>(xa * 2 + (wSize - 1)) -
                                  2 * static_cast<int64_t>(opt.startX);
            const int64_t off2z = static_cast<int64_t>(az * 2 + (hSize - 1)) -
                                  2 * static_cast<int64_t>(opt.startZ);
            const int64_t d2 = off2x * off2x + off2z * off2z;
            const bool dedupe = strict && !opt.directionLocked;
            if (!h.byCount() && !dedupe && h.full() && d2 > h.worstD2()) {
                ++foundCnt;          // 不影响命中总数统计
                bumpLiveMatches();
                if (kScanProfileOn) g_profPruned.fetch_add(1, std::memory_order_relaxed);
                return;              // 进不了榜：跳过 hypot / 结果构造 / 堆操作
            }
            if (kScanProfileOn) g_profHits.fetch_add(1, std::memory_order_relaxed);
            const double dist = 0.5 * std::sqrt(static_cast<double>(d2));
            MatchResult mr;
            mr.minX = static_cast<int>(xa);
            mr.maxX = static_cast<int>(xa + wSize - 1);
            mr.minZ = static_cast<int>(az);
            mr.maxZ = static_cast<int>(az + hSize - 1);
            if (densityOverride >= 0.0f) {
                mr.density = densityOverride; // v3.3.0：估算模式（采样估计密度）
            } else if (!strict) {
                int cnt = preCount;
                if (cnt < 0) {
                    cnt = 0;
                    for (int rr = 0; rr < hSize; ++rr) cnt += grid.countBits(az + rr, xa, wSize);
                }
                mr.density = static_cast<float>(cnt) / static_cast<float>(static_cast<int64_t>(wSize) * wSize);
            }
            Scored sc{ mr, dist, rank, d2, 0 };
            if (h.byCount()) {
                sc.count = static_cast<int>(std::lround(mr.density * static_cast<float>(wSize) * wSize));
            }
            if (!strict || opt.directionLocked) {
                h.push(sc);
                ++foundCnt;
                bumpLiveMatches();
            } else {
                const QuadKey key{ mr.minX, mr.maxX, mr.minZ, mr.maxZ };
                if (seen.insert(key).second) {
                    h.push(sc);
                    ++foundCnt;
                    bumpLiveMatches();
                }
            }
        };

        const auto emitMatchAt = [&](int64_t az, int64_t xa, TopNHeap& h, int64_t& foundCnt, int preCount) {
            if (!evalWindowAt(az, xa, preCount)) return;
            recordMatchAt(az, xa, h, foundCnt, preCount);
        };

        // v3.3.0（3.2）：严格匹配 AVX2 位并行 —— 行内 4 个连续窗口一次判定。
        // 注意：标量实现带“首行不符即退出”的早退，在低行通过率（如全绿密集图案）下极快；
        // SIMD 批处理放弃早退，仅在“行多且每行约束少（行通过率高）”时占优。
        // 因此默认按图案特征自适应，可用 SLIME_AVX2_STRICT=0/1 强制。
        const bool avx2 = cpuHasAvx2();
        const bool strictSimdOk = [&]() {
            if (!(strict && avx2 && !smartOrder)) return false;
            const char* env = std::getenv("SLIME_AVX2_STRICT");
            // 默认关闭：标量内联路径带“首个不符行即退出”的早退，实测普遍快于 SIMD 批处理；
            // 仅在显式要求（=1）时启用 SIMD（用于特定图案/对照测试）。
            if (!env || *env != '1') return false;
            const VariantMasks& v = variants[vi];
            return v.rows >= 8;
        }();
        const auto evalStrictRowSimd = [&](int64_t az, TopNHeap& h, int64_t& foundCnt) {
            const VariantMasks& v = variants[vi];
            const int rows = v.rows;
            const int cols = v.cols;
            const uint64_t colMask = (cols >= 64) ? ~0ULL : ((1ULL << cols) - 1ULL);
            const int wpr = static_cast<int>(grid.wordsPerRow());
            const uint64_t* rp[64];
            for (int rr = 0; rr < rows; ++rr) rp[rr] = grid.rowBits(az + rr);
            static thread_local std::vector<unsigned char> tFlags;
            const size_t nFlags = static_cast<size_t>(d.axMax - d.axMin + 1);
            if (tFlags.size() < nFlags) tFlags.resize(nFlags);
            strictRowAvx2(rp, v.enf.data(), v.grn.data(), rows, wpr, static_cast<int>(x0),
                          static_cast<int>(d.axMin), static_cast<int>(d.axMax), colMask, tFlags.data());
            for (size_t i = 0; i < nFlags; ++i) {
                if (tFlags[i]) recordMatchAt(az, d.axMin + static_cast<int64_t>(i), h, foundCnt, -1);
            }
        };
        // v3.3.0（3.2 之二）：严格匹配「字级位并行」判定 —— 一次 64 个锚点。
        //
        // 原理：图案行 rr 的约束可写成 (窗口位 & enf[rr]) == grn[rr]；对 64 个连续锚点，
        // 第 cc 列的位向量 = 该行位图整体右移 (锚点基址 + cc) 后的同一字（跨字用两个字拼接），
        // 因此每个受约束列只需 1 次移位 + 1 次与/与非，再对行做与即可得到 64 个锚点的命中掩码。
        // 成本 ≈ 受约束列数 × 图案行数 × ~3 次 64 位运算 / 64 锚点（2×2 全绿 ≈ 0.2 op/锚点），
        // 显著低于标量早退路径的“每锚点至少 1 次位段取值 + 比较”，且无需 AVX2（纯 64 位运算）。
        // 与标量路径结果逐位一致（单元测试 [3][9][16] 全量对照），可用 SLIME_BITS_STRICT=0 关闭。
        const bool bitsStrictOk = [&]() {
            // v3.10.12.2：允许「由近及远」排序下也走字级位并行快路径 —— 它按行独立评估，
            //   与行顺序无关；旧限制导致图案 + 智能范围从 ~92 ms 掉到 ~557 ms。
            if (!strict) return false;
            if (opt.algorithm != Algorithm::Exact) return false; // 保守模式保持 v3.2.1 字面实现
            const VariantMasks& v = variants[vi];
            if (v.cols > 64 || v.rows < 1 || v.rows > 64) return false;
            if (v.allIgnore) return false;
            const char* env = std::getenv("SLIME_BITS_STRICT");
            if (env && *env == '0') return false;
            return true;
        }();
        const auto evalStrictRowBits = [&](int64_t az, TopNHeap& h, int64_t& foundCnt) {
            const VariantMasks& v = variants[vi];
            const int rows = v.rows;
            const int cols = v.cols;
            const int64_t wpr = static_cast<int64_t>(grid.wordsPerRow());
            const uint64_t* rp[64];
            for (int rr = 0; rr < rows; ++rr) rp[rr] = grid.rowBits(az + rr);
            // 每行受约束列（enf 中的置位），预先把列号列表缓存（避免逐字重复扫描）
            const int64_t relMin = d.axMin - x0;
            const int64_t relMax = d.axMax - x0;
            const int64_t wBegin = relMin >> 6;
            const int64_t wEnd = relMax >> 6;
            const uint64_t tailMask = (relMax & 63) == 63 ? ~0ULL : ((1ULL << ((relMax & 63) + 1)) - 1ULL);
            // v3.6.20：全绿矩形（所有行都是同一段连续绿、且无白格）时，把「逐列移位 AND」
            //   换成「倍增前缀 AND」（log2 L 次移位），大图案可省数倍工作。
            bool runFast = false;
            int rc0 = 0, rlen = 0;
            // v3.6.20：连续绿长度 >= 16 时倍增才划算（12x12 实测会慢 13%，16x16 起变快）
            constexpr int kRunFastMinLen = 16;
            {
                const uint64_t e0 = v.enf[0];
                bool uniform = (e0 != 0);
                for (int rr = 1; rr < rows && uniform; ++rr) {
                    const uint64_t e = v.enf[static_cast<size_t>(rr)];
                    if (e != e0 || (v.grn[static_cast<size_t>(rr)] & e) != e) uniform = false;
                }
                static const bool runFastEnabled = []
                {
                    const char* e = std::getenv("SLIME_RUN_FAST");
                    return !(e && *e == '0');
                }();
                if (runFastEnabled && uniform && (v.grn[0] & e0) == e0 && cols <= 64) {
                    int c0 = 0;
                    while (c0 < cols && ((e0 >> c0) & 1ULL) == 0) ++c0;
                    int c1 = c0;
                    while (c1 + 1 < cols && ((e0 >> (c1 + 1)) & 1ULL) != 0) ++c1;
                    if (c0 < cols && c1 + 1 <= 64) {
                        const int len = c1 - c0 + 1;
                        const uint64_t want = (len >= 64) ? ~0ULL : (((1ULL << len) - 1ULL) << c0);
                        if (e0 == want && len >= kRunFastMinLen) {
                            runFast = true;
                            rc0 = c0;
                            rlen = len;
                        }
                    }
                }
            }
            for (int64_t w = wBegin; w <= wEnd; ++w) {
                uint64_t acc = ~0ULL;
                if (runFast) {
                    for (int rr = 0; rr < rows && acc; ++rr) {
                        const uint64_t* row = rp[rr];
                        // 128 位窗口（从 rc0 开始），保证逻辑右移时能跨字取到下一字的位
                        const uint64_t r0 = row[w];
                        const uint64_t r1 = (w + 1 < wpr) ? row[w + 1] : 0ULL;
                        const uint64_t r2 = (w + 2 < wpr) ? row[w + 2] : 0ULL;
                        uint64_t lo, hi;
                        if (rc0 == 0) {
                            lo = r0;
                            hi = r1;
                        } else {
                            lo = (r0 >> rc0) | (r1 << (64 - rc0));
                            hi = (r1 >> rc0) | (r2 << (64 - rc0));
                        }
                        const auto shrAnd = [&](int k) {
                            uint64_t slo, shi;
                            if (k < 64) {
                                slo = (lo >> k) | (hi << (64 - k));
                                shi = hi >> k;
                            } else {
                                slo = hi >> (k - 64);
                                shi = 0ULL;
                            }
                            lo &= slo;
                            hi &= shi;
                        };
                        int k = 1;
                        while (k * 2 <= rlen) {
                            shrAnd(k);
                            k *= 2;
                        }
                        if (k < rlen) shrAnd(rlen - k);
                        acc &= lo;
                    }
                } else {
                for (int rr = 0; rr < rows && acc; ++rr) {
                    const uint64_t e = v.enf[static_cast<size_t>(rr)];
                    if (!e) continue;
                    const uint64_t* row = rp[rr];
                    uint64_t ok = ~0ULL;
                    for (int cc = 0; cc < cols; ++cc) {
                        if (((e >> cc) & 1ULL) == 0) continue;
                        const int64_t sw = w + (cc >> 6);
                        const int sh = cc & 63;
                        uint64_t bits;
                        if (sh == 0) {
                            bits = (sw < wpr) ? row[sw] : 0ULL;
                        } else {
                            bits = (sw < wpr) ? (row[sw] >> sh) : 0ULL;
                            if (sw + 1 < wpr) bits |= row[sw + 1] << (64 - sh);
                        }
                        if (((v.grn[static_cast<size_t>(rr)] >> cc) & 1ULL) != 0) ok &= bits;
                        else ok &= ~bits;
                    }
                    acc &= ok;
                }
                }
                if (w == wBegin) acc &= ~((1ULL << (relMin & 63)) - 1ULL); // 起始锚点之前的位置
                if (w == wEnd) acc &= tailMask;                            // 结束锚点之后的位置
                while (acc) {
                    const int b = std::countr_zero(acc);
                    const int64_t xa = x0 + (w << 6) + b;
                    recordMatchAt(az, xa, h, foundCnt, -1);
                    acc &= acc - 1;
                }
            }
        };
        // v3.3.1（任务书 3.1）：AVX2 批量窗口密度计数内核（一次算出一整行锚点的 g×g 计数）。
        // 用于「无滑动资格」的密度路径（提前停止 / 分层未剪枝段等）——此前每锚点 g 次 countBits。
        // 内核与朴素实现逐位一致（kernel_selftest 全量比对），回退时自动走原逻辑。
        // SLIME_DENS_KERNEL=0 可关闭本优化（同机 A/B 对照用）。
        const char* densKernelEnv = std::getenv("SLIME_DENS_KERNEL");
        const bool densKernelOk = !strict && opt.gridSize >= 1 && opt.gridSize <= 512 &&
                                  !(densKernelEnv && *densKernelEnv == '0');
        static thread_local std::vector<uint32_t> tRowCounts;
        const int64_t rowAnchorCount = d.axMax - d.axMin + 1;
        const auto fillRowCounts = [&](int64_t az) -> const uint32_t* {
            if (!densKernelOk || rowAnchorCount <= 0) return nullptr;
            const int g = opt.gridSize;
            // v3.6.16：门限 = g²（即最小密度 1.0，要求窗口内全是史莱姆区块）时，
            //   不需要逐窗计数 —— g 行纵向 AND + g 列横向 AND 即可，实测快 20~30 倍。
            //   结果与通用路径逐位一致（命中窗计数恒为 g²，其余为 0）。
            if (fastDensity && densThr == g * g) {
                if (static_cast<int64_t>(tRowCounts.size()) < rowAnchorCount) {
                    tRowCounts.resize(static_cast<size_t>(rowAnchorCount));
                }
                std::fill(tRowCounts.begin(), tRowCounts.begin() + rowAnchorCount, 0u);
                static thread_local std::vector<uint64_t> tAnd;
                const int64_t wpr = static_cast<int64_t>(grid.wordsPerRow());
                if (static_cast<int64_t>(tAnd.size()) < wpr) tAnd.resize(static_cast<size_t>(wpr));
                for (int64_t w = 0; w < wpr; ++w) {
                    uint64_t v = ~0ULL;
                    for (int rr = 0; rr < g; ++rr) v &= grid.rowBits(az + rr)[w];
                    tAnd[static_cast<size_t>(w)] = v;
                }
                const int64_t relBase = d.axMin - x0;              // 锚点在位图中的起始位
                const int64_t wFirst = relBase >> 6;
                const int64_t wLast = (relBase + rowAnchorCount - 1) >> 6;
                for (int64_t w = wFirst; w <= wLast; ++w) {
                    const uint64_t cur = tAnd[static_cast<size_t>(w)];
                    const uint64_t nxt = (w + 1 < wpr) ? tAnd[static_cast<size_t>(w + 1)] : 0ULL;
                    uint64_t res = cur;
                    for (int dc = 1; dc < g; ++dc) res &= (cur >> dc) | (nxt << (64 - dc));
                    while (res) {
                        const int bit = static_cast<int>(std::countr_zero(res));
                        res &= res - 1;
                        const int64_t xa = x0 + w * 64 + bit;
                        const int64_t idx = xa - d.axMin;
                        if (idx >= 0 && idx < rowAnchorCount) tRowCounts[static_cast<size_t>(idx)] = static_cast<uint32_t>(g * g);
                    }
                }
                // v3.6.16：SLIME_DENS_VERIFY=1 时与通用计数逐锚点比对（排查用，默认关）
                static const bool verifyAll = []
                {
                    const char* e = std::getenv("SLIME_DENS_VERIFY");
                    return e && *e == '1';
                }();
                if (verifyAll) {
                    std::vector<uint32_t> ref(static_cast<size_t>(rowAnchorCount), 0u);
                    if (!densityWindowCountsAvx2(nullptr, g, static_cast<int64_t>(grid.wordsPerRow()),
                                                 d.axMin - x0, d.axMax - x0, nullptr)) {
                        // 传 nullptr 的调用会返回 false，这里改用逐窗标量统计
                        for (int64_t i = 0; i < rowAnchorCount; ++i) {
                            uint32_t c = 0;
                            const int64_t xa = d.axMin + i;
                            for (int rr = 0; rr < g; ++rr) c += static_cast<uint32_t>(grid.countBits(az + rr, xa, g));
                            ref[static_cast<size_t>(i)] = c;
                        }
                        int bad = 0;
                        for (int64_t i = 0; i < rowAnchorCount; ++i) {
                            const uint32_t a = tRowCounts[static_cast<size_t>(i)];
                            const uint32_t b = (ref[static_cast<size_t>(i)] == static_cast<uint32_t>(g * g))
                                                   ? static_cast<uint32_t>(g * g) : 0u;
                            if (a != b) ++bad;
                        }
                        if (bad > 0) std::fprintf(stderr, "[dens-verify] 行 az=%lld 不一致 %d 个锚点\n",
                                                  static_cast<long long>(az), bad);
                    }
                }
                return tRowCounts.data();
            }
            if (static_cast<int64_t>(tRowCounts.size()) < rowAnchorCount) {
                tRowCounts.resize(static_cast<size_t>(rowAnchorCount));
            }
            const uint64_t* rows[512];
            for (int rr = 0; rr < g; ++rr) rows[rr] = grid.rowBits(az + rr);
            if (!densityWindowCountsAvx2(rows, g, static_cast<int64_t>(grid.wordsPerRow()),
                                         d.axMin - x0, d.axMax - x0, tRowCounts.data())) {
                return nullptr;
            }
            return tRowCounts.data();
        };

        const auto evalRow = [&](int64_t az, TopNHeap& h, int64_t& foundCnt, int64_t& skipped) {
            int64_t inRow = 0;
            if (strict) {
                const VariantMasks& v = variants[vi];
                const int rows = v.rows;
                const int cols = v.cols;
                const uint64_t* enf = v.enf.data();
                const uint64_t* grn = v.grn.data();
                if (smartOrder) {
                    // 智能范围（中心向两侧）：保留通用路径
                    int64_t lo = std::max(d.axMin, static_cast<int64_t>(std::llround(sxA)));
                    int64_t hi = lo;
                    emitMatchAt(az, lo, h, foundCnt, -1);
                    ++inRow;
                    while (hi < d.axMax || lo > d.axMin) {
                        if (hi < d.axMax) { ++hi; emitMatchAt(az, hi, h, foundCnt, -1); ++inRow; }
                        if (lo > d.axMin) { --lo; emitMatchAt(az, lo, h, foundCnt, -1); ++inRow; }
                        if ((inRow & 0x3FFF) == 0 && cancel.load(std::memory_order_relaxed)) break;
                    }
                } else {
                    // 光栅顺序：行内联判定（命中才记录，避免逐窗函数调用开销）
                    for (int64_t xa = d.axMin; xa <= d.axMax; ++xa) {
                        bool ok = true;
                        for (int rr = 0; rr < rows; ++rr) {
                            const uint64_t seg = grid.segmentBits(az + rr, xa, cols);
                            if ((seg & enf[rr]) != grn[rr]) {
                                ok = false;
                                break;
                            }
                        }
                        if (ok) recordMatchAt(az, xa, h, foundCnt, -1);
                        if ((++inRow & 0x3FFF) == 0 && cancel.load(std::memory_order_relaxed)) break;
                    }
                }
            } else if (smartOrder) {
                int64_t lo = std::max(d.axMin, static_cast<int64_t>(std::llround(sxA)));
                int64_t hi = lo;
                emitMatchAt(az, lo, h, foundCnt, -1);
                ++inRow;
                while (hi < d.axMax || lo > d.axMin) {
                    if (hi < d.axMax) { ++hi; emitMatchAt(az, hi, h, foundCnt, -1); ++inRow; }
                    if (lo > d.axMin) { --lo; emitMatchAt(az, lo, h, foundCnt, -1); ++inRow; }
                    if ((inRow & 0x3FFF) == 0 && cancel.load(std::memory_order_relaxed)) break;
                }
            } else if (layeredActive && layeredPrune) {
                // v3.3.0（3.3）：分层剪枝 —— 按“粗块列”成段判定（ratio ≤ 1 时为无损剪枝）。
                // 段内锚点的窗口完全落在该块列内，其上界 = 该列各粗块计数之和（一次求和
                // 可覆盖整段，避免逐锚点判定开销）；块边界处 g-1 列锚点用双列和判定。
                const int g = opt.gridSize;
                const int64_t bz0 = (az - z0) / layerL;
                const int64_t bz1 = (az + g - 1 - z0) / layerL;
                // v3.3.1：整行的 AVX2 批量窗口计数按需计算（本行若有段通过剪枝才计算，
                // 避免在“整行被剪掉”的稀疏场景为每行都做 O(W) 的计数，抵消分层剪枝收益）
                const uint32_t* layerRowCnts = nullptr;
                bool layerRowCntsDone = false;
                const auto rowCntsLazy = [&]() -> const uint32_t* {
                    if (!layerRowCntsDone) {
                        layerRowCnts = fillRowCounts(az);
                        layerRowCntsDone = true;
                    }
                    return layerRowCnts;
                };
                for (int64_t bx = 0; bx < layeredNBx; ++bx) {
                    int v = 0; // 该块列在窗口行域内的史莱姆总数（段内窗口上界）
                    for (int64_t bz = bz0; bz <= bz1; ++bz) {
                        v += layeredCnt[static_cast<size_t>(bz * layeredNBx + bx)];
                    }
                    const auto evalRange = [&](int64_t a, int64_t b) {
                        if (a > b) return;
                        const uint32_t* rowCnts = rowCntsLazy();
                        for (int64_t xa = a; xa <= b; ++xa) {
                            int cnt = 0;
                            if (rowCnts) {
                                cnt = static_cast<int>(rowCnts[xa - d.axMin]);
                            } else {
                                for (int rr = 0; rr < g; ++rr) cnt += grid.countBits(az + rr, xa, g);
                            }
                            const bool hit = (cnt >= densThr);
                            if (hit) recordMatchAt(az, xa, h, foundCnt, cnt);
                            if ((++inRow & 0x3FFF) == 0 && cancel.load(std::memory_order_relaxed)) return;
                        }
                    };
                    const int64_t segA = std::max(d.axMin, x0 + bx * layerL);
                    const int64_t segB = std::min(d.axMax, x0 + (bx + 1) * layerL - g);
                    if (v < layerNeed) {
                        if (segB >= segA) skipped += segB - segA + 1;
                    } else {
                        evalRange(segA, segB);
                        if (cancel.load(std::memory_order_relaxed)) break;
                    }
                    // 跨块边界锚点（窗口横跨 bx / bx+1 两列）
                    const int64_t bA = std::max(d.axMin, x0 + (bx + 1) * layerL - g + 1);
                    const int64_t bB = std::min(d.axMax, x0 + (bx + 1) * layerL - 1);
                    if (bA <= bB) {
                        int v2 = v;
                        if (bx + 1 < layeredNBx) {
                            for (int64_t bz = bz0; bz <= bz1; ++bz) {
                                v2 += layeredCnt[static_cast<size_t>(bz * layeredNBx + bx + 1)];
                            }
                        }
                        if (v2 < layerNeed) {
                            skipped += bB - bA + 1;
                        } else {
                            evalRange(bA, bB);
                            if (cancel.load(std::memory_order_relaxed)) break;
                        }
                    }
                }
            } else if (opt.algorithm == Algorithm::Sample) {
                // v3.3.0（3.5）：快速估算 —— 仅评估采样网格上的锚点，窗口密度按采样格点比例估计。
                // 锚点直接按步长迭代（避免逐锚点取模：整除是采样模式的主要开销来源）。
                const int S = std::clamp(opt.sampleStep, 1, 64);
                const int g = opt.gridSize;
                const int64_t offX = ((x0 - d.axMin) % S + S) % S;
                const int64_t xa0 = d.axMin + offX;
                const int64_t nSampled = (xa0 <= d.axMax) ? ((d.axMax - xa0) / S + 1) : 0;
                skipped += d.nAx - nSampled; // 未落在采样网格上的锚点
                if ((((az - z0) % S) + S) % S == 0) {
                    for (int64_t xa = xa0; xa <= d.axMax; xa += S) {
                        if (opt.sampleExact) {
                            // 精算采样：逐窗精算密度（扫描阶段已计算窗口覆盖的全部区块）
                            int cnt = 0;
                            for (int rr = 0; rr < g; ++rr) cnt += grid.countBits(az + rr, xa, g);
                            const bool hit = (cnt >= densThr);
                            if (hit) recordMatchAt(az, xa, h, foundCnt, cnt);
                        } else {
                            int hits = 0;
                            int samples = 0;
                            // 窗口内采样格点：行/列均以 S 为步长（锚点与采样网格对齐，故无需取模）
                            for (int rr = 0; rr < g; rr += S) {
                                const int64_t rz = az + rr;
                                for (int rx = 0; rx < g; rx += S) {
                                    ++samples;
                                    if (grid.isSlime(xa + rx, rz)) ++hits;
                                }
                            }
                            const double est = samples > 0 ? static_cast<double>(hits) / samples : 0.0;
                            if (est >= effectiveMinDensity) {
                                recordMatchAt(az, xa, h, foundCnt, hits, static_cast<float>(est));
                            }
                        }
                        if ((++inRow & 0x3FFF) == 0 && cancel.load(std::memory_order_relaxed)) break;
                    }
                }
            } else if (opt.algorithm == Algorithm::Brute) {
                // 保守模式：逐窗统计 + 浮点判定（v3.2.1 字面语义，内联）
                const int g = opt.gridSize;
                for (int64_t xa = d.axMin; xa <= d.axMax; ++xa) {
                    int cnt = 0;
                    for (int rr = 0; rr < g; ++rr) cnt += grid.countBits(az + rr, xa, g);
                    if (densityWindowMeets(cnt, g, effectiveMinDensity)) recordMatchAt(az, xa, h, foundCnt, cnt);
                    if ((++inRow & 0x3FFF) == 0 && cancel.load(std::memory_order_relaxed)) break;
                }
            } else {
                // 精确模式（无滑动资格：智能范围/提前停止）——整数快路径，判定结果与浮点一致
                // v3.3.1：优先使用 AVX2 批量计数内核（回退逐窗 countBits），结果逐位一致
                const int g = opt.gridSize;
                const uint32_t* rowCnts = fillRowCounts(az);
                for (int64_t xa = d.axMin; xa <= d.axMax; ++xa) {
                    int cnt = 0;
                    if (rowCnts) {
                        cnt = static_cast<int>(rowCnts[xa - d.axMin]);
                    } else {
                        for (int rr = 0; rr < g; ++rr) cnt += grid.countBits(az + rr, xa, g);
                    }
                    const bool hit =
                        fastDensity ? (cnt >= densNeed + 2 ||
                                       (cnt > densNeed - 2 && densityWindowMeets(cnt, g, effectiveMinDensity)))
                                    : densityWindowMeets(cnt, g, effectiveMinDensity);
                    if (hit) recordMatchAt(az, xa, h, foundCnt, cnt);
                    if ((++inRow & 0x3FFF) == 0 && cancel.load(std::memory_order_relaxed)) break;
                }
            }
        };

        const auto evalRowSel = [&](int64_t az, TopNHeap& h, int64_t& foundCnt, int64_t& skipped) {
            if (bitsStrictOk) {
                evalStrictRowBits(az, h, foundCnt); // v3.3.0：字级位并行（一次 64 锚点）
            } else if (strictSimdOk) {
                evalStrictRowSimd(az, h, foundCnt);
            } else {
                evalRow(az, h, foundCnt, skipped);
            }
        };

        // v3.3.0（3.1）：密度精确模式的行块滑动 —— 维护“列垂直和”vertSum（长度 W，
        // 每行摊销 O(W) 更新），窗口计数 O(1) 递推；结果与逐窗统计 100% 一致。
        // 仅用于光栅顺序（smartRange 由中心向外，不适用）且非提前停止路径。
        // 分层模式在剪枝无效时也走本路径（结果与精确模式一致，速度相当）。
        const bool slideOk = !strict && (opt.algorithm == Algorithm::Exact ||
                                         (opt.algorithm == Algorithm::Layered && !layeredPrune)) &&
                             true;   // v3.9.2：智能范围已按行带排序，带内连续 ⇒ 滑窗可用
        const auto evalDensityBlock = [&](size_t rowBegin, size_t rowEnd, TopNHeap& h, int64_t& foundCnt,
                                          bool report, int64_t& doneWin) {
            if (rowBegin >= rowEnd) return;
            const int K = opt.gridSize; // 1..512（validate 保证）
            const int wprI = static_cast<int>(grid.wordsPerRow());
            const int64_t az0 = azRows[rowBegin];
            // v3.6.16：门限 = K²（最小密度 1.0，窗口必须全是史莱姆区块）时，
            //   不需要 vertSum 滑窗计数 —— K 行纵向 AND + K 列横向 AND 直接出命中，
            //   实测比滑窗快 20 倍以上（探针 B1）。结果与滑窗路径逐位一致。
            const bool allSetFast = fastDensity && densNeed == K * K;
            if (allSetFast) {
                const int64_t relA0 = d.axMin - x0;
                const int64_t wFirst = relA0 >> 6;
                const int64_t wLast = (relA0 + (d.axMax - d.axMin)) >> 6;
                int64_t inRowAll = 0;
                for (size_t ri = rowBegin; ri < rowEnd; ++ri) {
                    const int64_t az = azRows[ri];
                    for (int64_t w = wFirst; w <= wLast; ++w) {
                        uint64_t v = ~0ULL;
                        uint64_t vn = ~0ULL;
                        for (int rr = 0; rr < K; ++rr) {
                            const uint64_t* row = grid.rowBits(az + rr);
                            v &= row[w];
                            if (w + 1 < wprI) vn &= row[w + 1];
                        }
                        uint64_t res = v;
                        for (int dc = 1; dc < K; ++dc) {
                            res &= (dc >= 64) ? 0ULL : ((v >> dc) | ((w + 1 < wprI) ? (vn << (64 - dc)) : 0ULL));
                        }
                        while (res) {
                            const int bit = static_cast<int>(std::countr_zero(res));
                            res &= res - 1;
                            const int64_t xa = x0 + w * 64 + bit;
                            if (xa < d.axMin || xa > d.axMax) continue;
                            recordMatchAt(az, xa, h, foundCnt, K * K);
                            ++inRowAll;
                        }
                    }
                    if ((inRowAll & 0x3FFF) == 0 && cancel.load(std::memory_order_relaxed)) return;
                    if (report) {
                        doneWin += windowsDoneRow;
                        reportWin(doneWin);
                    }
                }
                return;
            }
            // vertSum 覆盖整字宽（wpr*64 ≥ W）+ 窗口右边界余量（AVX2 行更新按整字处理）
            std::vector<int> vertSum(static_cast<size_t>(wprI) * 64 + static_cast<size_t>(K) + 8, 0);
            for (int rr = 0; rr < K; ++rr) {
                const int64_t rz = az0 + rr;
                for (int64_t rx = 0; rx < W; ++rx) {
                    if (grid.isSlime(x0 + rx, rz)) ++vertSum[static_cast<size_t>(rx)];
                }
            }
            const int64_t rxA0 = d.axMin - x0;
            int64_t inRow = 0;
            for (size_t ri = rowBegin; ri < rowEnd; ++ri) {
                const int64_t az = azRows[ri];
                if (ri > rowBegin) {
                    const int64_t rzNew = az + K - 1; // 新进入行
                    if (avx2) {
                        // v3.3.0（3.2）：整字 AVX2 行差分（位展开查表）
                        const uint64_t* newRow = grid.rowBits(rzNew);
                        const uint64_t* oldRow = (az > z0) ? grid.rowBits(az - 1) : nullptr;
                        vertSumDiffAvx2(vertSum.data(), newRow, oldRow, wprI, static_cast<int>(W), 0);
                    } else if (az > z0) {
                        const int64_t rzOld = az - 1; // 离开行
                        for (int64_t rx = 0; rx < W; ++rx) {
                            const int add = grid.isSlime(x0 + rx, rzNew) ? 1 : 0;
                            const int sub = grid.isSlime(x0 + rx, rzOld) ? 1 : 0;
                            vertSum[static_cast<size_t>(rx)] += add - sub;
                        }
                    } else {
                        for (int64_t rx = 0; rx < W; ++rx) {
                            if (grid.isSlime(x0 + rx, rzNew)) ++vertSum[static_cast<size_t>(rx)];
                        }
                    }
                }
                // 窗口扫描：v3.6.18 改成「SIMD 批量算窗口计数 + 只遍历候选」——
                //   一次 8 个锚点，先比 densNeed-2 出候选掩码，再对候选做精确判定（含 ±2 浮点回退），
                //   语义与标量滑窗逐位一致。AVX2 不可用或 K 过大时回退原标量滑窗。
                const int64_t nAnchors = d.axMax - d.axMin + 1;
                bool simdDone = false;
                const unsigned long long tDens0 = kScanProfileOn ? __rdtsc() : 0ULL;
                if (kUseSimdDensitySums && nAnchors >= 8 && fastDensity) {
                    // v3.7.7：SIMD 直接出「候选位掩码」，不再落 count*4 字节的中间数组
                    //   （1 亿锚点 = 400 MB 写+读，实测这层带宽就是匹配阶段的主要成本）。
                    static thread_local std::vector<uint64_t> tMask;
                    const int64_t words = (nAnchors + 63) / 64;
                    if (static_cast<int64_t>(tMask.size()) < words) {
                        tMask.resize(static_cast<size_t>(words));
                    }
                    if (densityHitMaskAvx2(vertSum.data() + rxA0, nAnchors, K, densThr - 1, tMask.data(),
                                           words)) {
                        simdDone = true;
                        for (int64_t w = 0; w < words; ++w) {
                            uint64_t m = tMask[static_cast<size_t>(w)];
                            while (m) {
                                const int b = std::countr_zero(m);
                                m &= m - 1;
                                const int64_t i = w * 64 + b;
                                if (i >= nAnchors) break;
                                // 命中很少：这里重算窗口和（K 次加法）比落数组便宜
                                int total = 0;
                                for (int c = 0; c < K; ++c) total += vertSum[static_cast<size_t>(rxA0 + i + c)];
                                if (total >= densThr) recordMatchAt(az, d.axMin + i, h, foundCnt, total);
                            }
                        }
                        inRow += nAnchors;
                        if (kScanProfileOn) {
                            g_profDensMaskCycles.fetch_add(static_cast<int64_t>(__rdtsc() - tDens0),
                                                           std::memory_order_relaxed);
                            g_profDensRows.fetch_add(1, std::memory_order_relaxed);
                        }
                        if (cancel.load(std::memory_order_relaxed)) return;
                    }
                } else if (kUseSimdDensitySums && nAnchors >= 8) {
                    static thread_local std::vector<int32_t> tSums;
                    if (static_cast<int64_t>(tSums.size()) < nAnchors) {
                        tSums.resize(static_cast<size_t>(nAnchors));
                    }
                    if (densityWindowSumsAvx2(vertSum.data() + rxA0, nAnchors, K, tSums.data())) {
                        simdDone = true;
                        for (int64_t i = 0; i < nAnchors; ++i) {
                            const int total = static_cast<int>(tSums[static_cast<size_t>(i)]);
                            if (densityWindowMeets(total, K, effectiveMinDensity)) {
                                recordMatchAt(az, d.axMin + i, h, foundCnt, total);
                            }
                        }
                        inRow += nAnchors;
                        if (cancel.load(std::memory_order_relaxed)) return;
                    }
                }
                if (!simdDone) {
                    int total = 0;
                    for (int c = 0; c < K; ++c) total += vertSum[static_cast<size_t>(rxA0 + c)];
                    int64_t xa = d.axMin;
                    for (;;) {
                        const bool hit = fastDensity
                                             ? (total >= densNeed + 2 ||
                                                (total > densNeed - 2 &&
                                                 densityWindowMeets(total, K, effectiveMinDensity)))
                                             : densityWindowMeets(total, K, effectiveMinDensity);
                        if (hit) recordMatchAt(az, xa, h, foundCnt, total);
                        ++inRow;
                        if ((inRow & 0x3FFF) == 0 && cancel.load(std::memory_order_relaxed)) return;
                        if (xa == d.axMax) break;
                        total += vertSum[static_cast<size_t>(rxA0 + (xa - x0) + K)] -
                                 vertSum[static_cast<size_t>(rxA0 + (xa - x0))];
                        ++xa;
                    }
                }
                if (report) {
                    doneWin += windowsDoneRow;
                    reportWin(doneWin);
                }
            }
        };

        // v3.3.8（任务 6 / E3）：密度滑窗路径的**独立计时**（多线程下用原子累加微秒）。
        // 两个调用点都改走本包装，保证 slideWindowMs 覆盖整段滑窗（含 vertSum 初始化与行差分）。
        std::atomic<int64_t> slideUsAccum{ 0 };
        const auto evalDensityBlockTimed = [&](size_t rowBegin, size_t rowEnd, TopNHeap& h, int64_t& foundCnt,
                                               bool report, int64_t& doneWin) {
            const auto s0 = clock::now();
            evalDensityBlock(rowBegin, rowEnd, h, foundCnt, report, doneWin);
            const int64_t us =
                std::chrono::duration_cast<std::chrono::microseconds>(clock::now() - s0).count();
            slideUsAccum.fetch_add(us, std::memory_order_relaxed);
        };

        // v3.2.0：行并行匹配。v3.9.3：允许与提前停止同时使用 —— 单位（行块）分批并行，
        //   每批结束后用与顺序路径完全相同的无损下界做一次早停判定。
        const bool parallelOk = dims.size() == 1 && nthreads > 1 &&
                                static_cast<int64_t>(azRows.size()) >= static_cast<int64_t>(nthreads) * 2;
        if (parallelOk) {
            const int nw = static_cast<int>(std::min<int64_t>(nthreads, azRows.size()));
            // v3.9.2：智能范围的行已按「带」重排，一个滑窗块必须正好是一条带（否则跨带行号跳变会算错）
            // v3.10.10.2：非滑窗（图案）路径此前 blockRows=1 ⇒ 早停批次=线程数(8) 时产生数千次
            //   建堆/合并（20k² 图案 stop：92 ms → 785 ms）。早停时按 64 行一个单元分批。
            const int64_t blockRows =
                slideOk ? (smartOrder ? smartBandRows() : 256) : (opt.stopAtTarget ? 64 : 1);
            const int64_t nUnits = (static_cast<int64_t>(azRows.size()) + blockRows - 1) / blockRows;
            int64_t uBegin = 0;
            int64_t evaluatedRows = 0;
            // v3.9.6.1：只有「提前停止」才需要分批（每批一次判停）。否则一次并行跑完，
            //   避免每批新建/合并堆的开销（严格图案匹配 1.5 → 163 ms 的回归就是它造成的）。
            // v3.10.5：早停时每批只跑 ≤2 个行块（约 512 行）就判停一次；此前一批=nw 个块
            //   （8 线程≈2048 行），小区域会「一批跑完全部」⇒ 提前停止永远不触发。
            // v3.10.6（用户反馈：修改后比 3.10.1 还慢）：恢复「每批 = 线程数 个行块」的并行度。
            //   v3.10.5 的 ≤2 行块把 CPU 早停搜索压到 1~2 线程 ⇒ 明显降速。
            const int64_t batchUnits = opt.stopAtTarget ? static_cast<int64_t>(nw) : nUnits;
            while (uBegin < nUnits && !earlyStop) {
                const int64_t uEnd = (std::min)(nUnits, uBegin + batchUnits);
                std::vector<TopNHeap> heaps;
                heaps.reserve(static_cast<size_t>(nw));
                for (int w = 0; w < nw; ++w) heaps.emplace_back(opt.targetCount);
                std::vector<int64_t> found(static_cast<size_t>(nw), 0);
                std::vector<int64_t> skippedPer(static_cast<size_t>(nw), 0);
                std::atomic<int64_t> nextU{ uBegin };
                {
                    std::vector<std::thread> pool;
                    pool.reserve(static_cast<size_t>(nw));
                    for (int w = 0; w < nw; ++w) {
                        pool.emplace_back([&, w]() {
                            TopNHeap& h = heaps[static_cast<size_t>(w)];
                            int64_t f = 0;
                            int64_t sk = 0;
                            for (;;) {
                                if (cancel.load(std::memory_order_relaxed)) break;
                                const int64_t u = nextU.fetch_add(1, std::memory_order_relaxed);
                                if (u >= uEnd) break;
                                const int64_t rb = u * blockRows;
                                const int64_t re = (std::min)(static_cast<int64_t>(azRows.size()),
                                                              rb + blockRows);
                                if (slideOk) {
                                    int64_t dummy = 0;
                                    evalDensityBlockTimed(static_cast<size_t>(rb), static_cast<size_t>(re), h, f,
                                                          false, dummy);
                                } else {
                                    for (int64_t i = rb; i < re; ++i) {
                                        evalRowSel(azRows[static_cast<size_t>(i)], h, f, sk);
                                    }
                                }
                            }
                            found[static_cast<size_t>(w)] = f;
                            skippedPer[static_cast<size_t>(w)] = sk;
                        });
                    }
                    for (auto& th : pool) th.join();
                }
                if (cancel.load(std::memory_order_relaxed)) {
                    rowCancelled = true;
                    break;
                }
                for (int w = 0; w < nw; ++w) {
                    totalFound += found[static_cast<size_t>(w)];
                    variantSkipped += skippedPer[static_cast<size_t>(w)];
                    for (const Scored& s : heaps[static_cast<size_t>(w)].sorted()) heap.push(s);
                }
                const int64_t rowsDone = (std::min)(static_cast<int64_t>(azRows.size()), uEnd * blockRows);
                evaluatedRows = rowsDone;
                globalWinDone = rowsDone * windowsDoneRow;
                reportWin(globalWinDone);
                uBegin = uEnd;
                // 每批结束做一次无损早停判定（与顺序路径同一套下界）
                if (opt.stopAtTarget && heap.full() && uBegin < nUnits) {
                    const double nth = heap.worstDist();
                    double L = std::numeric_limits<double>::infinity();
                    if (!smartOrder) {
                        L = distToRect(sxA, szA, static_cast<double>(d.axMin), static_cast<double>(d.axMax),
                                       static_cast<double>(azRows[static_cast<size_t>(uBegin * blockRows)]),
                                       static_cast<double>(azRows.back()));
                    } else {
                        for (size_t rr = static_cast<size_t>(uBegin * blockRows); rr < azRows.size(); ++rr) {
                            const double dd = std::fabs(static_cast<double>(azRows[rr]) - szA);
                            if (dd < L) L = dd;
                        }
                    }
                    if (nth <= L) earlyStop = true;
                }
            }
            if (cancel.load(std::memory_order_relaxed)) {
                rowCancelled = true;
            } else if (std::getenv("SLIME_DEBUG")) {
                std::fprintf(stderr, "[engine] parallel batched done rows=%lld earlyStop=%d\n",
                             static_cast<long long>(evaluatedRows), earlyStop ? 1 : 0);
                std::fflush(stderr);
            }
        } else if (slideOk) {
            // v3.3.0：顺序滑动的整段（一次 vertSum 初始化 + 行块滑动）
            // v3.4.8（用户实测：勾选「达到目标数量即停止」后没有任何提速）：原来**整段一次跑完**，
            // 提前停止判定永远轮不到 ⇒ 特性形同虚设。改为按行小块推进，每块后用与逐行路径
            // 完全相同的无损下界判定（下界成立 ⇒ 剩余窗口不可能优于当前第 N 名 ⇒ 可以停）。
            // 结果与不提前停止逐位一致（单元测试「提前停止结果 = 完整结果最近 N 个」继续成立）。
            const int64_t stopRows = stopBlockRows(); // v3.4.11：集中常量（可用 SLIME_STOP_BLOCK_ROWS 覆盖）
            for (int64_t rb = 0; rb < static_cast<int64_t>(azRows.size()) && !earlyStop;
                 rb += stopRows) {
                if (cancel.load(std::memory_order_relaxed)) { rowCancelled = true; break; }
                const int64_t re = std::min<int64_t>(static_cast<int64_t>(azRows.size()),
                                                     rb + stopRows);
                evalDensityBlockTimed(static_cast<size_t>(rb), static_cast<size_t>(re), heap, totalFound,
                                      true, globalWinDone);
                if (cancel.load(std::memory_order_relaxed)) { rowCancelled = true; break; }
                // v3.4.8：不再要求 dims.size() == 1 —— 该判定只约束**当前变体剩余窗口**能否进入
                // 全局 TopN 堆，与后续变体无关；提前停止当前变式不会漏掉任何「全局最近 N」的结果。
                if (opt.stopAtTarget && heap.full()) {
                    const double nth = heap.worstDist();
                    double L = std::numeric_limits<double>::infinity();
                    if (re < static_cast<int64_t>(azRows.size())) {
                        if (!smartOrder) {
                            // 光栅：尚未处理的行构成连续 z 段（azRows 升序）
                            L = distToRect(sxA, szA, static_cast<double>(d.axMin),
                                           static_cast<double>(d.axMax),
                                           static_cast<double>(azRows[static_cast<size_t>(re)]),
                                           static_cast<double>(azRows.back()));
                        } else {
                            // v3.9.2：行带排序后逐行单调性不成立 ⇒ 直接取剩余行的最小行距（每次带边界算一次）
                            L = std::numeric_limits<double>::infinity();
                            for (size_t rr = static_cast<size_t>(re); rr < azRows.size(); ++rr) {
                                const double dd = std::fabs(static_cast<double>(azRows[rr]) - szA);
                                if (dd < L) L = dd;
                            }
                        }
                    }
                    if (nth <= L) earlyStop = true;
                }
            }
        } else {
            for (size_t rowIdx = 0; rowIdx < azRows.size(); ++rowIdx) {
                if (cancel.load(std::memory_order_relaxed)) { rowCancelled = true; break; }
                const int64_t az = azRows[rowIdx];
                evalRowSel(az, heap, totalFound, variantSkipped);
                if (cancel.load(std::memory_order_relaxed)) { rowCancelled = true; break; }

                globalWinDone += windowsDoneRow;
                reportWin(globalWinDone);

                // 提前停止判定：剩余窗口不可能优于当前第 N 个。
                // 仅单变体（方向锁定/密度）时可精确停止——旋转模式中后续变体
                // 可能包含更近的窗口，因此不提前停止（结果仍为全局最优）。
                // v3.4.8：同上，去掉 dims.size()==1 限制（严格模式四向旋转同样可无损提前停止）
                if (opt.stopAtTarget && heap.full() && !earlyStop) {
                    const double nth = heap.worstDist();
                    double L = std::numeric_limits<double>::infinity();
                    if (rowIdx + 1 < azRows.size()) {
                        if (!smartOrder) {
                            // 光栅：剩余行构成连续矩形
                            L = distToRect(sxA, szA,
                                           static_cast<double>(d.axMin), static_cast<double>(d.axMax),
                                           static_cast<double>(azRows[rowIdx + 1]), static_cast<double>(d.azMax));
                        } else {
                            // 螺旋：剩余行按距起点距离单调不减
                            L = std::fabs(static_cast<double>(azRows[rowIdx + 1]) - szA);
                        }
                    } // 已是最后一行：L = inf，循环自然结束
                    if (nth <= L) earlyStop = true;
                }
                // v3.4.8（用户实测：本选项没有效果）：判定成立就**立刻跳出本变体的行循环**。
                // 旧代码只把 earlyStop 置真、却继续把剩下的行全部评估完（单变体时更是完全没用），
                // 于是「提前停止」只剩一个标志位。
                if (earlyStop) break;
            }
        }
        if (rowCancelled) break;

        variantBase += d.nAx * d.nAz;
        totalSkipped += variantSkipped;
        slideUsTotal += slideUsAccum.load(std::memory_order_relaxed);
    }

    out.stats.matchMs = elapsedMs();
    out.stats.slideWindowMs = static_cast<double>(slideUsTotal) / 1000.0; // v3.3.8（E3）
    out.windowsSkipped = totalSkipped;

    if (cancel.load(std::memory_order_relaxed)) {
        out.cancelled = true;
    } else {
        out.stats.windowsEvaluated = globalWinDone;
        const std::vector<Scored> sorted = heap.sorted();
        out.results.reserve(sorted.size());
        for (const Scored& s : sorted) out.results.push_back(s.m);
        out.stoppedEarly = earlyStop;
        // totalFound：找到的匹配窗口总数；提前停止时精确为堆内 N 个
        out.totalFound = earlyStop ? static_cast<int64_t>(out.results.size()) : totalFound;
    }
    out.stats.totalMs = elapsedMs();
    if (!out.cancelled) {
        matchEm.finish();
        out.ok = true;
    }
}

// ============================ v3.1.1 分块执行（整图/超大区域） ============================
// 位图整幅无法放入内存时：按 z 切成互不重叠的“锚点行组”，每块额外向下多扫
// hMax-1 行（窗口行高），块内调用经典路径（内存 ≤ kChunkWordsLimit），
// 块间并行（≤ kChunkThreads），结果经 (距离, minX, minZ) 归并取全局最近 N。
void SlimeEngine::runChunked(const SearchOptions& opt,
                             const std::atomic<bool>& cancel,
                             const ProgressFn& progress,
                             SearchOutcome& out) const noexcept
{
    using clock = std::chrono::steady_clock;
    const auto t0 = clock::now();
    const auto elapsedMs = [&t0]() {
        return std::chrono::duration<double, std::milli>(clock::now() - t0).count();
    };

    const int64_t x0 = opt.minX, z0 = opt.minZ;
    const int64_t W = static_cast<int64_t>(opt.maxX) - opt.minX + 1;
    const int64_t H = static_cast<int64_t>(opt.maxZ) - opt.minZ + 1;
    const int64_t wpr = (W + 63) / 64;
    const int64_t hMax = std::max<int64_t>(1, windowRowsMax(opt));
    // v3.3.9（任务书 1.1/1.2）：只有「缓存构建模式」收集并写入缓存条目；其他模式一律只读。
    const bool collectCacheChunks = opt.buildMode && opt.cache && opt.cacheWrite;
    out.chunkedPath = true; // v3.3.7.2：供 run() 区分汇总口径
    const int64_t planCap = collectCacheChunks
                                ? std::min<int64_t>(chunkWordsCap(), 2'500'000LL)
                                : chunkWordsCap();
    // v3.4.13（第 3 步）：计划**之前**先估一个并发（perChunkBytes=0 ⇒ 只受线程数与分块并发上限约束），
    //   用它算块数目标；计划定稿后再按真实单块字节数算最终并发（内存预算收紧时以最终值为准）。
    const int concGuess = chunkConcurrencyFor(opt.threads, 0, opt.memBudgetBytes);
    const int64_t targetChunks = chunkTargetFor(collectCacheChunks, concGuess, H, hMax);
    const ChunkPlan plan = chunkPlanFor(H, wpr, hMax, planCap, targetChunks);
    // v3.3.7（任务书 P0-4）：按「单块位图字节数 × 内存预算」决定并发（默认预算 1.5GB）
    const int64_t perChunkBytes = plan.dataRowsCap * wpr * static_cast<int64_t>(sizeof(uint64_t));
    const int conc = chunkConcurrencyFor(opt.threads, perChunkBytes, opt.memBudgetBytes);
    const int64_t totalAnchorRows = std::max<int64_t>(0, H - hMax + 1);
    const int64_t totalEvals = plan.totalDataEvals * W;
    // v3.3.7.2（任务书 1.2）：把引擎口径的总扫描量公开给界面，作为**同源分母**。
    // 分块路径含块间重叠行（每块 hMax-1 行），因此它比「去重区域区块数」略大；
    // 界面若用后者当分母，扫描完成时会算出 >100%。
    if (opt.liveTotal) opt.liveTotal->store(totalEvals, std::memory_order_relaxed);
    // 窗口几何（跨块剪枝用：窗口中心的可达范围）
    const int64_t winW = (opt.mode == Mode::Density) ? std::max<int64_t>(1, opt.gridSize)
                                                     : std::max<int64_t>(1, opt.pattern.cols());
    const int64_t winH = (opt.mode == Mode::Density) ? std::max<int64_t>(1, opt.gridSize)
                                                     : std::max<int64_t>(1, opt.pattern.rows());

    // 块几何
    struct ChunkGeom { int64_t start = 0, rows = 0, winRows = 0; };
    std::vector<ChunkGeom> chunks;
    chunks.reserve(static_cast<size_t>(plan.nChunks));
    {
        int64_t start = 0;
        while (start < H) {
            const int64_t rows = std::min(plan.dataRowsCap, H - start);
            const int64_t winRows = std::max<int64_t>(0, std::min(plan.anchorStep, totalAnchorRows - start));
            chunks.push_back(ChunkGeom{ start, rows, winRows });
            if (rows == plan.dataRowsCap) {
                start += plan.anchorStep;
            } else {
                start = H;
            }
        }
    }
    const int64_t nChunks = static_cast<int64_t>(chunks.size());

    // 全局收集：普通模式按 (dist, minX, minZ) 保留最近 N；
    // v3.10.3 最优候选模式按 (密度降序, dist 升序, minZ, minX) 合并各块结果。
    struct BestEntry {
        MatchResult m;
        double dist = 0.0;
        float density = 0.0f;
    };
    std::vector<BestEntry> best;
    best.reserve(static_cast<size_t>(std::min<int64_t>(opt.targetCount, 1 << 20)));
    const bool bestByDensity = opt.ignoreThreshold;
    const auto better = [bestByDensity](const BestEntry& a, const BestEntry& b) {
        if (bestByDensity) {
            if (a.density != b.density) return a.density > b.density;
            if (a.dist != b.dist) return a.dist < b.dist;
            if (a.m.minZ != b.m.minZ) return a.m.minZ < b.m.minZ;
            return a.m.minX < b.m.minX;
        }
        if (a.dist != b.dist) return a.dist < b.dist;
        if (a.m.minX != b.m.minX) return a.m.minX < b.m.minX;
        return a.m.minZ < b.m.minZ;
    };
    std::mutex mu;
    std::atomic<int64_t> totalFound{ 0 };
    std::atomic<int64_t> windowsDone{ 0 };
    std::atomic<int64_t> skippedDone{ 0 }; // v3.3.0：剪枝跳过窗口数（分层/估算）
    std::atomic<bool> anyError{ false };
    // v3.3.7（任务书 P0-1，B 方案）：全局第 N 名距离 + 已剪枝块数（只增不减 → 安全）
    std::atomic<double> globalWorstDist{ std::numeric_limits<double>::infinity() };
    std::atomic<bool> heapFullFlag{ false };
    std::atomic<int64_t> chunksPruned{ 0 };
    // v3.9.10（过程可视化）：跨块汇总 GPU 使用情况（只做展示）
    std::atomic<bool> gpuScanAny{ false };
    std::atomic<bool> gpuMatchAny{ false };
    std::string gpuFallback;
    std::string firstError;
    // v3.3.7.2（任务书步骤 2 / P1-D）：性能状态（块级耗时 / 归并 / 剪枝 / CPU / 内存）
    // 必须声明在 flushCacheBatch 之前（批次回调会累加这几个计数器）。
    slime::perf::CpuSampler perfCpuSampler;
    std::atomic<int64_t> perfMergeTotal{ 0 };
    std::atomic<int64_t> perfPruneTotal{ 0 };
    std::atomic<int64_t> perfPruneDrops{ 0 };
    // v3.3.8（任务 1 / ④′ 修复方案 B）：因「本块 0 新增条目」而跳过覆盖声明的块数
    std::atomic<int64_t> perfHollowSkips{ 0 };
    // v3.3.8（任务 6 / E3）：跨块累加的「填充 / 密度滑窗」耗时（微秒）
    std::atomic<int64_t> perfFillUs{ 0 };
    std::atomic<int64_t> perfSlideUs{ 0 };
    std::atomic<int64_t> perfCacheHitPoints{ 0 }; // v3.3.8：因缓存位图省掉的填充点数
    std::atomic<int64_t> perfLastSampleMs{ 0 };
    std::atomic<int64_t> perfCpuCount{ 0 };
    std::atomic<int64_t> perfMergeCalls{ 0 };
    std::atomic<double> perfCpuSum{ 0.0 };
    std::atomic<double> perfPeakMB{ 0.0 };
    std::atomic<double> perfPeakSeen{ 0.0 };
    std::atomic<int64_t> perfLockWaitMax{ 0 };
    std::atomic<int64_t> perfCacheWaitMax{ 0 };
    const int64_t perfStartMs = static_cast<int64_t>(elapsedMs());
    slime::IncidentLog::note("search.begin 分块路径 chunks=" + std::to_string(plan.nChunks) +
                             " rows/块=" + std::to_string(plan.dataRowsCap) + " conc=" +
                             std::to_string(conc) + " totalEvals=" + std::to_string(totalEvals) +
                             " cacheWrite=" + std::to_string(collectCacheChunks ? 1 : 0) +
                             (plan.nChunks <= 1
                                  ? " 注意=仅1块→块内多线程补齐（宽扁范围，见 F-4）"
                                  : (plan.nChunks < conc ? " 注意=块数少于并发，部分线程空闲" : "")));
    // 容量预算：未显式给定时按 1GB 兜底（防止大范围搜索缓存无限膨胀）
    // v3.3.7（P0-4）：缓存改为逐块即时归并（见 runChunkBody），不再保留分块片段，
    // 因此没有 cachePieces / piecesEntries 这类随范围增长的容器。
    // v3.3.8（任务 4）：缓存优先排序的可用性（与 useCache 同源；此处只用于「块分类」）
    const bool useCacheForOrder = (opt.cache != nullptr);
    const int64_t cacheBudget = (opt.cacheBudgetBytes > 0)
                                    ? opt.cacheBudgetBytes
                                    : (collectCacheChunks ? int64_t{ 1 } << 30 : 0);

    // v3.3.7.2（任务书步骤 3 / P0-C）：缓存**摊还归并 + 低水位剪枝**。
    // 旧实现每块完成就 mergeRegion 一次（O(N) 全表重建，全程持全局锁 mu）——
    // 开缓存时分块上限被压到 2.5M words ⇒ 439 亿场景有 277 块 ⇒ 277 次全表重建，
    // 实测 1.2 亿条目时单次 181~718 ms ⇒ 串行合并开销上百秒，且 11 个线程阻塞在同一把锁上。
    // 新策略：
    //   * 增量先攒进「批次」（条目数或块数达阈值才真正归并），一次 mergeRaw 合并多个区域；
    //   * 归并用**独立的 cacheMu**，不再占用 mu（mu 只保护 best/计数等共享状态）；
    //   * 剪枝只在批次边界执行，且剪到**低水位**（预算的 90%），避免临界区反复触发。
    // v3.4.13（任务书第 2 步）：单批上限从 16M 降到 4M —— 批越小，单次归并的临界区越短；
    //   配合下面的专职归并线程，工作线程不再为归并排队（实测单次归并降到 ~0.1 s 量级）。
    constexpr int64_t kCacheBatchEntries = 4'000'000; // 单批条目上限（≈32MB）
    constexpr int64_t kCacheBatchChunks = 4;           // 单批块数上限
    constexpr double kCacheLowWater = 0.90;            // 剪枝目标 = 预算 × 0.90
    std::mutex cacheMu;
    std::vector<Rect> pendingRanges;
    std::vector<std::pair<int, int>> pendingEntries;
    int64_t pendingChunks = 0;
    // ---------------- v3.4.13（任务书第 2 步）：归并与扫描解耦 ----------------
    // 旧实现：工作线程在 cacheMu 下直接 mergeRaw（O(N) 位图写入 + 可能的 pruneToFit），
    //   439 亿场景实测单次 1.1~1.8 s；块数一多（第 3 步 2→48）就会变成「扫描线程排队等归并」，
    //   收益被吃光甚至负收益（任务书第四节明确警告的连锁反应）。
    // 现在：工作线程只把本块增量**移入**批次并入队（短锁 µs 级）后立刻继续扫描；
    //   专职归并线程串行写缓存 —— 写入者仍然唯一 ⇒ CacheManager 无需内部加锁、缓存格式与
    //   最终位图逐位不变（归并顺序只影响「先写哪一位」，置位是幂等的 OR）。
    struct MergeBatch {
        std::vector<Rect> ranges;
        std::vector<std::pair<int, int>> entries;
        int64_t chunks = 0;
        int64_t ci = -1;
        int64_t enqueueMs = 0;
    };
    const bool mergeInline = []() {
        const char* v = std::getenv("SLIME_MERGE_INLINE");
        return v && *v == '1';
    }();
    std::mutex qMu;
    std::condition_variable qCv;     // 生产者在队列过深时等待（背压）
    std::condition_variable mergeCv; // 归并线程等待新批次 / 退出
    std::deque<MergeBatch> mergeQ;
    int64_t queuedEntries = 0; // qMu 保护
    bool mergeQuit = false;    // qMu 保护：工作线程全部结束
    std::atomic<int64_t> mergeQueueWaitMsMax{ 0 }; // 背压等待最长时间（诊断）
    // v3.3.7.2（任务书步骤 3 / P0-C）：缓存写入**准入控制**状态。
    // 判据：预算能容纳的条目数 = cacheBudgetBytes / 8（每条目 8 字节）。
    // 分块路径每块收集到的条目数可观测：一旦累计超过「可容纳条目」，继续收集只是
    // 白做工（收集→排序→合并→立刻被剪枝丢弃）。439.8 亿 + 密度 0.75 实测每块约 1600 万条，
    // 全范围 277 块共约 44 亿条（≈35GB），而 1GiB 预算只放得下 1.34 亿条 ⇒ 97% 被立即丢弃。
    const int64_t cacheCapacityEntries = (cacheBudget > 0) ? (cacheBudget / 8) : 0;
    std::atomic<int64_t> collectedEntries{ 0 };
    std::atomic<bool> cacheAdmissionClosed{ false };
    // v3.3.9（任务书 1.1）：构建模式达到容量上限 / 已跳过（缓存已覆盖）区块数
    std::atomic<bool> budgetStop{ false };
    std::atomic<int64_t> buildSkipped{ 0 };
    const auto closeCacheAdmission = [&](int64_t ci, int64_t collected) {
        if (cacheAdmissionClosed.exchange(true)) return;
        slime::IncidentLog::note("cache.admit 关闭缓存写入：块 " + std::to_string(ci) + " 已收集 " +
                                 std::to_string(collected) + " 条目 > 预算可容纳 " +
                                 std::to_string(cacheCapacityEntries) +
                                 "（继续收集会被立即剪枝丢弃，纯浪费；已合并的范围仍然有效）");
    };
    // v3.4.13（第 2 步）：真正写缓存的动作（**调用者必须持有 cacheMu**）。
    //   同时被「内联路径」（SLIME_MERGE_INLINE=1 / 收尾）与「专职归并线程」调用 ——
    //   两条路都保证同一时刻只有一个写入者。
    const auto applyMergeToCache = [&](std::vector<Rect>& ranges,
                                       std::vector<std::pair<int, int>>& entries, int64_t batched,
                                       int64_t ci, int64_t waitMs) {
        if (!collectCacheChunks || !opt.cacheWrite) return;
        if (ranges.empty() && entries.empty()) return;
        const int64_t entriesBefore = opt.cacheWrite->entryCount();
        const int64_t added = static_cast<int64_t>(entries.size());
        const int64_t tMerge0 = static_cast<int64_t>(elapsedMs());
        opt.cacheWrite->mergeRaw(ranges, entries);
        const int64_t mergeMs = static_cast<int64_t>(elapsedMs()) - tMerge0;
        perfMergeTotal.fetch_add(mergeMs, std::memory_order_relaxed);
        perfMergeCalls.fetch_add(1, std::memory_order_relaxed);
        slime::perf::chunkMerge(ci, waitMs, mergeMs, entriesBefore, added, batched);
        ranges.clear();
        entries.clear();
        if (cacheBudget > 0 && opt.cacheWrite->sizeBytes() > cacheBudget) {
            if (opt.buildMode) {
                // v3.3.9（任务书 1.1）：构建模式的第二个完成条件 —— **达到缓存容量上限即停止**。
                // 这里不做「边写边丢最旧」的剪枝：那会让构建永远到不了范围终点（半途而废）。
                budgetStop.store(true, std::memory_order_relaxed);
                return;
            }
            const int64_t soft = static_cast<int64_t>(static_cast<double>(cacheBudget) * kCacheLowWater);
            const size_t rangesBefore = opt.cacheWrite->ranges().size();
            const int64_t tPrune0 = static_cast<int64_t>(elapsedMs());
            opt.cacheWrite->pruneToFit(soft);
            const int64_t pruneMs = static_cast<int64_t>(elapsedMs()) - tPrune0;
            const int64_t drops = static_cast<int64_t>(rangesBefore) -
                                  static_cast<int64_t>(opt.cacheWrite->ranges().size());
            if (drops > 0 || pruneMs > 20) {
                perfPruneTotal.fetch_add(pruneMs, std::memory_order_relaxed);
                perfPruneDrops.fetch_add(drops, std::memory_order_relaxed);
                slime::perf::cachePrune(ci, pruneMs, drops, opt.cacheWrite->entryCount(),
                                        opt.cacheWrite->sizeBytes());
            }
        }
    };

    // v3.4.13（第 2 步）：旧的内联归并入口（**调用者必须持有 cacheMu**）。
    //   仅在 SLIME_MERGE_INLINE=1（A/B 对照 / 回滚）与收尾强制冲刷时使用。
    const auto flushCacheBatch = [&](int64_t ci, bool force) {
        if (!collectCacheChunks || !opt.cacheWrite) return;
        if (pendingRanges.empty() && pendingEntries.empty()) return;
        if (!force && static_cast<int64_t>(pendingEntries.size()) < kCacheBatchEntries &&
            pendingChunks < kCacheBatchChunks) {
            return;
        }
        const int64_t batched = pendingChunks;
        pendingChunks = 0;
        applyMergeToCache(pendingRanges, pendingEntries, batched, ci, 0);
    };
    // v3.4.13（第 2 步）：工作线程把本块增量**移入**队列（短锁）后立刻返回扫描。
    //   背压：队列内条目超过 2 批上限时等待归并线程消费（内存有界，不会因归并跟不上而爆）。
    const auto enqueueMerge = [&](MergeBatch&& b, int64_t ci) {
        const int64_t tEnq = static_cast<int64_t>(elapsedMs());
        b.ci = ci;
        b.enqueueMs = tEnq;
        std::unique_lock<std::mutex> ql(qMu);
        while (queuedEntries > kCacheBatchEntries * 2 && !mergeQuit) {
            qCv.wait_for(ql, std::chrono::milliseconds(20));
        }
        const int64_t waited = static_cast<int64_t>(elapsedMs()) - tEnq;
        if (waited > mergeQueueWaitMsMax.load(std::memory_order_relaxed)) {
            mergeQueueWaitMsMax.store(waited, std::memory_order_relaxed);
        }
        queuedEntries += static_cast<int64_t>(b.entries.size());
        mergeQ.push_back(std::move(b));
        ql.unlock();
        mergeCv.notify_one();
    };
    // v3.4.13（第 2 步）：专职归并线程 —— 唯一的缓存写入者（因而 CacheManager 无需内部加锁）。
    const auto mergerBody = [&]() {
        for (;;) {
            MergeBatch b;
            {
                std::unique_lock<std::mutex> ql(qMu);
                mergeCv.wait(ql, [&]() { return !mergeQ.empty() || mergeQuit; });
                if (mergeQ.empty()) {
                    if (mergeQuit) break;
                    continue;
                }
                b = std::move(mergeQ.front());
                mergeQ.pop_front();
                queuedEntries -= static_cast<int64_t>(b.entries.size());
                ql.unlock();
                qCv.notify_all(); // 生产者可以继续入队
            }
            const int64_t waitMs = static_cast<int64_t>(elapsedMs()) - b.enqueueMs;
            std::lock_guard<std::mutex> cacheLock(cacheMu); // 与内联路径互斥（同一时刻唯一写入者）
            applyMergeToCache(b.ranges, b.entries, b.chunks > 0 ? b.chunks : 1, b.ci, waitMs);
        }
    };

    // 与经典路径 TopNHeap 相同的方向：better 作为堆比较子 → 堆顶为当前最差
    const auto pushBest = [&](const BestEntry& e) {
        const int64_t cap = opt.targetCount;
        if (static_cast<int64_t>(best.size()) < cap) {
            best.push_back(e);
            std::push_heap(best.begin(), best.end(), better);
        } else if (better(e, best.front())) {
            std::pop_heap(best.begin(), best.end(), better);
            best.back() = e;
            std::push_heap(best.begin(), best.end(), better);
        }
    };

    // 进度：分块粒度（每完成一块汇报一次，整块扫描+匹配合计）
    std::atomic<int64_t> chunksDone{ 0 };

    // v3.3.7（任务书 P0-1，B 方案）：跨块剪枝判定 —— 本块是否存在「可能进入全局前 N」的窗口。
    // 判定依据：窗口中心距离（与引擎距离口径一致）；与当前第 N 名距离比较。
    // v3.4.11（用户拍板：等距也剪，与经典路径的 `nth <= L` 统一）：判据用 `>= worst`。
    //   无损性：TopN 堆的比较器是 (dist, rank) 字典序（rank = 扫描顺序索引，见 betterThan），
    //   等距窗口只有在 rank 更小时才可能替换第 N 名；而堆里的第 N 名必然是**更早评估**的窗口
    //   （rank 更小）⇒ 等距新窗口进不了前 N ⇒ 与经典路径语义一致（等距不进入前 N）。
    //   用陈旧的（偏大）第 N 名距离只会少跳，不会误跳。
    const auto chunkCannotImprove = [&](int64_t ci) {
        if (noPruneEnv()) return false; // SLIME_NO_PRUNE=1：仅用于 A/B 对照（关闭跨块剪枝）
        if (!heapFullFlag.load(std::memory_order_relaxed)) return false; // 全局堆未满：不剪枝
        const double worst = globalWorstDist.load(std::memory_order_relaxed);
        if (!(worst < std::numeric_limits<double>::infinity())) return false;
        const ChunkGeom& ch = chunks[static_cast<size_t>(ci)];
        if (ch.winRows <= 0) return true; // 该块没有需要评估的锚点行
        const double cxLo = static_cast<double>(x0) + (winW - 1) / 2.0;
        const double cxHi = static_cast<double>(x0 + W - winW) + (winW - 1) / 2.0;
        const double czLo = static_cast<double>(z0 + ch.start) + (winH - 1) / 2.0;
        const double czHi = static_cast<double>(z0 + ch.start + ch.winRows - 1) + (winH - 1) / 2.0;
        if (cxHi < cxLo || czHi < czLo) return true;
        return distToRect(static_cast<double>(opt.startX), static_cast<double>(opt.startZ),
                          cxLo, cxHi, czLo, czHi) >= worst;
    };

    // v3.3.9.1（任务书 1.2 根因修复）：**在启动工作线程之前**串行预计算「整块已被缓存覆盖」标志。
    // 旧实现让每个工作线程各自调用 opt.cache->covers() / 位图直取 / isSlime()，而同一时刻别的线程
    // 正在 mergeRaw 改写 ranges/bitmaps（CacheManager 无内部锁）⇒ 数据竞争 ⇒ 偶发闪退；开启详细日志
    // 后时序变化、命中概率下降，正是用户描述的「开日志就不崩」。预计算后工作线程不再并发读缓存对象。
    std::vector<uint8_t> chunkCoveredFlags(static_cast<size_t>(nChunks), 0);
    if (opt.cache != nullptr) {
        for (int64_t ci = 0; ci < nChunks; ++ci) {
            const ChunkGeom& cg = chunks[static_cast<size_t>(ci)];
            // 判定域 = 本块**锚点行域**（构建模式登记覆盖声明时用的正是这个域）⇒ 单块范围也能正确判「已覆盖」。
            // 尾部多出的 hMax-1 行属块间重叠行，本来就由下一块覆盖，不参与判定。
            const int64_t anchorRows = (plan.anchorStep > 0 && totalAnchorRows > cg.start)
                                           ? std::min<int64_t>(plan.anchorStep, totalAnchorRows - cg.start)
                                           : cg.rows;
            const int64_t lastRow = cg.start + (std::max<int64_t>(1, anchorRows) - 1);
            const Rect coverRect{ static_cast<int>(x0), static_cast<int>(x0 + W - 1),
                                  static_cast<int>(z0 + cg.start),
                                  static_cast<int>(z0 + std::min<int64_t>(lastRow, cg.start + cg.rows - 1)) };
            chunkCoveredFlags[static_cast<size_t>(ci)] = opt.cache->covers(coverRect) ? 1 : 0;
        }
    }

    // v3.1.2 修复：块间并行改用普通线程池（严禁在 OpenMP 并行区内部再调用
    // omp_set_num_threads —— 块内经典路径会设置线程数，嵌套会导致运行时错误/崩溃）。
    const auto runChunkBody = [&](int64_t ci) {
        const ChunkGeom& ch = chunks[static_cast<size_t>(ci)];
        // v3.3.9（任务书 1.1）：缓存构建模式的「中断续传」——整块已被缓存覆盖 ⇒ 直接跳过，
        // 不重复判定（重启后从头跑一次即可自动接着上次的位置继续）。
        if (opt.buildMode && opt.cache != nullptr) {
            if (chunkCoveredFlags[static_cast<size_t>(ci)] != 0) {
                if (opt.livePhase) opt.livePhase->store(1, std::memory_order_relaxed); // 整块来自缓存
                const int64_t skipped = ch.rows * W;
                buildSkipped.fetch_add(skipped, std::memory_order_relaxed);
                if (opt.liveSkippedCovered) {
                    opt.liveSkippedCovered->fetch_add(skipped, std::memory_order_relaxed);
                }
                chunksDone.fetch_add(1, std::memory_order_relaxed);
                return;
            }
        }
        SearchOptions sub = opt;
        sub.minZ = static_cast<int>(z0 + ch.start);
        sub.maxZ = static_cast<int>(z0 + ch.start + ch.rows - 1);
        // 锚点行域 = [start, start+anchorStep-1]（含旋转等更矮变体的尾部不再越块）
        g_anchorMaxAZ = z0 + ch.start + plan.anchorStep - 1;
        sub.smartRange = false;   // 分块路径按光栅顺序（结果与顺序无关，仅影响“智能”早停）
        // v3.3.7（任务书 P0-1，B 方案）：**允许块内几何提前停止**。
        // 块内停止条件（堆满 + 本块剩余区域不可能更近）对全局最近 N 是安全的：
        // 被跳过的窗口距离 ≥ 本块第 N 名 ≥ 全局第 N 名，因此不可能进入全局前 N。
        sub.stopAtTarget = opt.stopAtTarget;
        // v3.3.7.2（任务书步骤 4 / F-4）：**宽扁范围**（行数 ≤ 每块行数上限、但整图位图超过
        // 经典上限）只会切出 1 块 ⇒ 旧实现 `sub.threads = 1` 会让整场搜索**只跑 1 个线程**
        // （界面上的线程数设置形同虚设，也没有任何提示）。此时块内改用多线程补齐并行度：
        // 只有 1 块在跑，不存在嵌套 OpenMP 导致的超订。
        const int hwCores = static_cast<int>(std::max(1u, std::thread::hardware_concurrency()));
        // 只有 1 块时用块内多线程补齐并行度；**不覆盖**用户显式指定的线程数（opt.threads > 0）
        sub.threads = (nChunks <= 1) ? ((opt.threads > 0) ? opt.threads : hwCores) : 1;
        // v3.3.7.2（任务书步骤 3 / P0-C）：准入控制关闭后，后续分块只读缓存、不再收集条目
        sub.cacheWriteDisabled = cacheAdmissionClosed.load(std::memory_order_relaxed);
        // v3.4.13（连锁反应修复）：构建模式 = 唯一「并发写缓存」的场景 ⇒ 把归并锁交给工作线程，
        //   让「缓存命中填充」的读取与归并写入互斥（读取的数据会拷贝出来）。
        sub.cacheReadMu = collectCacheChunks ? &cacheMu : nullptr;
        // v3.4.13：数据域末尾 hMax-1 行属下一块锚点行域 ⇒ 只有**末块**收集它们（避免重复计入）
        {
            const bool lastChunk = (ci + 1 >= nChunks);
            sub.collectSkipZ0 = sub.minZ + static_cast<int>(ch.winRows);
            sub.collectSkipZ1 = lastChunk ? sub.collectSkipZ0 - 1
                                          : sub.minZ + static_cast<int>(ch.rows) - 1;
        }

        // v3.3.7（任务书 P0-3）：块内进度 —— 引擎内部仍按行上报，这里把它换算成
        // 「全局已扫描区块数 / 全局总扫描量」，让进度条与计数连续推进（≤4Hz）。
        const auto lastReport = std::make_shared<std::atomic<int64_t>>(0);
        const bool fullScanCoverage = (opt.algorithm != Algorithm::Sample); // 采样模式不按全量口径换算
        const ProgressFn subProgress = [&, lastReport](double engFrac, bool scanning, double, double) {
            if (!progress || !opt.liveScanned) return;
            const int64_t nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                      clock::now().time_since_epoch())
                                      .count();
            const int64_t prev = lastReport->load(std::memory_order_relaxed);
            if (nowMs - prev < 250) return; // 节流：最多 4 次/秒
            if (!lastReport->compare_exchange_strong(const_cast<int64_t&>(prev), nowMs,
                                                     std::memory_order_relaxed)) {
                return;
            }
            const int64_t scanned = opt.liveScanned->load(std::memory_order_relaxed);
            double frac = engFrac; // 采样模式：沿用引擎自己的块内比例
            if (fullScanCoverage && totalEvals > 0) {
                frac = std::min(0.999, static_cast<double>(scanned) / static_cast<double>(totalEvals));
            }
            const double el = elapsedMs() / 1000.0;
            const double cps = (el > 0.05) ? static_cast<double>(scanned) / el : 0.0;
            const double eta = (cps > 1.0) ? (static_cast<double>(totalEvals) - static_cast<double>(scanned)) / cps
                                           : 0.0;
            progress(frac, scanning, cps, eta);
        };

        SearchOutcome so;
        // v3.3.7.2（任务书步骤 2 / P1-D）：块级性能事件
        const int64_t perfChunk0 = static_cast<int64_t>(elapsedMs());
        slime::perf::chunkBegin(ci, ch.rows, z0 + ch.start, perfChunk0);
        if (std::getenv("SLIME_DEBUG")) {
            std::fprintf(stderr, "[chunk %lld] pre-run rows=%lld\n", static_cast<long long>(ci),
                         static_cast<long long>(ch.rows));
            std::fflush(stderr);
        }
        runImpl(sub, cancel, subProgress, so, false);
        g_anchorMaxAZ = INT64_MAX; // 恢复（本线程）
        // v3.3.8（任务 6 / E3）：跨块累加「填充 / 密度滑窗」耗时（每块内部两者是分开的）
        perfFillUs.fetch_add(static_cast<int64_t>(so.stats.fillMs * 1000.0), std::memory_order_relaxed);
        perfSlideUs.fetch_add(static_cast<int64_t>(so.stats.slideWindowMs * 1000.0),
                              std::memory_order_relaxed);
        perfCacheHitPoints.fetch_add(so.cacheHitPoints, std::memory_order_relaxed);
        slime::perf::chunkEnd(ci, static_cast<int64_t>(elapsedMs()) - perfChunk0,
                              so.stats.scannedChunks, static_cast<int64_t>(so.results.size()),
                              static_cast<int64_t>(elapsedMs()));
        if (std::getenv("SLIME_DEBUG")) {
            std::fprintf(stderr, "[chunk %lld] post-run ok=%d results=%zu newcache=%zu\n",
                         static_cast<long long>(ci), so.ok, so.results.size(), so.newCacheChunks.size());
            std::fflush(stderr);
        }
        if (cancel.load(std::memory_order_relaxed)) return;

        {
            // v3.3.7.2（任务书步骤 2 / P1-D）：记录「等锁时间 / 归并耗时 / 剪枝耗时」
            const int64_t perfLock0 = static_cast<int64_t>(elapsedMs());
            std::unique_lock<std::mutex> lock(mu);
            const int64_t perfLockWait = static_cast<int64_t>(elapsedMs()) - perfLock0;
            if (perfLockWait > perfLockWaitMax.load(std::memory_order_relaxed)) {
                perfLockWaitMax.store(perfLockWait, std::memory_order_relaxed);
            }
            if (so.gpuScanUsed) gpuScanAny.store(true, std::memory_order_relaxed);
            if (so.gpuMatchUsed) gpuMatchAny.store(true, std::memory_order_relaxed);
            if (!so.gpuFallbackReason.empty() && gpuFallback.empty()) gpuFallback = so.gpuFallbackReason;
            if (!so.ok) {
                if (!anyError.load(std::memory_order_relaxed)) {
                    anyError.store(true, std::memory_order_relaxed);
                    firstError = so.error;
                }
                lock.unlock();
                return;
            }
            for (const MatchResult& r : so.results) {
                const double cx = (static_cast<double>(r.minX) + r.maxX) / 2.0;
                const double cz = (static_cast<double>(r.minZ) + r.maxZ) / 2.0;
                const double d = std::hypot(cx - static_cast<double>(opt.startX),
                                           cz - static_cast<double>(opt.startZ));
                pushBest(BestEntry{ r, d, r.density });
            }
            totalFound.fetch_add(so.totalFound, std::memory_order_relaxed);
            windowsDone.fetch_add(so.stats.windowsEvaluated, std::memory_order_relaxed);
            skippedDone.fetch_add(so.windowsSkipped, std::memory_order_relaxed);

            // v3.3.7（任务书 P0-1，B 方案）：堆满后维护「全局第 N 名距离」，供跨块剪枝使用。
            // 该值只会随更优结果变小 → 剪枝判定用陈旧值也是保守的（不会误跳）。
            if (static_cast<int64_t>(best.size()) >= opt.targetCount) {
                globalWorstDist.store(best.front().dist, std::memory_order_relaxed);
                heapFullFlag.store(true, std::memory_order_relaxed);
            }

            // v3.3.7.2（任务书步骤 3 / P0-C）：**摊还归并** —— 不再每块做一次 O(N) 全表重建，
            // 而是把增量攒进批次（见 flushCacheBatch），在独立的 cacheMu 下批量合并。
            // v3.3.9.1（任务书 1.2）：用串行预计算的覆盖标志代替工作线程内的 covers()
            //（消除「一个线程读 ranges/bitmaps、另一个线程 mergeRaw 改写」的数据竞争）
            if (collectCacheChunks && opt.cacheWrite &&
                chunkCoveredFlags[static_cast<size_t>(ci)] == 0 &&
                !cacheAdmissionClosed.load(std::memory_order_relaxed)) {
                collectedEntries.fetch_add(static_cast<int64_t>(so.newCacheChunks.size()),
                                           std::memory_order_relaxed);
                if (cacheCapacityEntries > 0 &&
                    collectedEntries.load(std::memory_order_relaxed) > cacheCapacityEntries) {
                    // 已收集的条目已超过预算容量 → 后续分块不再收集（本块仍照常合并）
                    closeCacheAdmission(ci, collectedEntries.load(std::memory_order_relaxed));
                    // v3.4.13：准入关闭本来就是「预算装不下这么多条目」的同义事件（条目 ≈ 预算/8 字节）
                    //   ⇒ 构建模式下同时置 hitCacheBudget，界面才会如实显示「达到容量上限」。
                    //   （块变小后单块位图可能始终小于预算字节数，只靠 sizeBytes() 判定会漏报。）
                    if (opt.buildMode) budgetStop.store(true, std::memory_order_relaxed);
                }
                const int64_t perfCacheLock0 = static_cast<int64_t>(elapsedMs());
                std::unique_lock<std::mutex> cacheLock(cacheMu);
                const int64_t perfCacheWait = static_cast<int64_t>(elapsedMs()) - perfCacheLock0;
                if (perfCacheWait > perfCacheWaitMax.load(std::memory_order_relaxed)) {
                    perfCacheWaitMax.store(perfCacheWait, std::memory_order_relaxed);
                }
                // v3.3.8（任务 1 / ④′ 修复方案 B）：**登记覆盖声明前先确认「有新增条目」**。
                // 旧实现无条件 `pendingRanges.push_back(sub.region())`：一旦 pruneToFit 整 range 丢条目，
                // 之后**跨运行**再次登记同一区域（该块 0 新增条目，因为其行大多已命中缓存）就会留下
                // 「无条目支撑」的空壳声明 ⇒ covers()=true 而 lookup() 恒返回 0（被判为已知非史莱姆）
                // ⇒ 命中区被判非史莱姆：实测漏 34%、0 假阳性。
                // 语义影响：一个块 0 新增条目 ⇒ 其扫描结果已被保留的 range 覆盖（否则 762 行 × 209715 列
                // 在基岩 ~10% 概率下期望约 1600 万条）⇒ 不登记不损失覆盖信息，只可能让未来重扫（正确性优先）。
                // 已知极小概率例外：整块 762×209715 个位置全部非史莱姆（概率 ≈0.9^1.6e8 ≈ 0），可忽略。
                // v3.3.8（候选 ②，用户预判选项）：**登记域 = 本块的锚点行域**（登记域缩到 758），
                // 不再是 `sub.region()` 的完整数据行域（762）。理由与影响：
                //   * 匹配域按 `g_anchorMaxAZ` 限定在锚点行 [start, start+winRows-1]，而数据行域比它多
                //     hMax-1=4 行（用于最后几个锚点行的窗口）；旧实现声明 762 行 ⇒ 相邻块声明**重叠 4 行**
                //     （两块都真的扫过、结论一致，故不是正确性问题，只是口径与去重上的冗余）；
                //   * 缩到锚点行后相邻块声明互不重叠、与匹配域口径一致；代价是重复搜索时每块有
                //     4/762 ≈ 0.5% 的行不再算「已覆盖」（会重算那几行，并因此走一次增量归并去重）；
                //   * 正确性不变：锚点行域内的每个位置都真的被扫过（史莱姆判定完整）。
                const Rect declareRect{ sub.region().minX, sub.region().maxX, sub.minZ,
                                        sub.minZ + static_cast<int>(ch.winRows) - 1 };
                if (!so.newCacheChunks.empty() && declareRect.valid()) {
                    if (mergeInline) {
                        // 旧路径（SLIME_MERGE_INLINE=1）：持有 cacheMu 就地归并
                        pendingRanges.push_back(declareRect); // 声明与条目同步登记（有支撑才声明）
                        pendingEntries.insert(pendingEntries.end(), so.newCacheChunks.begin(),
                                              so.newCacheChunks.end());
                        so.newCacheChunks.clear();
                        ++pendingChunks;
                        flushCacheBatch(ci, false);
                    } else {
                        // v3.4.13（第 2 步）：移入批次并入队（短锁）—— 扫描线程不再等归并
                        MergeBatch b;
                        b.ranges.push_back(declareRect);
                        b.entries.swap(so.newCacheChunks); // 零拷贝移交
                        b.chunks = 1;
                        enqueueMerge(std::move(b), ci);
                    }
                } else {
                    perfHollowSkips.fetch_add(1, std::memory_order_relaxed);
                }
                so.newCacheChunks.shrink_to_fit();
            }
        }
        // v3.3.7.2（任务书步骤 2 / P1-D）：每 5 秒采样一次 CPU 与内存（锁外执行，避免干扰）
        {
            const int64_t el2 = static_cast<int64_t>(elapsedMs());
            const int64_t last = perfLastSampleMs.load(std::memory_order_relaxed);
            if (el2 - last >= 5000 && perfLastSampleMs.compare_exchange_strong(
                                          const_cast<int64_t&>(last), el2, std::memory_order_relaxed)) {
                double cpu = 0.0, cpuAll = 0.0, ws = 0.0;
                if (perfCpuSampler.sample(&cpu, &cpuAll, &ws)) { // v3.4.13：多线程并发调用已加锁
                    perfCpuSum.fetch_add(cpu, std::memory_order_relaxed);
                    perfCpuCount.fetch_add(1, std::memory_order_relaxed);
                    const double peak = slime::IncidentLog::peakWorkingSetMB();
                    atomicMaxDouble(perfPeakMB, ws);
                    if (ws >= perfPeakSeen.load(std::memory_order_relaxed) && ws > 1.0) {
                        perfPeakSeen.store(ws, std::memory_order_relaxed);
                        slime::perf::memPeak(el2, peak, ws);
                    }
                    slime::perf::cpuSample(el2, cpu, ws, peak);
                }
            }
        }
        const int64_t done = chunksDone.fetch_add(1, std::memory_order_relaxed) + 1;
        if (progress) {
            // v3.3.7（任务书 P0-3）：优先用「真实已扫描区块数 / 总扫描量」作为进度，
            // 与界面显示的 N/M 完全同源；无计数器时退回块完成比例。
            double frac = static_cast<double>(done) / static_cast<double>(nChunks);
            if (opt.liveScanned && totalEvals > 0) {
                frac = static_cast<double>(opt.liveScanned->load(std::memory_order_relaxed)) /
                       static_cast<double>(totalEvals);
            }
            double cps = 0.0, eta = 0.0;
            const double el = elapsedMs() / 1000.0;
            if (el > 0.01 && totalEvals > 0) {
                const double doneEvals = frac * static_cast<double>(totalEvals);
                cps = doneEvals / el;
                if (cps > 1.0) eta = (static_cast<double>(totalEvals) - doneEvals) / cps;
            }
            progress(std::min(0.999, frac), true, cps, eta);
        }
    };

    std::atomic<int64_t> nextChunk{ 0 };
    // v3.3.8（任务 4：缓存优先 + 命中即停）：启用「达到目标数量即停止」时，**先处理「锚点行域已被
    // 缓存覆盖」的块**（这些块的填充几乎全部由缓存位图直取，代价极低），让 TopN 堆尽早填满；
    // 堆满后，剩余未覆盖块由既有的跨块剪枝 `chunkCannotImprove` 跳过（距离下界已劣于第 N 名）⇒ 真正「命中即停」。
    // 不启用提前停止时保持原光栅顺序（不做任何行为改变）。
    std::vector<int64_t> chunkOrder;
    chunkOrder.reserve(static_cast<size_t>(nChunks));
    int64_t cacheFirstCount = 0;
    bool cacheFirstReorder = false;
    bool cacheAllCovered = false; // v3.3.9（1.3）：整区被缓存覆盖 ⇒ 堆满即停
    std::atomic<bool> cacheStopFlag{ false }; // v3.3.8.1（任务书 1.1）：缓存内够数即停是否触发
    if (opt.stopAtTarget && useCacheForOrder && opt.cache != nullptr) {
        // v3.3.9（任务书 1.3 根因②）：覆盖率判定改为**按行覆盖率**（缓存 range 的 X 区间并集
        // 覆盖整行即算覆盖），不再要求「单个 range 同时覆盖整宽 + 整段 Z + wpr 完全相同」——
        // 旧判据在真实缓存（多段、宽度不同）下几乎不可能满足 ⇒ 特性永不触发（实测「未生效」）。
        const std::vector<Rect>& crs = opt.cache->ranges();
        std::vector<uint8_t> rowCov(static_cast<size_t>(H), 0);
        {
            std::vector<std::pair<int64_t, int64_t>> spans;
            spans.reserve(crs.size());
            for (int64_t r = 0; r < H; ++r) {
                const int absZ = static_cast<int>(z0 + r);
                spans.clear();
                for (const Rect& rc : crs) {
                    if (absZ < rc.minZ || absZ > rc.maxZ) continue;
                    spans.emplace_back(rc.minX, rc.maxX);
                }
                if (spans.empty()) continue;
                std::sort(spans.begin(), spans.end());
                int64_t cover = x0;
                for (const auto& sp : spans) {
                    if (sp.first > cover) break;                       // 出现空洞
                    cover = std::max(cover, sp.second + 1);
                    if (cover >= x0 + W) break;                        // 整行覆盖
                }
                rowCov[static_cast<size_t>(r)] = (cover >= x0 + W) ? 1 : 0;
            }
        }
        const auto chunkCovered = [&](int64_t ci) -> bool {
            const ChunkGeom& ch = chunks[static_cast<size_t>(ci)];
            if (ch.rows <= 0) return false;
            for (int64_t r = ch.start; r < ch.start + ch.rows; ++r) {
                if (!rowCov[static_cast<size_t>(r)]) return false;
            }
            return true;
        };
        for (int64_t i = 0; i < nChunks; ++i) {
            if (chunkCovered(i)) {
                chunkOrder.push_back(i);
                ++cacheFirstCount;
            }
        }
        if (cacheFirstCount > 0 && cacheFirstCount < nChunks) {
            for (int64_t i = 0; i < nChunks; ++i) {
                if (!chunkCovered(i)) chunkOrder.push_back(i);
            }
            cacheFirstReorder = true;
        } else if (cacheFirstCount >= nChunks && nChunks > 0) {
            // 整区都被缓存覆盖：堆一满即可停（所有数据都来自缓存 ⇒ 无需再扩展扫描）
            for (int64_t i = 0; i < nChunks; ++i) chunkOrder.push_back(i);
            cacheFirstReorder = true;
            cacheAllCovered = true;
        } else {
            chunkOrder.clear();
        }
        if (std::getenv("SLIME_DEBUG") || slime::IncidentLog::verbose()) {
            std::fprintf(stderr,
                         "[engine] cache-first: covered=%lld/%lld reorder=%d (W=%lld H=%lld rowsCov=%lld)\n",
                         static_cast<long long>(cacheFirstCount), static_cast<long long>(nChunks),
                         cacheFirstReorder ? 1 : 0, static_cast<long long>(W), static_cast<long long>(H),
                         static_cast<long long>(std::count(rowCov.begin(), rowCov.end(), uint8_t{ 1 })));
        }
    }
    if (chunkOrder.empty()) {
        for (int64_t i = 0; i < nChunks; ++i) chunkOrder.push_back(i);
    }
    {
        std::vector<std::thread> pool;
        // v3.4.13（第 2 步）：与工作线程池同生命周期启动专职归并线程
        std::thread mergerThread;
        const bool mergerOn = (!mergeInline && collectCacheChunks && opt.cacheWrite != nullptr);
        if (mergerOn) mergerThread = std::thread(mergerBody);
        const int workerCount = static_cast<int>(std::min<int64_t>(conc, nChunks));
        for (int w = 0; w < workerCount; ++w) {
            pool.emplace_back([&]() {
                for (;;) {
                    const int64_t slot = nextChunk.fetch_add(1, std::memory_order_relaxed);
                    if (slot >= nChunks) break;
                    const int64_t ci = chunkOrder[static_cast<size_t>(slot)];
                    if (cancel.load(std::memory_order_relaxed)) break;
                    // v3.3.8.1（任务书 1.1：缓存 + 及时反馈联动）：**缓存优先阶段已凑够目标数 ⇒ 立即停**，
                    // 不再扩展扫描未覆盖区（「及时反馈」语义 = 缓存内够数即停；不保证全局最近 N）。
                    // 仅在「存在缓存覆盖块」的排序生效时启用；无缓存时保持 v3.3.7 的无损剪枝行为。
                    // v3.3.9（任务书 1.3）：整区被缓存覆盖时（cacheAllCovered）堆一满即停；
                    // 否则在「缓存覆盖块」处理完之后、堆已满时立即停（不再扩展扫描未覆盖区）。
                    if (opt.stopAtTarget && cacheFirstReorder &&
                        (cacheAllCovered || slot >= cacheFirstCount) &&
                        heapFullFlag.load(std::memory_order_relaxed)) {
                        cacheStopFlag.store(true, std::memory_order_relaxed);
                        chunksPruned.fetch_add(1, std::memory_order_relaxed);
                        const int64_t doneSkip = chunksDone.fetch_add(1, std::memory_order_relaxed) + 1;
                        if (progress) {
                            const double frac = static_cast<double>(doneSkip) / static_cast<double>(nChunks);
                            progress(std::min(0.999, frac), true, 0.0, 0.0);
                        }
                        continue;
                    }
                    // v3.3.7（任务书 P0-1，B 方案）：跨块剪枝 —— 全局堆已满且本块全部窗口
                    // 的可能最近距离都不优于当前第 N 名时，跳过该块（结果仍为全局最近 N）。
                    if (opt.stopAtTarget && chunkCannotImprove(ci)) {
                        chunksPruned.fetch_add(1, std::memory_order_relaxed);
                        const int64_t done = chunksDone.fetch_add(1, std::memory_order_relaxed) + 1;
                        if (progress) {
                            double frac = static_cast<double>(done) / static_cast<double>(nChunks);
                            if (opt.liveScanned && totalEvals > 0) {
                                frac = static_cast<double>(opt.liveScanned->load(std::memory_order_relaxed)) /
                                       static_cast<double>(totalEvals);
                            }
                            progress(std::min(0.999, frac), true, 0.0, 0.0);
                        }
                        continue;
                    }
                    runChunkBody(ci);
                }
            });
        }
        for (auto& th : pool) th.join();
        // v3.4.13（第 2 步）：排空队列后收束归并线程（此时所有工作线程已结束）
        if (mergerOn) {
            {
                std::lock_guard<std::mutex> ql(qMu);
                mergeQuit = true;
            }
            mergeCv.notify_all();
            mergerThread.join();
        }
    }

    // v3.3.7.2（任务书步骤 3 / P0-C）：收尾 —— 把最后一批增量并入缓存（所有工作线程已结束）
    {
        std::lock_guard<std::mutex> cacheLock(cacheMu);
        flushCacheBatch(-1, true);
    }

    if (cancel.load(std::memory_order_relaxed)) {
        out.cancelled = true;
        out.stats.totalMs = elapsedMs();
        return;
    }
    if (anyError.load(std::memory_order_relaxed)) {
        out.error = firstError;
        out.stats.totalMs = elapsedMs();
        return;
    }

    // v3.3.7（P0-4）：缓存已在各块完成时即时并入 cacheWrite（见 runChunkBody），此处无需再合并。

    std::sort(best.begin(), best.end(), better);
    out.results.reserve(best.size());
    for (const BestEntry& e : best) out.results.push_back(e.m);
    out.totalFound = totalFound.load(std::memory_order_relaxed);
    // v3.3.7（任务书 P0-3）：真实已扫描区块数（与界面显示的 N/M 同源）；
    // 无计数器时退回计划扫描量 totalEvals（旧口径）。
    out.stats.scannedChunks = opt.liveScanned ? opt.liveScanned->load(std::memory_order_relaxed) : totalEvals;
    out.stats.windowsEvaluated = windowsDone.load(std::memory_order_relaxed);
    out.windowsSkipped = skippedDone.load(std::memory_order_relaxed);
    out.stats.threadsUsed = conc;
    out.stats.totalMs = elapsedMs();
    out.stats.scanMs = elapsedMs(); // 分块路径扫描/匹配交错，合并报告为总耗时
    // v3.3.8（任务 6 / E3）：填充与密度滑窗的**分列**耗时（跨块累加；每块内部两者本就是分开的）
    out.stats.fillMs = static_cast<double>(perfFillUs.load(std::memory_order_relaxed)) / 1000.0;
    out.stats.slideWindowMs = static_cast<double>(perfSlideUs.load(std::memory_order_relaxed)) / 1000.0;
    out.cacheHitPoints = perfCacheHitPoints.load(std::memory_order_relaxed);
    out.cacheFillSkipped = out.cacheHitPoints > 0;
    // v3.3.7（任务书 P0-1，B 方案）：剪枝统计（>0 表示跨块提前停止生效，结果仍为全局最近 N）
    out.cacheFirstOrder = cacheFirstReorder;
    out.cacheFirstChunks = cacheFirstReorder ? cacheFirstCount : 0;
    out.cacheFirstStopped = cacheStopFlag.load(std::memory_order_relaxed);
    out.chunksPruned = chunksPruned.load(std::memory_order_relaxed);
    // v3.4.11（用户拍板）：缓存优先阶段「堆满即停」（cacheFirstStopped）也是一次真正的提前停止
    // ——旧代码只认 chunksPruned，导致这种情形界面不显示「[提前停止]」（实测：评估窗口从 15.7 万降到
    // 7.9 万却不报提前停止）。
    out.stoppedEarly = (out.chunksPruned > 0) || out.cacheFirstStopped;
    out.gpuScanUsed = gpuScanAny.load(std::memory_order_relaxed);
    out.gpuMatchUsed = gpuMatchAny.load(std::memory_order_relaxed);
    out.gpuFallbackReason = gpuFallback;
    if (std::getenv("SLIME_DEBUG")) {
        std::fprintf(stderr,
                     "[engine] chunked done: chunks=%lld pruned=%lld conc=%d scanned=%lld/%lld results=%zu ms=%.0f\n",
                     static_cast<long long>(nChunks), static_cast<long long>(out.chunksPruned), conc,
                     static_cast<long long>(out.stats.scannedChunks), static_cast<long long>(totalEvals),
                     out.results.size(), out.stats.totalMs);
        std::fflush(stderr);
    }
    // 分块路径的缓存已按块并入 cacheWrite：GUI 不再整区合并（避免整体覆盖声明失真）
    out.cacheHandledIncrementally = collectCacheChunks && (opt.cache != nullptr);
    // v3.3.9（任务书 1.1）：构建模式的收尾统计（是否因容量上限停止 / 已跳过多少已覆盖区块）
    out.hitCacheBudget = budgetStop.load(std::memory_order_relaxed);
    out.buildSkippedChunks = buildSkipped.load(std::memory_order_relaxed);
    // v3.3.7.2（任务书步骤 2 / P1-D）：search.summary —— 各段累计耗时与峰值，供慢搜索归因
    {
        const int64_t mergeTotal = perfMergeTotal.load(std::memory_order_relaxed);
        const int64_t pruneTotal = perfPruneTotal.load(std::memory_order_relaxed);
        const int64_t drops = perfPruneDrops.load(std::memory_order_relaxed);
        const int64_t cpuN = perfCpuCount.load(std::memory_order_relaxed);
        const double cpuAvg = (cpuN > 0) ? perfCpuSum.load(std::memory_order_relaxed) / static_cast<double>(cpuN)
                                         : 0.0;
        const double peakMB = perfPeakMB.load(std::memory_order_relaxed);
        char buf[512];
        std::snprintf(buf, sizeof(buf),
                      "chunks=%lld pruned=%lld scanned=%lld/%lld results=%zu total=%.0fms merge=%lldms "
                      "mergeCalls=%lld prune=%lldms drops=%lld lockWaitMax=%lldms cacheLockWaitMax=%lldms "
                      "cpuAvg=%.1f%% wsPeak=%.0fMB "
                      "conc=%d cacheWrite=%d hollowSkips=%lld fill=%.0fms slideWindow=%.0fms rowsPerChunk=%lld cacheHitPoints=%lld cacheFirst=%d/%lld cacheStop=%d mergeQueueWaitMax=%lldms mergeMode=%s",
                      static_cast<long long>(nChunks), static_cast<long long>(out.chunksPruned),
                      static_cast<long long>(out.stats.scannedChunks), static_cast<long long>(totalEvals),
                      out.results.size(), out.stats.totalMs, static_cast<long long>(mergeTotal),
                      static_cast<long long>(perfMergeCalls.load(std::memory_order_relaxed)),
                      static_cast<long long>(pruneTotal), static_cast<long long>(drops),
                      static_cast<long long>(perfLockWaitMax.load(std::memory_order_relaxed)),
                      static_cast<long long>(perfCacheWaitMax.load(std::memory_order_relaxed)), cpuAvg, peakMB,
                      conc, collectCacheChunks ? 1 : 0,
                      static_cast<long long>(perfHollowSkips.load(std::memory_order_relaxed)),
                      out.stats.fillMs, out.stats.slideWindowMs,
                      static_cast<long long>(plan.dataRowsCap),
                      // v3.3.8.1：缓存相关统计（此前格式串追加了字段却没补实参 ⇒ C4473 警告 + 读到野值）
                      static_cast<long long>(out.cacheHitPoints), out.cacheFirstOrder ? 1 : 0,
                      static_cast<long long>(out.cacheFirstChunks), out.cacheFirstStopped ? 1 : 0,
                      static_cast<long long>(mergeQueueWaitMsMax.load(std::memory_order_relaxed)),
                      mergeInline ? "inline" : "decoupled");
        slime::perf::searchSummary(buf);
        // v3.3.8（任务 1 / ④′ 修复方案 B）：把「跳过空壳声明」的块数显式记一行，便于事后取证
        if (perfHollowSkips.load(std::memory_order_relaxed) > 0) {
            slime::IncidentLog::note(
                "cache.hollow 跳过覆盖声明 " +
                std::to_string(perfHollowSkips.load(std::memory_order_relaxed)) +
                " 个块（该块 0 新增条目 ⇒ 扫描结果已被保留的 range 覆盖；"
                "旧实现会留下无条目支撑的空壳声明 ⇒ covers()=true 而 lookup() 恒 0 ⇒ 漏史莱姆）");
        }
        if (std::getenv("SLIME_DEBUG")) {
            std::fprintf(stderr, "[engine] %s (perfStart=%.0fms)\n", buf,
                         static_cast<double>(perfStartMs));
            std::fflush(stderr);
        }
    }
    out.ok = true;
    if (progress) progress(1.0, false, 0.0, 0.0);
}

// ============================ v3.1.1 耗时估算 ============================
bool SlimeEngine::estimateSearch(const SearchOptions& opt, int threads, SearchEstimate& est) noexcept
{
    est = SearchEstimate();
    const int64_t W = static_cast<int64_t>(opt.maxX) - opt.minX + 1;
    const int64_t H = static_cast<int64_t>(opt.maxZ) - opt.minZ + 1;
    const int64_t wpr = (W + 63) / 64;
    const int64_t hMax = std::max<int64_t>(1, windowRowsMax(opt));
    if (W <= 0 || H <= 0) return false;

    // 样本区域（小块实测速率：扫描按当前判定；匹配按当前模式/图案）
    const int64_t sw = std::min<int64_t>(W, 1024);
    const int64_t sh = std::min<int64_t>(H, 1024);
    const int64_t needW = (opt.mode == Mode::Density) ? opt.gridSize : opt.pattern.cols();
    const int64_t needH = (opt.mode == Mode::Density) ? opt.gridSize : opt.pattern.rows();
    if (sw < needW || sh < needH) return false; // 样本不足以评估匹配速率

    SearchOptions sample = opt;
    sample.minX = opt.minX;
    sample.minZ = opt.minZ;
    sample.maxX = opt.minX + static_cast<int>(sw - 1);
    sample.maxZ = opt.minZ + static_cast<int>(sh - 1);
    sample.smartRange = false;
    sample.stopAtTarget = false;
    sample.threads = 1; // 匹配本身单线程；分块内扫描亦单线程 → 用单线程速率推算

    SearchOutcome so;
    std::atomic<bool> localCancel{ false };
    SlimeEngine engine;
    engine.runImpl(sample, localCancel, nullptr, so, false);
    if (!so.ok) return false;
    // v3.10.10.1：最小密度=0 走「最近 N 快速路径」时样本耗时可能接近 0（未扫描整图）——
    //   这不代表估算失败；用 0.01 ms 下限避免除零，并让「预计耗时」如实体现在极快。
    const double sampleScanMs = (std::max)(0.01, so.stats.scanMs);

    const double scanMs = sampleScanMs;
    const double matchMs = std::max(0.0, so.stats.totalMs - so.stats.scanMs);
    const int64_t sampleChunks = sw * sh;
    const int64_t sampleWindows = so.stats.windowsEvaluated;

    est.areaChunks = W * H;
    est.windows = windowsTotalFor(opt, W, H);
    est.scanRate = static_cast<double>(sampleChunks) / (scanMs / 1000.0);
    est.matchRate = matchMs > 0 && sampleWindows > 0
                        ? static_cast<double>(sampleWindows) / (matchMs / 1000.0)
                        : 0.0;

    const int64_t collectEst = (opt.version == Version::Bedrock) && opt.useCache;
    const int64_t planCap = collectEst ? std::min<int64_t>(chunkWordsCap(), 2'500'000LL)
                                       : chunkWordsCap();
    // v3.4.13（第 3 步）：与 runChunked 用同一套块数目标，否则 ETA 会与实际执行不一致
    const int estConcGuess = chunkConcurrencyFor(threads, 0, opt.memBudgetBytes);
    const int64_t estTarget = chunkTargetFor(collectEst != 0, estConcGuess, H, hMax);
    const ChunkPlan plan = chunkPlanFor(H, wpr, hMax, planCap, estTarget);
    est.chunkRows = static_cast<int>(std::min<int64_t>(plan.dataRowsCap, INT32_MAX));
    est.evalChunks = plan.totalDataEvals * W;
    est.chunkConcurrency = chunkConcurrencyFor(threads, 0, opt.memBudgetBytes);

    const double conc = std::max(1, est.chunkConcurrency);
    est.scanWallSec = est.evalChunks / (est.scanRate * conc);
    if (est.matchRate > 0.0) {
        est.matchWallSec = static_cast<double>(est.windows) / (est.matchRate * conc);
    } else {
        est.matchWallSec = 0.0; // 无窗口可测（例如图案无有效格）：匹配阶段可忽略
    }
    est.totalSec = est.scanWallSec + est.matchWallSec;
    est.ok = est.scanRate > 0.0 && est.totalSec > 0.0;
    return est.ok;
}


// v3.12.3：基岩版缓存分带搜索。
// 每个 band 只加载与该带（含 halo）相交的缓存 pack；band 结束立刻释放 CacheManager，
// 避免把整图位图/条目展开进内存。halo 保证图案/密度窗口跨带时结果不丢失；
// 只收集锚点落在核心 band 内的结果，避免相邻带重复。
void SlimeEngine::runBanded(const SearchOptions& optIn,
                            const std::atomic<bool>& cancel,
                            const ProgressFn& progress,
                            SearchOutcome& out) const noexcept
{
    out = SearchOutcome{};
    if (!optIn.cacheBandLoader) {
        run(optIn, cancel, progress, out);
        return;
    }
    if (!optIn.region().valid()) {
        out.error = "搜索范围非法";
        return;
    }

    const int bandSize = std::max(16, optIn.cacheBandSize);
    int halo = 1;
    if (optIn.mode == Mode::Strict) {
        halo = std::max({ optIn.pattern.rows(), optIn.pattern.cols(), 1 });
    } else {
        halo = std::max(optIn.gridSize, 1);
    }
    halo = std::max(2, halo + 1); // 安全余量：旋转/窗口边界都覆盖

    const int64_t x0 = optIn.minX, x1 = optIn.maxX;
    const int64_t z0 = optIn.minZ, z1 = optIn.maxZ;
    const int64_t coreW = bandSize;
    const int64_t nx = std::max<int64_t>(1, (x1 - x0 + coreW) / coreW);
    const int64_t nz = std::max<int64_t>(1, (z1 - z0 + coreW) / coreW);
    const int64_t totalBands = nx * nz;

    const bool byDensity = optIn.ignoreThreshold; // 最优候选：结果按窗口史莱姆数排序
    const auto dist2 = [&](const MatchResult& r) {
        // 与引擎 TopNHeap 同口径：窗口中心的 2×偏移平方距离。
        const int64_t wSize = static_cast<int64_t>(r.maxX) - r.minX + 1;
        const int64_t hSize = static_cast<int64_t>(r.maxZ) - r.minZ + 1;
        const int64_t off2x = static_cast<int64_t>(r.minX) * 2 + (wSize - 1) -
                              2 * static_cast<int64_t>(optIn.startX);
        const int64_t off2z = static_cast<int64_t>(r.minZ) * 2 + (hSize - 1) -
                              2 * static_cast<int64_t>(optIn.startZ);
        return off2x * off2x + off2z * off2z;
    };
    const auto better = [&](const MatchResult& a, const MatchResult& b) {
        if (byDensity) {
            if (a.density != b.density) return a.density > b.density;
        }
        const auto da = dist2(a), db = dist2(b);
        if (da != db) return da < db;
        // 与引擎光栅 rank 同口径：X 主序、Z 次序。
        const auto rankOf = [&](const MatchResult& r) {
            const int64_t hSize = static_cast<int64_t>(r.maxZ) - r.minZ + 1;
            const int64_t anchorRows = static_cast<int64_t>(optIn.maxZ) - optIn.minZ + 2 - hSize;
            return static_cast<int64_t>(r.minX - optIn.minX) * anchorRows +
                   static_cast<int64_t>(r.minZ - optIn.minZ);
        };
        const auto ra = rankOf(a), rb = rankOf(b);
        if (ra != rb) return ra < rb;
        return false;
    };

    std::map<std::tuple<int, int, int, int>, MatchResult> merged;
    int64_t totalFound = 0;
    int64_t scanned = 0;
    int64_t windows = 0;
    int64_t chunksPruned = 0;
    double scanMs = 0, matchMs = 0, totalMs = 0;
    int64_t cacheHitPoints = 0;
    int64_t cacheFirstChunks = 0;
    bool cacheFillSkipped = false;
    bool cacheFirstOrder = false;
    bool stoppedEarly = false;
    bool anyOk = false;
    int64_t bandIndex = 0;

    struct BandInfo {
        int64_t cx0 = 0, cx1 = 0, cz0 = 0, cz1 = 0;
        bool cached = false;
    };
    std::vector<BandInfo> bands;
    bands.reserve(static_cast<size_t>(totalBands));
    for (int64_t bz = 0; bz < nz; ++bz) {
        for (int64_t bx = 0; bx < nx; ++bx) {
            BandInfo b;
            b.cx0 = x0 + bx * coreW;
            b.cx1 = std::min<int64_t>(x1, b.cx0 + coreW - 1);
            b.cz0 = z0 + bz * coreW;
            b.cz1 = std::min<int64_t>(z1, b.cz0 + coreW - 1);
            if (optIn.cacheBandMayCover) {
                b.cached = optIn.cacheBandMayCover(Rect{ static_cast<int>(b.cx0), static_cast<int>(b.cx1),
                                                          static_cast<int>(b.cz0), static_cast<int>(b.cz1) });
            }
            bands.push_back(b);
        }
    }
    // 缓存优先：先处理有缓存的 band，尽早命中并减少无效计算；最终仍按原排序合并结果。
    std::stable_sort(bands.begin(), bands.end(), [](const BandInfo& a, const BandInfo& b) {
        return a.cached && !b.cached;
    });
    for (const BandInfo& band : bands) {
        const int64_t cx0 = band.cx0;
        const int64_t cx1 = band.cx1;
        const int64_t cz0 = band.cz0;
        const int64_t cz1 = band.cz1;
        {
            if (cancel.load(std::memory_order_relaxed)) {
                out.cancelled = true;
                out.ok = false;
                return;
            }
            Rect core{ static_cast<int>(cx0), static_cast<int>(cx1),
                       static_cast<int>(cz0), static_cast<int>(cz1) };
            Rect loaded{ static_cast<int>(cx0) - halo, static_cast<int>(cx1) + halo,
                         static_cast<int>(cz0) - halo, static_cast<int>(cz1) + halo };

            std::shared_ptr<CacheManager> bandCache = optIn.cacheBandLoader(loaded);
            if (!bandCache) bandCache = std::make_shared<CacheManager>();

            SearchOptions bandOpt = optIn;
            bandOpt.cacheBandLoader = nullptr;
            bandOpt.cacheBandMayCover = nullptr;
            bandOpt.cache = bandCache.get();
            bandOpt.cacheWrite = nullptr;
            bandOpt.cacheReadMu = nullptr;
            bandOpt.minX = loaded.minX;
            bandOpt.maxX = loaded.maxX;
            bandOpt.minZ = loaded.minZ;
            bandOpt.maxZ = loaded.maxZ;
            bandOpt.stopAtTarget = false;
            bandOpt.smartRange = false;

            const auto bandProgress = [&](double frac, bool scanning, double cps, double eta) {
                if (progress) {
                    const double total = static_cast<double>(totalBands);
                    progress((static_cast<double>(bandIndex) + std::clamp(frac, 0.0, 1.0)) / total,
                             scanning, cps, eta);
                }
            };

            SearchOutcome bo;
            run(bandOpt, cancel, bandProgress, bo);
            if (cancel.load(std::memory_order_relaxed)) {
                out.cancelled = true;
                out.ok = false;
                return;
            }
            if (!bo.ok) {
                out.error = bo.error;
                return;
            }
            anyOk = true;
            scanned += bo.stats.scannedChunks;
            windows += bo.stats.windowsEvaluated;
            chunksPruned += bo.chunksPruned;
            scanMs += bo.stats.scanMs;
            matchMs += bo.stats.matchMs;
            totalMs += bo.stats.totalMs;
            if (bo.stoppedEarly) stoppedEarly = true;
            totalFound += bo.totalFound;
            cacheHitPoints += bo.cacheHitPoints;
            cacheFirstChunks += bo.cacheFirstChunks;
            cacheFillSkipped = cacheFillSkipped || bo.cacheFillSkipped;
            cacheFirstOrder = cacheFirstOrder || bo.cacheFirstOrder;

            for (const auto& r : bo.results) {
                // 只接收锚点位于核心带内的结果；halo 只用于计算，不用于输出。
                // 同时要求窗口完全落在原始搜索范围内，避免 halo 边界产生旧引擎不会返回的结果。
                if (r.minX < optIn.minX || r.maxX > optIn.maxX ||
                    r.minZ < optIn.minZ || r.maxZ > optIn.maxZ) continue;
                if (!core.contains(r.minX, r.minZ)) continue;
                const auto key = std::make_tuple(r.minX, r.maxX, r.minZ, r.maxZ);
                auto it = merged.find(key);
                if (it == merged.end() || better(r, it->second)) merged[key] = r;
            }
        }
        ++bandIndex;
    }

    if (cancel.load(std::memory_order_relaxed)) {
        out.cancelled = true;
        out.ok = false;
        return;
    }
    if (!anyOk) {
        out.error = "缓存分带搜索失败";
        return;
    }

    std::vector<MatchResult> results;
    results.reserve(merged.size());
    for (auto& [key, r] : merged) {
        (void)key;
        results.push_back(r);
    }
    std::sort(results.begin(), results.end(), better);
    if (optIn.targetCount > 0 && static_cast<int64_t>(results.size()) > optIn.targetCount) {
        results.resize(static_cast<size_t>(optIn.targetCount));
    }

    out.ok = true;
    out.results = std::move(results);
    // halo 内重复命中只影响统计，不应改变最终显示结果。
    out.totalFound = static_cast<int64_t>(out.results.size());
    out.stoppedEarly = stoppedEarly;
    out.cacheHitPoints = cacheHitPoints;
    out.cacheFillSkipped = cacheFillSkipped;
    out.cacheFirstOrder = cacheFirstOrder;
    out.cacheFirstChunks = cacheFirstChunks;
    out.stats.threadsUsed = optIn.threads;
    out.stats.scannedChunks = scanned;
    out.stats.windowsEvaluated = windows;
    out.stats.scanMs = scanMs;
    out.stats.matchMs = matchMs;
    out.stats.totalMs = totalMs;
    out.chunksPruned = chunksPruned;
    out.chunkedPath = true;
}
} // namespace slime

