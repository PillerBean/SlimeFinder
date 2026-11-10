// FastMt.cpp - AVX2 四通道批量 MT19937 内核实现（基岩版史莱姆区块判定）
//
// 本文件是项目中除 FastMatch.cpp 外唯一启用 AVX2 的编译单元：CMake 对本文件单独
// 添加 /arch:AVX2（MSVC）或 -mavx2（GCC/Clang）；未启用时全部自动编译为标量回退
// （见文件末尾的 #else 分支）。所有 AVX2 入口在运行期先经 cpuHasAvx2() 检查。
//
// ------------------------------ 与引擎的逐位等价性 ------------------------------
// 参考实现（src/core/SlimeEngine.cpp，不得修改）：
//   seed = low32(cx * 0x1F1F1F1F) ^ uint32(cz)                       // bedrockChunkSeed
//   mt[0] = seed
//   mt[i] = (1812433253 * (mt[i-1] ^ (mt[i-1] >> 30)) + i) mod 2^32   // i = 1..623
//   twist:  i=0..226   y=(mt[i]&0x80000000)|(mt[i+1]&0x7FFFFFFF); mt[i]=mt[i+397]^(y>>1)^(0x9908B0DF & -(y&1))
//           i=227..622 同上，右操作数为 mt[i-227]
//           i=623      y=(mt[623]&0x80000000)|(mt[0]&0x7FFFFFFF); mt[623]=mt[396]^(y>>1)^(0x9908B0DF & -(y&1))
//   v = temper(mt[0])，temper: v^=v>>11; v^=(v<<7)&0x9D2C5680; v^=(v<<15)&0xEFC60000; v^=v>>18
//   史莱姆 ⟺ v % 10 == 0
//
// ------------------------------ 核心代数优化 ------------------------------
// 关键观察：首次输出只依赖 twist 之后的 mt[0]，而 twist 的第一次迭代（i=0）写入的
// 正是 mt[0]，它读取的三个旧字是 mt[0]、mt[1]（构造 y）与 mt[397]（被异或项）；
// 由于 twist 按 i 递增写入，这三个字在第 0 次迭代时都还是 init 阶段的原始值。于是：
//     y      = (mt[0] & 0x80000000) | (mt[1] & 0x7FFFFFFF)          // init 值
//     mt0'   = mt[397] ^ (y >> 1) ^ (0x9908B0DF & -(y & 1))          // init 值
//     v      = temper(mt0')
// 也就是说整条链只需要 init 的 mt[0]、mt[1]、mt[397] 三个字：
//   原实现：624 步 init + 624 步 twist ≈ 1.2 万次标量运算/区块
//   本实现：396 步 init 递推 + 常数次运算/区块（bit-exact，由自测 100 万+ 样本验证）
// AVX2 侧再以“4 个 lane = 4 个区块”并行，并用多条独立链（batch8/16/32 交织）填充
// vpmulld 的长延迟——init 递推是串行依赖链，单链是延迟瓶颈，多链才吃得满吞吐
// （实测 Zen 3：1 链 7.9M/s → 2 链 14.8M/s → 4 链 23.8M/s → 8 链 36.0M/s）。
#include "core/FastMt.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>

#if defined(_MSC_VER)
#include <immintrin.h>
#include <intrin.h>
#elif defined(__AVX2__)
#include <immintrin.h>
#endif

// %10 判定实现方式（0 = 标量抽取，1 = AVX2 魔数除法）。
// 两者结果完全一致，仅影响少量尾部指令；默认值由实测吞吐选定（见自测报告）。
#ifndef FASTMT_VECTOR_MOD
#define FASTMT_VECTOR_MOD 1
#endif

namespace slime {
namespace fastmt {

// ============================ 运行时 CPU 检测 ============================

bool cpuHasAvx2() noexcept
{
    static const bool ok = []() -> bool {
#if defined(_MSC_VER)
        int regs[4] = { 0, 0, 0, 0 };
        __cpuid(regs, 0);
        if (regs[0] < 7) return false; // 不支持 CPUID.7
        __cpuid(regs, 1);
        const bool osxsave = (regs[2] & (1 << 27)) != 0;
        const bool avx = (regs[2] & (1 << 28)) != 0;
        if (!osxsave || !avx) return false;
        const unsigned long long xcr0 = _xgetbv(0);
        if ((xcr0 & 0x6) != 0x6) return false; // XMM + YMM 状态未启用
        __cpuidex(regs, 7, 0);
        return (regs[1] & (1 << 5)) != 0; // CPUID.7.EBX.AVX2
#elif defined(__GNUC__) && (defined(__x86_64__) || defined(__i386__))
        __builtin_cpu_init();
        return __builtin_cpu_supports("avx2") != 0;
#else
        return false;
#endif
    }();
    return ok;
}

// ============================ 常量与标量工具 ============================

namespace {

constexpr uint32_t kSeedMul = 0x1F1F1F1FU;      // 基岩版区块种子乘数
constexpr uint32_t kMtInitMul = 1812433253U;    // MT19937 init 乘数
constexpr uint32_t kTwistMagic = 0x9908B0DFU;   // twist 掩码
constexpr int kMtIndex397 = 397;                // twist 第一步需要的最远 init 字

inline uint32_t umul32Lo(uint32_t a, uint32_t b) noexcept
{
    return static_cast<uint32_t>((static_cast<uint64_t>(a) * static_cast<uint64_t>(b)) & 0xFFFFFFFFULL);
}

// 区块种子：low32(cx * 0x1F1F1F1F) ^ czu（与引擎 bedrockChunkSeed 一致）
inline uint32_t chunkSeed(int32_t cx, uint32_t czu) noexcept
{
    return umul32Lo(static_cast<uint32_t>(cx), kSeedMul) ^ czu;
}

// ------------------------- 种子查表（任务书 P0-2.2） -------------------------
// T[lo] = low32(lo * K)，lo = cx & 0xFFFF（65536 × 4B = 256KB，首次调用时构建一次）。
// 拼接依据（uint32 回绕）：
//   cx*K = (hi*2^16 + lo)*K = hi*(K<<16) + lo*K
//   ⇒ low32(cx*K) = low32(hi * low32(K<<16)) + T[lo]
// 只用一次表查 + 一次 16 位乘法，省掉原来的 32 位乘法（乘法本身在现代 CPU 上只需
// 1 个周期，是否真的更快见 kernel_selftest 的 A/B 实测结论）。
constexpr uint32_t kSeedMulHi = static_cast<uint32_t>(kSeedMul << 16); // low32(K << 16)

const uint32_t* seedTable() noexcept
{
    struct Table {
        uint32_t v[65536];
        Table() noexcept
        {
            uint32_t* p = v;
#if defined(_MSC_VER)
#pragma loop(no_vector) // 建表循环保持纯标量：本函数可在无 AVX2 的机器上安全执行
#endif
            for (uint32_t i = 0; i < 65536U; ++i) {
                p[i] = i * kSeedMul;
            }
        }
    };
    static const Table t;
    return t.v;
}

inline uint32_t chunkSeedTable(uint32_t cxu, uint32_t czu) noexcept
{
    const uint32_t lo = seedTable()[cxu & 0xFFFFU];
    const uint32_t hi = static_cast<uint32_t>(cxu >> 16) * kSeedMulHi;
    return (lo + hi) ^ czu;
}

// 查表开关（-1 = 尚未初始化，首次读取时取环境变量 SLIME_MT_TABLE）
std::atomic<int> g_seedTableFlag{ -1 };

bool seedTableOn() noexcept
{
    int v = g_seedTableFlag.load(std::memory_order_relaxed);
    if (v < 0) {
        const char* e = std::getenv("SLIME_MT_TABLE");
        v = (e != nullptr && e[0] != '\0' && e[0] != '0') ? 1 : 0;
        g_seedTableFlag.store(v, std::memory_order_relaxed);
    }
    return v != 0;
}

// 种子选择：kUseTable = true 走查表，false 走直接乘法（结果逐位一致）
template <bool kUseTable>
inline uint32_t seedFor(uint32_t cxu, uint32_t czu) noexcept
{
    return kUseTable ? chunkSeedTable(cxu, czu) : (umul32Lo(cxu, kSeedMul) ^ czu);
}

// MT19937 temper（首次加扰输出）
inline uint32_t temper32(uint32_t v) noexcept
{
    v ^= (v >> 11);
    v ^= (v << 7) & 0x9D2C5680UL;
    v ^= (v << 15) & 0xEFC60000UL;
    v ^= (v >> 18);
    return v;
}

inline uint8_t slimeFromDraw(uint32_t v) noexcept
{
    return (v % 10U) == 0U ? static_cast<uint8_t>(1) : static_cast<uint8_t>(0);
}

// 完整参考实现：624 步 init + 完整 twist + 首次加扰（与 SlimeEngine.cpp 的
// bedrockFirstDraw 逐行同构，含三段 twist 的索引划分）。
uint32_t firstDrawFull(uint32_t seed) noexcept
{
    uint32_t mt[624];
    mt[0] = seed;
    for (int i = 1; i < 624; ++i) {
        mt[i] = (1812433253UL * (mt[i - 1] ^ (mt[i - 1] >> 30)) + static_cast<uint32_t>(i)) & 0xFFFFFFFFUL;
    }
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
    return temper32(mt[0]);
}

// 代数等价捷径（见文件头证明）：只需 init 链上的 mt[0]、mt[1]、mt[397]。
uint32_t firstDrawShortcut(uint32_t seed) noexcept
{
    const uint32_t m0 = seed;
    const uint32_t m1 = kMtInitMul * (m0 ^ (m0 >> 30)) + 1U; // mt[1]
    uint32_t cur = m1;
    for (uint32_t i = 2; i <= static_cast<uint32_t>(kMtIndex397); ++i) {
        cur = kMtInitMul * (cur ^ (cur >> 30)) + i;
    }
    const uint32_t m397 = cur;
    const uint32_t y = (m0 & 0x80000000UL) | (m1 & 0x7FFFFFFFUL);
    const uint32_t v = m397 ^ (y >> 1) ^ (kTwistMagic & static_cast<uint32_t>(-static_cast<int32_t>(y & 1UL)));
    return temper32(v);
}

// 标量 4 区块（可按需选择“完整参考”或“捷径”实现）
template <bool kShortcut>
void scalarBatch4(const int32_t* cx, const uint32_t* czu, uint8_t* outSlime4) noexcept
{
    for (int j = 0; j < 4; ++j) {
        const uint32_t s = chunkSeed(cx[j], czu[j]);
        outSlime4[j] = slimeFromDraw(kShortcut ? firstDrawShortcut(s) : firstDrawFull(s));
    }
}

// 诊断开关：SLIME_FORCE_SCALAR=1 时所有批量/扫描入口强制走标量（结果必须一致）
bool useAvx2() noexcept
{
    static const bool forceScalar = []() -> bool {
        const char* v = std::getenv("SLIME_FORCE_SCALAR");
        return v != nullptr && v[0] != '\0' && v[0] != '0';
    }();
    return !forceScalar && cpuHasAvx2();
}

// 运行期回退（cpuHasAvx2()==false 时）统一使用“捷径”标量实现：
// 它只含标量整数指令，即使本文件整体以 /arch:AVX2 编译也不会生成 AVX 指令
// （已用 dumpbin 逐函数核对），因此在无 AVX2 的老 CPU 上调用也是安全的。
// 注意：bedrockSlimeBatch4Scalar 走的是 firstDrawFull（完整 624+twist 参考实现），
// MSVC 会对它的 twist 循环自动向量化出 ymm/AVX2 指令——它只用于一致性对照测试，
// 不要作为“无 AVX2 老机器”的回退路径（那种场景请使用未启用 /arch:AVX2 的构建，
// 项目本就对 SlimeEngine.cpp 保持 SSE2 编译）。
inline void scalarFallbackN(const int32_t* cx, const uint32_t* czu, uint8_t* out, int groups) noexcept
{
    for (int g = 0; g < groups; ++g) {
        scalarBatch4<true>(cx + 4 * g, czu + 4 * g, out + 4 * g);
    }
}

// ------------------------- 标量扫描（模板：种子函数可切换） -------------------------
// kUseTable = false 时与既有 bedrockSlimeScanScalar 逐行同构（仅把 chunkSeed 换成
// 等价的 umul32Lo 写法），= true 时只把种子换成查表实现；两者结果逐位一致。
template <bool kUseTable>
void scanContiguousScalar(int32_t cx0, int32_t cz, int32_t count, uint8_t* out, bool startAlignedTo4) noexcept
{
    if (count <= 0 || out == nullptr) return;
    const uint32_t czu = static_cast<uint32_t>(cz);
    uint32_t cxs = static_cast<uint32_t>(cx0);
    int64_t n = count;
    uint8_t* o = out;
    if (!startAlignedTo4 || (cxs & 3U) != 0U) {
        while (n > 0 && (cxs & 3U) != 0U) {
            *o++ = slimeFromDraw(firstDrawShortcut(seedFor<kUseTable>(cxs, czu)));
            ++cxs;
            --n;
        }
    }
    while (n >= 4) {
        for (int j = 0; j < 4; ++j) {
            const uint32_t s = seedFor<kUseTable>(cxs + static_cast<uint32_t>(j), czu);
            o[j] = slimeFromDraw(firstDrawShortcut(s));
        }
        cxs += 4;
        o += 4;
        n -= 4;
    }
    while (n > 0) {
        *o++ = slimeFromDraw(firstDrawShortcut(seedFor<kUseTable>(cxs, czu)));
        ++cxs;
        --n;
    }
}

// 跨步采样扫描（标量）：cx = cx0 + i*step（32 位回绕），step ≤ 0 按 1 处理
template <bool kUseTable>
void scanStridedScalar(int32_t cx0, uint32_t czu, int32_t count, int32_t step, uint8_t* out) noexcept
{
    if (count <= 0 || out == nullptr) return;
    const uint32_t st = static_cast<uint32_t>(step < 1 ? 1 : step);
    uint32_t cxs = static_cast<uint32_t>(cx0);
    for (int32_t i = 0; i < count; ++i) {
        out[i] = slimeFromDraw(firstDrawShortcut(seedFor<kUseTable>(cxs, czu)));
        cxs += st;
    }
}

} // namespace

// ============================ 标量公开接口 ============================

uint32_t bedrockChunkSeedScalar(int32_t cx, uint32_t czu) noexcept
{
    return chunkSeed(cx, czu);
}

uint32_t bedrockFirstDrawScalar(uint32_t seed) noexcept
{
    return firstDrawFull(seed);
}

uint32_t bedrockFirstDrawScalarShortcut(uint32_t seed) noexcept
{
    return firstDrawShortcut(seed);
}

void bedrockSlimeBatch4Scalar(const int32_t* cx, const uint32_t* czu, uint8_t* outSlime4) noexcept
{
    scalarBatch4<false>(cx, czu, outSlime4);
}

// ============================ AVX2 内核 ============================

#if defined(__AVX2__)

namespace {

inline __m256i bc(uint32_t v) noexcept { return _mm256_set1_epi32(static_cast<int32_t>(v)); }

// 只读取 4 个 32 位元素（128 位加载 + 高 128 位置零）：
// 调用方只保证数组有 4 项，绝不能用 256 位加载越界读取。
inline __m256i load4i(const int32_t* p) noexcept
{
    return _mm256_zextsi128_si256(_mm_loadu_si128(reinterpret_cast<const __m128i*>(p)));
}

inline __m256i load4u(const uint32_t* p) noexcept
{
    return _mm256_zextsi128_si256(_mm_loadu_si128(reinterpret_cast<const __m128i*>(p)));
}

// 4 元素 → 256 位（低 4 lane 有效，高 4 lane 复制低 4 lane；仅用低 4 lane 参与计算）
inline __m256i set4(int32_t a, int32_t b, int32_t c, int32_t d) noexcept
{
    return _mm256_setr_epi32(a, b, c, d, a, b, c, d);
}

// temper 的 4 lane 版本
inline __m256i temperLanes(__m256i v) noexcept
{
    v = _mm256_xor_si256(v, _mm256_srli_epi32(v, 11));
    v = _mm256_xor_si256(v, _mm256_and_si256(_mm256_slli_epi32(v, 7), bc(0x9D2C5680U)));
    v = _mm256_xor_si256(v, _mm256_and_si256(_mm256_slli_epi32(v, 15), bc(0xEFC60000U)));
    v = _mm256_xor_si256(v, _mm256_srli_epi32(v, 18));
    return v;
}

// 由 init 链上的 mt[0]、mt[1]、mt[397] 直接得到 twist 后 mt[0] 的首次加扰输出
inline __m256i finishDraw(__m256i m0, __m256i m1, __m256i m397) noexcept
{
    const __m256i y = _mm256_or_si256(_mm256_and_si256(m0, bc(0x80000000U)),
                                      _mm256_and_si256(m1, bc(0x7FFFFFFFU)));
    const __m256i parity = _mm256_and_si256(y, bc(1U));                        // y & 1
    const __m256i neg = _mm256_sub_epi32(_mm256_setzero_si256(), parity);      // 0 / 0xFFFFFFFF
    __m256i v = _mm256_xor_si256(m397, _mm256_srli_epi32(y, 1));
    v = _mm256_xor_si256(v, _mm256_and_si256(neg, bc(kTwistMagic)));
    return temperLanes(v);
}

// 4 lane 的 %10 == 0 判定 → 低 4 位掩码（bit j = lane j）
#if FASTMT_VECTOR_MOD
inline uint32_t slimeMask4(__m256i v) noexcept
{
    // 无符号 32 位除法魔数：q = (uint64)v * 0xCCCCCCCD >> 35 == v / 10（精确，全值域）
    const __m256i magic = bc(0xCCCCCCCDU);
    const __m256i evenProd = _mm256_mul_epu32(v, magic);                 // 偶 lane 的 64 位积
    const __m256i oddIn = _mm256_srli_epi64(v, 32);                      // 奇 lane 移到偶位置
    const __m256i oddProd = _mm256_mul_epu32(oddIn, magic);
    const __m256i qEven = _mm256_srli_epi64(evenProd, 35);               // 偶 lane 商（低 32 位）
    const __m256i qOdd = _mm256_slli_epi64(_mm256_srli_epi64(oddProd, 35), 32); // 奇 lane 商回高位
    const __m256i q = _mm256_blend_epi32(qEven, qOdd, 0xAA);
    const __m256i r = _mm256_sub_epi32(v, _mm256_mullo_epi32(q, bc(10U)));
    const __m256i z = _mm256_cmpeq_epi32(r, _mm256_setzero_si256());
    return static_cast<uint32_t>(_mm256_movemask_ps(_mm256_castsi256_ps(z))) & 0xFU;
}

inline void storeDraw4(__m256i v, uint8_t* out4) noexcept
{
    const uint32_t m = slimeMask4(v);
    out4[0] = static_cast<uint8_t>(m & 1U);
    out4[1] = static_cast<uint8_t>((m >> 1) & 1U);
    out4[2] = static_cast<uint8_t>((m >> 2) & 1U);
    out4[3] = static_cast<uint8_t>((m >> 3) & 1U);
}
#else
inline void storeDraw4(__m256i v, uint8_t* out4) noexcept
{
    alignas(32) uint32_t t[8];
    _mm256_store_si256(reinterpret_cast<__m256i*>(t), v);
    out4[0] = slimeFromDraw(t[0]);
    out4[1] = slimeFromDraw(t[1]);
    out4[2] = slimeFromDraw(t[2]);
    out4[3] = slimeFromDraw(t[3]);
}
#endif

// 一条链的一步：iv 为当前 i 的广播向量，cA 为 init 乘数广播
#define FASTMT_CHAIN_STEP(C)                                                     \
    do {                                                                         \
        const __m256i t_ = _mm256_xor_si256((C), _mm256_srli_epi32((C), 30));     \
        (C) = _mm256_add_epi32(_mm256_mullo_epi32(t_, cA), iv);                   \
    } while (0)

// 单链：4 个区块（lane j = 区块 j），init 递推 1..397 + 首次输出
inline void draw4(__m256i seeds, uint8_t* out4) noexcept
{
    const __m256i cA = bc(kMtInitMul);
    const __m256i cOne = bc(1U);
    const __m256i m0 = seeds;
    const __m256i m1 = _mm256_add_epi32(
        _mm256_mullo_epi32(_mm256_xor_si256(m0, _mm256_srli_epi32(m0, 30)), cA), cOne);
    __m256i c0 = m1;
    __m256i iv = cOne;
    for (int i = 2; i <= kMtIndex397; ++i) {
        iv = _mm256_add_epi32(iv, cOne);
        FASTMT_CHAIN_STEP(c0);
    }
    storeDraw4(finishDraw(m0, m1, c0), out4);
}

// 双链：8 个区块（2×4 lane 交织，隐藏 vpmulld 延迟）
inline void draw8(__m256i seedsA, __m256i seedsB, uint8_t* out8) noexcept
{
    const __m256i cA = bc(kMtInitMul);
    const __m256i cOne = bc(1U);
    const __m256i a0 = seedsA;
    const __m256i b0 = seedsB;
    const __m256i a1 = _mm256_add_epi32(
        _mm256_mullo_epi32(_mm256_xor_si256(a0, _mm256_srli_epi32(a0, 30)), cA), cOne);
    const __m256i b1 = _mm256_add_epi32(
        _mm256_mullo_epi32(_mm256_xor_si256(b0, _mm256_srli_epi32(b0, 30)), cA), cOne);
    __m256i ca = a1;
    __m256i cb = b1;
    __m256i iv = cOne;
    for (int i = 2; i <= kMtIndex397; ++i) {
        iv = _mm256_add_epi32(iv, cOne);
        FASTMT_CHAIN_STEP(ca);
        FASTMT_CHAIN_STEP(cb);
    }
    storeDraw4(finishDraw(a0, a1, ca), out8);
    storeDraw4(finishDraw(b0, b1, cb), out8 + 4);
}

// 四链：16 个区块（扫描内核主体；4×4 lane 交替发射，吞吐优先）
inline void draw16(__m256i seedsA, __m256i seedsB, __m256i seedsC, __m256i seedsD, uint8_t* out16) noexcept
{
    const __m256i cA = bc(kMtInitMul);
    const __m256i cOne = bc(1U);
    const __m256i a0 = seedsA;
    const __m256i b0 = seedsB;
    const __m256i c0s = seedsC;
    const __m256i d0 = seedsD;
    const __m256i a1 = _mm256_add_epi32(
        _mm256_mullo_epi32(_mm256_xor_si256(a0, _mm256_srli_epi32(a0, 30)), cA), cOne);
    const __m256i b1 = _mm256_add_epi32(
        _mm256_mullo_epi32(_mm256_xor_si256(b0, _mm256_srli_epi32(b0, 30)), cA), cOne);
    const __m256i c1 = _mm256_add_epi32(
        _mm256_mullo_epi32(_mm256_xor_si256(c0s, _mm256_srli_epi32(c0s, 30)), cA), cOne);
    const __m256i d1 = _mm256_add_epi32(
        _mm256_mullo_epi32(_mm256_xor_si256(d0, _mm256_srli_epi32(d0, 30)), cA), cOne);
    __m256i ca = a1;
    __m256i cb = b1;
    __m256i cc = c1;
    __m256i cd = d1;
    __m256i iv = cOne;
    for (int i = 2; i <= kMtIndex397; ++i) {
        iv = _mm256_add_epi32(iv, cOne);
        FASTMT_CHAIN_STEP(ca);
        FASTMT_CHAIN_STEP(cb);
        FASTMT_CHAIN_STEP(cc);
        FASTMT_CHAIN_STEP(cd);
    }
    storeDraw4(finishDraw(a0, a1, ca), out16);
    storeDraw4(finishDraw(b0, b1, cb), out16 + 4);
    storeDraw4(finishDraw(c0s, c1, cc), out16 + 8);
    storeDraw4(finishDraw(d0, d1, cd), out16 + 12);
}

// 由 4 组 (cx, cz) 生成 4 个 seed 向量
inline void seeds16(const int32_t* cx, const uint32_t* czu, __m256i& sA, __m256i& sB, __m256i& sC, __m256i& sD) noexcept
{
    const __m256i k = bc(kSeedMul);
    sA = _mm256_xor_si256(_mm256_mullo_epi32(load4i(cx), k), load4u(czu));
    sB = _mm256_xor_si256(_mm256_mullo_epi32(load4i(cx + 4), k), load4u(czu + 4));
    sC = _mm256_xor_si256(_mm256_mullo_epi32(load4i(cx + 8), k), load4u(czu + 8));
    sD = _mm256_xor_si256(_mm256_mullo_epi32(load4i(cx + 12), k), load4u(czu + 12));
}

// 八链：32 个区块（进一步填满发射槽；实测吞吐高于四链，见自测基准）
inline void draw32(const __m256i seeds[8], uint8_t* out32) noexcept
{
    const __m256i cA = bc(kMtInitMul);
    const __m256i cOne = bc(1U);
    __m256i m0[8];
    __m256i c[8];
    for (int g = 0; g < 8; ++g) {
        m0[g] = seeds[g];
        const __m256i t = _mm256_xor_si256(m0[g], _mm256_srli_epi32(m0[g], 30));
        c[g] = _mm256_add_epi32(_mm256_mullo_epi32(t, cA), cOne);
    }
    __m256i ca = c[0], cb = c[1], cc = c[2], cd = c[3];
    __m256i ce = c[4], cf = c[5], cg = c[6], ch = c[7];
    __m256i iv = cOne;
    for (int i = 2; i <= kMtIndex397; ++i) {
        iv = _mm256_add_epi32(iv, cOne);
        FASTMT_CHAIN_STEP(ca);
        FASTMT_CHAIN_STEP(cb);
        FASTMT_CHAIN_STEP(cc);
        FASTMT_CHAIN_STEP(cd);
        FASTMT_CHAIN_STEP(ce);
        FASTMT_CHAIN_STEP(cf);
        FASTMT_CHAIN_STEP(cg);
        FASTMT_CHAIN_STEP(ch);
    }
    storeDraw4(finishDraw(m0[0], c[0], ca), out32);
    storeDraw4(finishDraw(m0[1], c[1], cb), out32 + 4);
    storeDraw4(finishDraw(m0[2], c[2], cc), out32 + 8);
    storeDraw4(finishDraw(m0[3], c[3], cd), out32 + 12);
    storeDraw4(finishDraw(m0[4], c[4], ce), out32 + 16);
    storeDraw4(finishDraw(m0[5], c[5], cf), out32 + 20);
    storeDraw4(finishDraw(m0[6], c[6], cg), out32 + 24);
    storeDraw4(finishDraw(m0[7], c[7], ch), out32 + 28);
}

// v3.6.21：8 lane/链 —— 一条链用一个 256 位寄存器装 8 个区块（此前只用了低 4 lane，
//   浪费一半通道）。init 递推与 twist 全是 32 位运算，8 lane 天然可用；
//   末端的 temper / %10 逐 lane 落回标量（每区块一次，可忽略）。
//   探针实测：4 lane/链 42.15 ns/区块 → 8 lane/链 21.40 ns/区块（1.97×，逐位一致）。
inline void draw32x8(uint32_t cx0, uint32_t czu, uint8_t* out32) noexcept
{
    const __m256i c = bc(kMtInitMul);
    const __m256i czv = bc(czu);
    const __m256i stepK = bc(8U * kSeedMul);
    __m256i base = _mm256_mullo_epi32(
        _mm256_setr_epi32(static_cast<int>(cx0), static_cast<int>(cx0 + 1), static_cast<int>(cx0 + 2),
                          static_cast<int>(cx0 + 3), static_cast<int>(cx0 + 4), static_cast<int>(cx0 + 5),
                          static_cast<int>(cx0 + 6), static_cast<int>(cx0 + 7)),
        bc(kSeedMul));
    __m256i s[4], m0[4], m1[4];
    for (int g = 0; g < 4; ++g) {
        s[g] = _mm256_xor_si256(base, czv);
        base = _mm256_add_epi32(base, stepK);
    }
    for (int g = 0; g < 4; ++g) {
        m0[g] = s[g];
        const __m256i t = _mm256_xor_si256(m0[g], _mm256_srli_epi32(m0[g], 30));
        m1[g] = _mm256_add_epi32(_mm256_mullo_epi32(t, c), bc(1U));
        s[g] = m1[g];
    }
    __m256i iv = bc(1U);
    for (int i = 2; i <= kMtIndex397; ++i) {
        iv = _mm256_add_epi32(iv, bc(1U));
        for (int g = 0; g < 4; ++g) {
            const __m256i t = _mm256_xor_si256(s[g], _mm256_srli_epi32(s[g], 30));
            s[g] = _mm256_add_epi32(_mm256_mullo_epi32(t, c), iv);
        }
    }
    for (int g = 0; g < 4; ++g) {
        const __m256i y = _mm256_or_si256(_mm256_and_si256(m0[g], bc(0x80000000U)),
                                          _mm256_and_si256(m1[g], bc(0x7FFFFFFFU)));
        const __m256i neg = _mm256_sub_epi32(_mm256_setzero_si256(), _mm256_and_si256(y, bc(1U)));
        __m256i v = _mm256_xor_si256(s[g], _mm256_srli_epi32(y, 1));
        v = _mm256_xor_si256(v, _mm256_and_si256(neg, bc(kTwistMagic)));
        v = _mm256_xor_si256(v, _mm256_srli_epi32(v, 11));
        v = _mm256_xor_si256(v, _mm256_and_si256(_mm256_slli_epi32(v, 7), bc(0x9D2C5680U)));
        v = _mm256_xor_si256(v, _mm256_and_si256(_mm256_slli_epi32(v, 15), bc(0xEFC60000U)));
        v = _mm256_xor_si256(v, _mm256_srli_epi32(v, 18));
        alignas(32) uint32_t tmp[8];
        _mm256_store_si256(reinterpret_cast<__m256i*>(tmp), v);
        for (int j = 0; j < 8; ++j) out32[g * 8 + j] = slimeFromDraw(tmp[j]);
    }
}

// 连续 cx 的 16 区块：seed = (cx0+j)*K ^ czu；按 K 的倍数递增（乘法对加法分配）
inline void scan16(uint32_t cxFirst, uint32_t czu, uint8_t* out16) noexcept
{
    const __m256i k = bc(kSeedMul);
    const __m256i czv = bc(czu);
    const __m256i base = set4(static_cast<int32_t>(cxFirst), static_cast<int32_t>(cxFirst + 1U),
                              static_cast<int32_t>(cxFirst + 2U), static_cast<int32_t>(cxFirst + 3U));
    const __m256i step = bc(4U * kSeedMul);
    const __m256i pA = _mm256_mullo_epi32(base, k);
    const __m256i pB = _mm256_add_epi32(pA, step);
    const __m256i pC = _mm256_add_epi32(pB, step);
    const __m256i pD = _mm256_add_epi32(pC, step);
    draw16(_mm256_xor_si256(pA, czv), _mm256_xor_si256(pB, czv),
           _mm256_xor_si256(pC, czv), _mm256_xor_si256(pD, czv), out16);
}

// 连续 cx 的 4 区块（尾部）
inline void scan4(uint32_t cxFirst, uint32_t czu, uint8_t* out4) noexcept
{
    const __m256i base = set4(static_cast<int32_t>(cxFirst), static_cast<int32_t>(cxFirst + 1U),
                              static_cast<int32_t>(cxFirst + 2U), static_cast<int32_t>(cxFirst + 3U));
    draw4(_mm256_xor_si256(_mm256_mullo_epi32(base, bc(kSeedMul)), bc(czu)), out4);
}

// 连续 cx 的 32 区块（八链）
inline void scan32(uint32_t cxFirst, uint32_t czu, uint8_t* out32) noexcept
{
    const __m256i k = bc(kSeedMul);
    const __m256i czv = bc(czu);
    const __m256i step = bc(4U * kSeedMul);
    const __m256i base = set4(static_cast<int32_t>(cxFirst), static_cast<int32_t>(cxFirst + 1U),
                              static_cast<int32_t>(cxFirst + 2U), static_cast<int32_t>(cxFirst + 3U));
    __m256i p = _mm256_mullo_epi32(base, k);
    __m256i s[8];
    for (int g = 0; g < 8; ++g) {
        s[g] = _mm256_xor_si256(p, czv);
        p = _mm256_add_epi32(p, step);
    }
    draw32(s, out32);
}

// 跨步采样的 32 个采样点（8 链 × 4 lane）：
//   cx(采样点) = cxFirst + i*step，种子 = low32(cx*K) ^ czu
//   乘法对加法分配 ⇒ cx*K = cxFirst*K + i*(step*K)（全部 mod 2^32），
//   于是整批只需 2 次 vpmulld：一次把 lane 序号 0..3 乘上 step*K，
//   一次把 cxFirst*K（标量）加进去；链间推进只是常数加法 4*step*K。
inline void stridedScan32(uint32_t cxFirst, uint32_t stepK, uint32_t czu, uint8_t* out32) noexcept
{
    const __m256i base = _mm256_add_epi32(
        _mm256_mullo_epi32(set4(0, 1, 2, 3), bc(stepK)),
        bc(cxFirst * kSeedMul));
    const __m256i stride = bc(4U * stepK);
    const __m256i czv = bc(czu);
    __m256i p = base;
    __m256i s[8];
    for (int g = 0; g < 8; ++g) {
        s[g] = _mm256_xor_si256(p, czv);
        p = _mm256_add_epi32(p, stride);
    }
    draw32(s, out32);
}

// 跨步采样的 8 个采样点（2 链交织，用于 32 之后的中间尾巴）
inline void stridedScan8(uint32_t cxFirst, uint32_t stepK, uint32_t czu, uint8_t* out8) noexcept
{
    const __m256i base = _mm256_add_epi32(
        _mm256_mullo_epi32(set4(0, 1, 2, 3), bc(stepK)),
        bc(cxFirst * kSeedMul));
    const __m256i czv = bc(czu);
    const __m256i sA = _mm256_xor_si256(base, czv);
    const __m256i sB = _mm256_xor_si256(_mm256_add_epi32(base, bc(4U * stepK)), czv);
    draw8(sA, sB, out8);
}

// 跨步采样的 4 个采样点（单链）
inline void stridedDraw4(uint32_t cxFirst, uint32_t stepK, uint32_t czu, uint8_t* out4) noexcept
{
    const __m256i base = _mm256_add_epi32(
        _mm256_mullo_epi32(set4(0, 1, 2, 3), bc(stepK)),
        bc(cxFirst * kSeedMul));
    draw4(_mm256_xor_si256(base, bc(czu)), out4);
}

// 字面版 4 lane 内核：完整 624 步 init + 完整 twist（仅作对照验证，见头文件说明）
inline void draw4FullTwist(__m256i seeds, uint8_t* out4) noexcept
{
    alignas(32) __m256i mt[624];
    const __m256i cA = bc(kMtInitMul);
    const __m256i cOne = bc(1U);
    const __m256i cHi = bc(0x80000000U);
    const __m256i cLo = bc(0x7FFFFFFFU);
    const __m256i cMagic = bc(kTwistMagic);
    const __m256i cZero = _mm256_setzero_si256();

    mt[0] = seeds;
    {
        __m256i iv = cOne;
        for (int i = 1; i < 624; ++i) {
            const __m256i prev = mt[i - 1];
            const __m256i t = _mm256_xor_si256(prev, _mm256_srli_epi32(prev, 30));
            mt[i] = _mm256_add_epi32(_mm256_mullo_epi32(t, cA), iv);
            iv = _mm256_add_epi32(iv, cOne);
        }
    }
    const auto twistMask = [&](const __m256i y) -> __m256i {
        const __m256i parity = _mm256_and_si256(y, cOne);
        const __m256i neg = _mm256_sub_epi32(cZero, parity);
        return _mm256_and_si256(neg, cMagic);
    };
    for (int i = 0; i < 227; ++i) {
        const __m256i y = _mm256_or_si256(_mm256_and_si256(mt[i], cHi), _mm256_and_si256(mt[i + 1], cLo));
        mt[i] = _mm256_xor_si256(_mm256_xor_si256(mt[i + 397], _mm256_srli_epi32(y, 1)), twistMask(y));
    }
    for (int i = 227; i < 623; ++i) {
        const __m256i y = _mm256_or_si256(_mm256_and_si256(mt[i], cHi), _mm256_and_si256(mt[i + 1], cLo));
        mt[i] = _mm256_xor_si256(_mm256_xor_si256(mt[i - 227], _mm256_srli_epi32(y, 1)), twistMask(y));
    }
    {
        const __m256i y = _mm256_or_si256(_mm256_and_si256(mt[623], cHi), _mm256_and_si256(mt[0], cLo));
        mt[623] = _mm256_xor_si256(_mm256_xor_si256(mt[396], _mm256_srli_epi32(y, 1)), twistMask(y));
    }
    storeDraw4(temperLanes(mt[0]), out4);
}

} // namespace

void bedrockSlimeBatch4(const int32_t* cx, const uint32_t* czu, uint8_t* outSlime4) noexcept
{
    if (useAvx2()) {
        const __m256i seeds = _mm256_xor_si256(_mm256_mullo_epi32(load4i(cx), bc(kSeedMul)), load4u(czu));
        draw4(seeds, outSlime4);
        return;
    }
    scalarFallbackN(cx, czu, outSlime4, 1);
}

void bedrockSlimeBatch8(const int32_t* cx, const uint32_t* czu, uint8_t* outSlime8) noexcept
{
    if (useAvx2()) {
        __m256i sA = _mm256_setzero_si256();
        __m256i sB = _mm256_setzero_si256();
        __m256i sC = _mm256_setzero_si256();
        __m256i sD = _mm256_setzero_si256();
        seeds16(cx, czu, sA, sB, sC, sD);
        draw8(sA, sB, outSlime8);
        return;
    }
    scalarFallbackN(cx, czu, outSlime8, 2);
}

void bedrockSlimeBatch16(const int32_t* cx, const uint32_t* czu, uint8_t* outSlime16) noexcept
{
    if (useAvx2()) {
        __m256i sA = _mm256_setzero_si256();
        __m256i sB = _mm256_setzero_si256();
        __m256i sC = _mm256_setzero_si256();
        __m256i sD = _mm256_setzero_si256();
        seeds16(cx, czu, sA, sB, sC, sD);
        draw16(sA, sB, sC, sD, outSlime16);
        return;
    }
    scalarFallbackN(cx, czu, outSlime16, 4);
}

void bedrockSlimeBatch32(const int32_t* cx, const uint32_t* czu, uint8_t* outSlime32) noexcept
{
    if (useAvx2()) {
        const __m256i k = bc(kSeedMul);
        __m256i s[8];
        for (int g = 0; g < 8; ++g) {
            s[g] = _mm256_xor_si256(_mm256_mullo_epi32(load4i(cx + 4 * g), k), load4u(czu + 4 * g));
        }
        draw32(s, outSlime32);
        return;
    }
    scalarFallbackN(cx, czu, outSlime32, 8);
}

void bedrockSlimeBatch4FullTwist(const int32_t* cx, const uint32_t* czu, uint8_t* outSlime4) noexcept
{
    if (useAvx2()) {
        const __m256i seeds = _mm256_xor_si256(_mm256_mullo_epi32(load4i(cx), bc(kSeedMul)), load4u(czu));
        draw4FullTwist(seeds, outSlime4);
        return;
    }
    scalarFallbackN(cx, czu, outSlime4, 1);
}

#else // !defined(__AVX2__) —— 未启用 AVX2 编译选项时的标量回退

void bedrockSlimeBatch4(const int32_t* cx, const uint32_t* czu, uint8_t* outSlime4) noexcept
{
    bedrockSlimeBatch4Scalar(cx, czu, outSlime4);
}

void bedrockSlimeBatch8(const int32_t* cx, const uint32_t* czu, uint8_t* outSlime8) noexcept
{
    scalarBatch4<false>(cx, czu, outSlime8);
    scalarBatch4<false>(cx + 4, czu + 4, outSlime8 + 4);
}

void bedrockSlimeBatch16(const int32_t* cx, const uint32_t* czu, uint8_t* outSlime16) noexcept
{
    for (int g = 0; g < 4; ++g) {
        scalarBatch4<false>(cx + 4 * g, czu + 4 * g, outSlime16 + 4 * g);
    }
}

void bedrockSlimeBatch4FullTwist(const int32_t* cx, const uint32_t* czu, uint8_t* outSlime4) noexcept
{
    bedrockSlimeBatch4Scalar(cx, czu, outSlime4);
}

void bedrockSlimeBatch32(const int32_t* cx, const uint32_t* czu, uint8_t* outSlime32) noexcept
{
    for (int g = 0; g < 8; ++g) {
        scalarBatch4<false>(cx + 4 * g, czu + 4 * g, outSlime32 + 4 * g);
    }
}

#endif // __AVX2__

// ============================ 扫描实现 ============================

void bedrockSlimeScanScalar(int32_t cx0, int32_t cz, int32_t count, uint8_t* out, bool startAlignedTo4) noexcept
{
    scanContiguousScalar<false>(cx0, cz, count, out, startAlignedTo4);
}

// ============================ 种子查表公开接口（P0-2.2） ============================

uint32_t bedrockChunkSeedTable(int32_t cx, uint32_t czu) noexcept
{
    return chunkSeedTable(static_cast<uint32_t>(cx), czu);
}

void setBedrockSeedTableEnabled(bool enabled) noexcept
{
    g_seedTableFlag.store(enabled ? 1 : 0, std::memory_order_relaxed);
}

bool bedrockSeedTableEnabled() noexcept
{
    return seedTableOn();
}

void bedrockSlimeScanScalarTable(int32_t cx0, int32_t cz, int32_t count, uint8_t* out, bool startAlignedTo4) noexcept
{
    if (seedTableOn()) {
        scanContiguousScalar<true>(cx0, cz, count, out, startAlignedTo4);
    } else {
        scanContiguousScalar<false>(cx0, cz, count, out, startAlignedTo4);
    }
}

// ============================ 跨步采样扫描（AVX2 + 标量） ============================

void bedrockSlimeScanStridedScalar(int32_t cx0, uint32_t czu, int32_t count, int32_t step, uint8_t* out) noexcept
{
    if (seedTableOn()) {
        scanStridedScalar<true>(cx0, czu, count, step, out);
    } else {
        scanStridedScalar<false>(cx0, czu, count, step, out);
    }
}

void bedrockSlimeScanStrided(int32_t cx0, uint32_t czu, int32_t count, int32_t step, uint8_t* out) noexcept
{
    if (count <= 0 || out == nullptr) return;
#if defined(__AVX2__)
    if (useAvx2()) {
        const uint32_t st = static_cast<uint32_t>(step < 1 ? 1 : step);
        const uint32_t stepK = st * kSeedMul; // low32(step*K)：链内步进一次的种子增量
        uint32_t cxs = static_cast<uint32_t>(cx0);
        int64_t n = count;
        uint8_t* o = out;
        while (n >= 32) {
            stridedScan32(cxs, stepK, czu, o);
            cxs += 32U * st;
            o += 32;
            n -= 32;
        }
        while (n >= 8) {
            stridedScan8(cxs, stepK, czu, o);
            cxs += 8U * st;
            o += 8;
            n -= 8;
        }
        while (n >= 4) {
            stridedDraw4(cxs, stepK, czu, o);
            cxs += 4U * st;
            o += 4;
            n -= 4;
        }
        while (n > 0) {
            *o++ = slimeFromDraw(firstDrawShortcut(chunkSeed(static_cast<int32_t>(cxs), czu)));
            cxs += st;
            --n;
        }
        return;
    }
#endif
    bedrockSlimeScanStridedScalar(cx0, czu, count, step, out);
}

void bedrockSlimeScan(int32_t cx0, int32_t cz, int32_t count, uint8_t* out, bool startAlignedTo4) noexcept
{
    if (count <= 0 || out == nullptr) return;
#if defined(__AVX2__)
    if (useAvx2()) {
        const uint32_t czu = static_cast<uint32_t>(cz);
        uint32_t cxs = static_cast<uint32_t>(cx0);
        int64_t n = count;
        uint8_t* o = out;
        // 头部：未声明 4 对齐（或实际未对齐）时用标量补齐
        if (!startAlignedTo4 || (cxs & 3U) != 0U) {
            while (n > 0 && (cxs & 3U) != 0U) {
                *o++ = slimeFromDraw(firstDrawShortcut(chunkSeed(static_cast<int32_t>(cxs), czu)));
                ++cxs;
                --n;
            }
        }
        while (n >= 32) {
            draw32x8(cxs, czu, o);   // v3.6.21：8 lane/链（逐位一致，约 2× 吞吐）
            cxs += 32;
            o += 32;
            n -= 32;
        }
        while (n >= 16) {
            scan16(cxs, czu, o);
            cxs += 16;
            o += 16;
            n -= 16;
        }
        while (n >= 4) {
            scan4(cxs, czu, o);
            cxs += 4;
            o += 4;
            n -= 4;
        }
        while (n > 0) {
            *o++ = slimeFromDraw(firstDrawShortcut(chunkSeed(static_cast<int32_t>(cxs), czu)));
            ++cxs;
            --n;
        }
        return;
    }
#endif
    bedrockSlimeScanScalar(cx0, cz, count, out, startAlignedTo4);
}

} // namespace fastmt
} // namespace slime
