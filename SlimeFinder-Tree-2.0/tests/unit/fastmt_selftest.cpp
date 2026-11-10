// fastmt_selftest.cpp - FastMt（AVX2 四通道批量 MT19937）独立自测
//
// 覆盖内容：
//   1) CPU 能力检测打印（cpuHasAvx2）
//   2) 三重独立参考实现交叉验证（本文件手写教科书 MT19937 / std::mt19937 /
//      项目自带 slime::MersenneTwister），含 C++ 标准强制值 4123659995 锚点
//   3) 一致性（bit-exact）：
//        - 200000 组随机 (cx,cz)：bedrockSlimeBatch4 / Batch4FullTwist / Batch4Scalar
//          对比独立参考实现
//        - 200000 组随机 (cx,cz)：bedrockSlimeBatch8 / Batch16 对比独立参考实现
//        - 扫描 bedrockSlimeScan / bedrockSlimeScanScalar 对比独立参考实现：
//          全组合边界坐标 × count 0..9、count ∈ {15,16,17,31,32,33,1000}、
//          300 组随机 (cx0,cz,count,aligned)
//        - 可选：与 slime_core 引擎 SlimeEngine::isSlimeChunk 交叉验证
//          （编译期定义 FASTMT_HAVE_SLIME_ENGINE 时启用）
//   4) 吞吐基准（单线程，warm-up + best of 3）：
//        标量批量 / AVX2 批量 4 / 8 / 16 / 扫描，并给出加速比
//
// 用法：fastmt_selftest.exe [每次基准的区块数，默认 20000000]
// 退出码：0 = 全部通过；1 = 存在失败项
#include "core/FastMt.h"
#include "core/RandomGenerators.h" // 项目自带 MT19937（与 FastMt 完全独立的实现）

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include <vector>

#if defined(_MSC_VER)
#include <intrin.h>
#endif

#if defined(FASTMT_HAVE_SLIME_ENGINE)
#include "core/SlimeEngine.h"
#endif

namespace {

int g_checks = 0;
int g_failures = 0;

void reportFail(const std::string& what)
{
    ++g_checks;
    ++g_failures;
    std::printf("  [FAIL] %s\n", what.c_str());
}

void expect(bool ok, const std::string& what)
{
    if (ok) {
        ++g_checks;
        std::printf("  [PASS] %s\n", what.c_str());
    } else {
        reportFail(what);
    }
}

// ======================= 独立参考实现 A：教科书式 MT19937 =======================
// 与本项目代码零共享：使用 (i+1)%624 / (i+397)%624 取模写法、64 位中间乘法。
struct RefMt19937 {
    uint32_t s[624];
    int idx = 624;

    explicit RefMt19937(uint32_t seed) { init(seed); }

    void init(uint32_t seed)
    {
        s[0] = seed;
        for (uint32_t i = 1; i < 624; ++i) {
            const uint64_t t = 1812433253ULL * static_cast<uint64_t>(s[i - 1] ^ (s[i - 1] >> 30)) + i;
            s[i] = static_cast<uint32_t>(t & 0xFFFFFFFFULL);
        }
        idx = 624;
    }

    void generate()
    {
        for (int i = 0; i < 624; ++i) {
            const uint32_t y = (s[i] & 0x80000000U) | (s[(i + 1) % 624] & 0x7FFFFFFFU);
            s[i] = s[(i + 397) % 624] ^ (y >> 1) ^ ((y & 1U) ? 0x9908B0DFU : 0U);
        }
        idx = 0;
    }

    uint32_t next()
    {
        if (idx >= 624) generate();
        uint32_t y = s[idx++];
        y ^= (y >> 11);
        y ^= (y << 7) & 0x9D2C5680U;
        y ^= (y << 15) & 0xEFC60000U;
        y ^= (y >> 18);
        return y;
    }
};

// 区块种子 + 首次输出判定（独立参考）
uint32_t refSeed(int32_t cx, uint32_t czu)
{
    return static_cast<uint32_t>(cx) * 0x1F1F1F1FU ^ czu;
}

bool refSlime(int32_t cx, uint32_t czu)
{
    RefMt19937 mt(refSeed(cx, czu));
    return (mt.next() % 10U) == 0U;
}

// ======================= 独立参考实现 B：C++ 标准库 std::mt19937 =======================
// C++ 标准规定 mt19937 的初始化/转移算法与顺序（并要求默认种子第 10000 次输出为
// 4123659995），因此这是最强的一个“外部”参照。
bool stdSlime(int32_t cx, uint32_t czu)
{
    std::mt19937 g(refSeed(cx, czu));
    return (g() % 10U) == 0U;
}

// ======================= 独立参考实现 C：项目自带 MersenneTwister =======================
bool projSlime(int32_t cx, uint32_t czu)
{
    slime::MersenneTwister mt(refSeed(cx, czu));
    return (mt.nextUInt32() % 10U) == 0U;
}

// 参考标量扫描（按 int32 回绕）
void refScan(int32_t cx0, uint32_t czu, int32_t count, uint8_t* out)
{
    uint32_t c = static_cast<uint32_t>(cx0);
    for (int32_t i = 0; i < count; ++i, ++c) {
        out[i] = refSlime(static_cast<int32_t>(c), czu) ? 1u : 0u;
    }
}

// ======================= 基准工具 =======================

using Clock = std::chrono::steady_clock;

volatile uint64_t g_sink = 0; // 防止内核调用被优化掉

struct BenchResult {
    double bestSec = 0.0;
    double chunksPerSec = 0.0;
};

// warmupRuns 次不计时 + timedRuns 次计时取最优
template <typename Kernel>
BenchResult runBench(Kernel&& k, int64_t totalChunks, int block, int warmupRuns, int timedRuns)
{
    for (int w = 0; w < warmupRuns; ++w) {
        int64_t remaining = totalChunks;
        while (remaining > 0) {
            const int n = static_cast<int>(std::min<int64_t>(block, remaining));
            k(n);
            remaining -= n;
        }
    }
    BenchResult r;
    r.bestSec = 1e300;
    for (int t = 0; t < timedRuns; ++t) {
        const auto t0 = Clock::now();
        int64_t remaining = totalChunks;
        while (remaining > 0) {
            const int n = static_cast<int>(std::min<int64_t>(block, remaining));
            k(n);
            remaining -= n;
        }
        const auto t1 = Clock::now();
        const double sec = std::chrono::duration<double>(t1 - t0).count();
        if (sec < r.bestSec) r.bestSec = sec;
    }
    r.chunksPerSec = static_cast<double>(totalChunks) / r.bestSec;
    return r;
}

void printBench(const char* name, const BenchResult& r, int64_t chunks, double baselineCps)
{
    std::printf("  %-34s %8.3f s   %14.0f chunks/s  (%.3f M/s)  加速比 %6.2fx\n",
                name, r.bestSec, r.chunksPerSec, r.chunksPerSec / 1e6,
                baselineCps > 0.0 ? r.chunksPerSec / baselineCps : 0.0);
}

std::string cpuBrand()
{
#if defined(_MSC_VER)
    int regs[4] = { 0, 0, 0, 0 };
    char buf[49] = { 0 };
    __cpuid(regs, 0x80000000);
    if (regs[0] >= 0x80000004) {
        for (int leaf = 0; leaf < 3; ++leaf) {
            __cpuid(regs, 0x80000002 + leaf);
            std::memcpy(buf + leaf * 16, regs, 16);
        }
    }
    std::string s(buf);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\0')) s.pop_back();
    return s.empty() ? std::string("(unknown)") : s;
#else
    return std::string("(unknown)");
#endif
}

} // namespace

int main(int argc, char** argv)
{
    int64_t benchChunks = 20000000; // 默认 2000 万区块
    if (argc > 1) {
        const long long v = std::atoll(argv[1]);
        if (v > 0) benchChunks = v;
    }

    std::printf("================ FastMt 自测（AVX2 四通道 MT19937 内核）================\n");
    std::printf("[环境] CPU            : %s\n", cpuBrand().c_str());
    std::printf("[环境] 编译期 __AVX2__: %s\n",
#if defined(__AVX2__)
                "defined"
#else
                "NOT defined"
#endif
    );
    const bool hasAvx2 = slime::fastmt::cpuHasAvx2();
    std::printf("[环境] cpuHasAvx2()   : %s\n", hasAvx2 ? "true" : "false");
    std::printf("[环境] 基准区块数     : %lld\n", static_cast<long long>(benchChunks));
    if (!hasAvx2) {
        std::printf("[警告] 本机不支持 AVX2：AVX2 入口将回退标量，吞吐对比仅供参考。\n");
    }

    // ---------------- 1. 参考实现自身锚点 ----------------
    std::printf("\n---- 1. 参考实现锚点（C++ 标准强制值：seed=5489 的第 10000 次输出 = 4123659995）----\n");
    {
        const uint32_t kExpected = 4123659995U;
        std::mt19937 g(5489);
        uint32_t v = 0;
        for (int i = 0; i < 10000; ++i) v = g();
        expect(v == kExpected, "std::mt19937(5489) 第 10000 次输出 == 4123659995");

        RefMt19937 r(5489);
        uint32_t rv = 0;
        for (int i = 0; i < 10000; ++i) rv = r.next();
        expect(rv == kExpected, "独立参考实现 RefMt19937 第 10000 次输出 == 4123659995");

        slime::MersenneTwister m(5489);
        uint32_t mv = 0;
        for (int i = 0; i < 10000; ++i) mv = m.nextUInt32();
        expect(mv == kExpected, "项目 MersenneTwister 第 10000 次输出 == 4123659995");

        // 首次输出三方一致（随机 2000 个 seed）
        std::mt19937_64 rng(0x5EED1234ULL);
        int mismatch = 0;
        for (int i = 0; i < 2000; ++i) {
            const uint32_t s = static_cast<uint32_t>(rng());
            RefMt19937 a(s);
            std::mt19937 b(s);
            slime::MersenneTwister c(s);
            const uint32_t va = a.next();
            const uint32_t vb = b();
            const uint32_t vc = c.nextUInt32();
            if (va != vb || vb != vc) ++mismatch;
            if (slime::fastmt::bedrockFirstDrawScalar(s) != va) ++mismatch;
        }
        expect(mismatch == 0, "2000 个随机 seed：独立参考 / std::mt19937 / 项目 MT / bedrockFirstDrawScalar 首次输出一致");
    }
    {
        // 捷径实现（只推进 mt[0]/mt[1]/mt[397]）与完整实现在随机 seed 上一致
        std::mt19937_64 rng(0xA5A5F00DULL);
        int mismatch = 0;
        for (int i = 0; i < 200000; ++i) {
            const uint32_t s = static_cast<uint32_t>(rng());
            if (slime::fastmt::bedrockFirstDrawScalar(s) != slime::fastmt::bedrockFirstDrawScalarShortcut(s)) ++mismatch;
        }
        expect(mismatch == 0, "200000 个随机 seed：标量捷径 == 标量完整实现（bit-exact）");
    }

    // ---------------- 2. 批量一致性：4 区块（200000 组随机 cx,cz） ----------------
    std::printf("\n---- 2. 批量一致性 4 区块（200000 组随机 cx/cz，全 int32 值域）----\n");
    {
        const int kPairs = 200000;
        std::mt19937_64 rng(0x1234567890ABCDEFULL);
        std::vector<int32_t> cx(4);
        std::vector<uint32_t> cz(4);
        uint8_t outAvx[4], outFull[4], outScalar[4];
        int badAvx = 0, badFull = 0, badScalar = 0, badStd = 0, badProj = 0, shown = 0;
        for (int i = 0; i < kPairs / 4; ++i) {
            for (int j = 0; j < 4; ++j) {
                cx[j] = static_cast<int32_t>(static_cast<uint32_t>(rng()));
                cz[j] = static_cast<uint32_t>(rng());
            }
            slime::fastmt::bedrockSlimeBatch4(cx.data(), cz.data(), outAvx);
            slime::fastmt::bedrockSlimeBatch4FullTwist(cx.data(), cz.data(), outFull);
            slime::fastmt::bedrockSlimeBatch4Scalar(cx.data(), cz.data(), outScalar);
            for (int j = 0; j < 4; ++j) {
                const uint8_t want = refSlime(cx[j], cz[j]) ? 1u : 0u;
                if (outAvx[j] != want) {
                    ++badAvx;
                    if (shown++ < 5) {
                        std::printf("      mismatch AVX2 batch4: cx=%d cz=%u got=%u want=%u\n", cx[j], cz[j],
                                    static_cast<unsigned>(outAvx[j]), static_cast<unsigned>(want));
                    }
                }
                if (outFull[j] != want) ++badFull;
                if (outScalar[j] != want) ++badScalar;
                if ((i % 8) == 0) {
                    if ((stdSlime(cx[j], cz[j]) ? 1u : 0u) != want) ++badStd;
                    if ((projSlime(cx[j], cz[j]) ? 1u : 0u) != want) ++badProj;
                }
            }
        }
        expect(badAvx == 0, "bedrockSlimeBatch4（AVX2）与独立参考 bit-exact（200000 组）");
        expect(badFull == 0, "bedrockSlimeBatch4FullTwist（AVX2 全长 624+twist 版）与独立参考 bit-exact");
        expect(badScalar == 0, "bedrockSlimeBatch4Scalar（标量完整参考）与独立参考 bit-exact");
        expect(badStd == 0, "独立参考与 std::mt19937 一致（25000 组抽样）");
        expect(badProj == 0, "独立参考与项目 MersenneTwister 一致（25000 组抽样）");
    }

    // ---------------- 3. 批量一致性：8 / 16 / 32 区块（200000 组随机 cx,cz） ----------------
    std::printf("\n---- 3. 批量一致性 8/16/32 区块（200000 组随机 cx/cz）----\n");
    {
        const int kPairs = 200000;
        std::mt19937_64 rng(0xFEDCBA9876543210ULL);
        std::vector<int32_t> cx(32);
        std::vector<uint32_t> cz(32);
        uint8_t out8[8], out16[16], out32[32];
        int bad8 = 0, bad16 = 0, bad32 = 0, shown = 0;
        for (int i = 0; i < kPairs / 32; ++i) {
            for (int j = 0; j < 32; ++j) {
                cx[j] = static_cast<int32_t>(static_cast<uint32_t>(rng()));
                cz[j] = static_cast<uint32_t>(rng());
            }
            slime::fastmt::bedrockSlimeBatch8(cx.data(), cz.data(), out8);
            slime::fastmt::bedrockSlimeBatch16(cx.data(), cz.data(), out16);
            slime::fastmt::bedrockSlimeBatch32(cx.data(), cz.data(), out32);
            for (int j = 0; j < 32; ++j) {
                const uint8_t want = refSlime(cx[j], cz[j]) ? 1u : 0u;
                if (out32[j] != want) {
                    ++bad32;
                    if (shown++ < 5) {
                        std::printf("      mismatch AVX2 batch32: cx=%d cz=%u got=%u want=%u\n", cx[j], cz[j],
                                    static_cast<unsigned>(out32[j]), static_cast<unsigned>(want));
                    }
                }
                if (j < 16 && out16[j] != want) ++bad16;
                if (j < 8 && out8[j] != want) ++bad8;
            }
        }
        expect(bad32 == 0, "bedrockSlimeBatch32（AVX2 八链）与独立参考 bit-exact（200000 组）");
        expect(bad16 == 0, "bedrockSlimeBatch16（AVX2 四链）与独立参考 bit-exact（200000 组）");
        expect(bad8 == 0, "bedrockSlimeBatch8（AVX2 双链）与独立参考 bit-exact（200000 组）");
    }

    // ---------------- 4. 扫描一致性：边界坐标 × count 0..9 ----------------
    std::printf("\n---- 4. 扫描一致性：边界坐标全组合 × count 0..9（对齐/非对齐）----\n");
    {
        const int32_t cxs[] = { 0, 1, -1, 2, -2, 3, -3, 4, -4, 5, -5, 7, -7, 8, -8,
                                INT32_MIN, INT32_MIN + 1, INT32_MAX, INT32_MAX - 1 };
        const int32_t czs[] = { 0, 1, -1, 2, -2, INT32_MIN, INT32_MIN + 1, INT32_MAX, INT32_MAX - 1,
                                0x1F1F1F1F, -0x1F1F1F1F, 12345678, -87654321, 0x40000000, -0x40000000,
                                999999999, -999999999, 31415926, -27182818 };
        int badScan = 0, badScanScalar = 0, cases = 0, shown = 0;
        std::vector<uint8_t> got, gotS, want;
        for (int32_t cx0 : cxs) {
            for (int32_t cz : czs) {
                for (int count = 0; count <= 9; ++count) {
                    for (int alignCase = 0; alignCase < 2; ++alignCase) {
                        const bool aligned = (alignCase == 1);
                        got.assign(static_cast<size_t>(count) + 1, 0xEE);
                        gotS.assign(static_cast<size_t>(count) + 1, 0xEE);
                        want.assign(static_cast<size_t>(count) + 1, 0xEE);
                        slime::fastmt::bedrockSlimeScan(cx0, cz, count, got.data(), aligned);
                        slime::fastmt::bedrockSlimeScanScalar(cx0, cz, count, gotS.data(), aligned);
                        refScan(cx0, static_cast<uint32_t>(cz), count, want.data());
                        for (int k = 0; k < count; ++k) {
                            if (got[k] != want[k]) {
                                ++badScan;
                                if (shown++ < 5) {
                                    std::printf("      mismatch scan: cx0=%d cz=%d count=%d aligned=%d k=%d got=%u want=%u\n",
                                                cx0, cz, count, aligned ? 1 : 0, k, static_cast<unsigned>(got[k]),
                                                static_cast<unsigned>(want[k]));
                                }
                            }
                            if (gotS[k] != want[k]) ++badScanScalar;
                        }
                        // 越界写检查（哨兵）
                        if (got[count] != 0xEE) ++badScan;
                        if (gotS[count] != 0xEE) ++badScanScalar;
                        ++cases;
                    }
                }
            }
        }
        expect(badScan == 0 && badScanScalar == 0,
               "扫描（AVX2 与纯标量）与独立参考 bit-exact，且无越界写：" + std::to_string(cases) + " 个用例");
    }

    // ---------------- 5. 扫描一致性：较大 count 与随机用例 ----------------
    std::printf("\n---- 5. 扫描一致性：count ∈ {15,16,17,31,32,33,47,48,49,63,64,65,1000} + 300 组随机用例 ----\n");
    {
        const int32_t cxs[] = { 0, -1, 1, INT32_MAX - 5, INT32_MIN + 3, 123456, -654321, 0x1F1F1F1F, -0x1F1F1F1F,
                                999999, -1000000, 7 };
        const int counts[] = { 15, 16, 17, 31, 32, 33, 47, 48, 49, 63, 64, 65, 1000 };
        const int32_t czs[] = { 0, 1, -1, INT32_MAX, INT32_MIN, 12345, -54321, 0x7FFFFF, -0x7FFFFF };
        int bad = 0, cases = 0, shown = 0;
        std::vector<uint8_t> got, want;
        for (int32_t cx0 : cxs) {
            for (int32_t cz : czs) {
                for (int count : counts) {
                    for (int alignCase = 0; alignCase < 2; ++alignCase) {
                        got.assign(static_cast<size_t>(count), 0);
                        want.assign(static_cast<size_t>(count), 0);
                        slime::fastmt::bedrockSlimeScan(cx0, cz, count, got.data(), alignCase == 1);
                        refScan(cx0, static_cast<uint32_t>(cz), count, want.data());
                        for (int k = 0; k < count; ++k) {
                            if (got[k] != want[k]) {
                                ++bad;
                                if (shown++ < 5) {
                                    std::printf("      mismatch scan: cx0=%d cz=%d count=%d k=%d\n", cx0, cz, count, k);
                                }
                            }
                        }
                        ++cases;
                    }
                }
            }
        }
        std::mt19937_64 rng(0x0BADF00DDEADBEEFULL);
        for (int t = 0; t < 300; ++t) {
            const int32_t cx0 = static_cast<int32_t>(static_cast<uint32_t>(rng()));
            const int32_t cz = static_cast<int32_t>(static_cast<uint32_t>(rng()));
            const int32_t count = static_cast<int32_t>(rng() % 1500ULL);
            const bool aligned = (rng() & 1ULL) != 0;
            got.assign(static_cast<size_t>(count), 0);
            want.assign(static_cast<size_t>(count), 0);
            slime::fastmt::bedrockSlimeScan(cx0, cz, count, got.data(), aligned);
            refScan(cx0, static_cast<uint32_t>(cz), count, want.data());
            for (int32_t k = 0; k < count; ++k) {
                if (got[k] != want[k]) {
                    ++bad;
                    if (shown++ < 5) {
                        std::printf("      mismatch scan(random): cx0=%d cz=%d count=%d k=%d\n", cx0, cz, count, k);
                    }
                }
            }
            ++cases;
        }
        expect(bad == 0, "扫描大 count / 随机用例与独立参考 bit-exact：" + std::to_string(cases) + " 个用例");
    }

    // ---------------- 6. 区块种子一致性 ----------------
    std::printf("\n---- 6. 区块种子 low32(cx*0x1F1F1F1F)^cz 一致性 ----\n");
    {
        std::mt19937_64 rng(0x1111222233334444ULL);
        int bad = 0;
        for (int i = 0; i < 100000; ++i) {
            const int32_t cx = static_cast<int32_t>(static_cast<uint32_t>(rng()));
            const uint32_t cz = static_cast<uint32_t>(rng());
            const uint64_t expect64 = (static_cast<uint64_t>(static_cast<uint32_t>(cx)) * 0x1F1F1F1FULL) & 0xFFFFFFFFULL;
            const uint32_t want = static_cast<uint32_t>(expect64) ^ cz;
            if (slime::fastmt::bedrockChunkSeedScalar(cx, cz) != want) ++bad;
        }
        expect(bad == 0, "bedrockChunkSeedScalar 与 64 位乘法取低 32 位一致（100000 组）");
    }

    // ---------------- 7. 与 slime_core 引擎交叉验证（可选） ----------------
    std::printf("\n---- 7. 与 slime_core 引擎交叉验证 ----\n");
#if defined(FASTMT_HAVE_SLIME_ENGINE)
    {
        const int32_t pts[] = { 0, 1, -1, 2, -2, 3, -3, 17, -17, 100, -100, 1000, -1000,
                                INT32_MAX, INT32_MIN, INT32_MAX - 1, INT32_MIN + 1 };
        int bad = 0, n = 0;
        std::vector<int32_t> cx(4);
        std::vector<uint32_t> cz(4);
        uint8_t out[4];
        for (int32_t a : pts) {
            for (int32_t b : pts) {
                cx[static_cast<size_t>(n % 4)] = a;
                cz[static_cast<size_t>(n % 4)] = static_cast<uint32_t>(b);
                ++n;
                if ((n % 4) != 0) continue;
                slime::fastmt::bedrockSlimeBatch4(cx.data(), cz.data(), out);
                for (int j = 0; j < 4; ++j) {
                    const bool want = slime::SlimeEngine::isSlimeChunk(slime::Version::Bedrock, 0,
                                                                       static_cast<int64_t>(cx[j]),
                                                                       static_cast<int64_t>(static_cast<int32_t>(cz[j])));
                    if ((out[j] != 0) != want) {
                        ++bad;
                        if (bad <= 5) {
                            std::printf("      mismatch engine: cx=%d cz=%d kernel=%u engine=%u\n", cx[j], cz[j],
                                        static_cast<unsigned>(out[j]), want ? 1u : 0u);
                        }
                    }
                }
            }
        }
        expect(bad == 0, "FastMt 内核与 SlimeEngine::isSlimeChunk(Bedrock) 一致（" + std::to_string(n) + " 组）");
    }
#else
    std::printf("  [SKIP] 未定义 FASTMT_HAVE_SLIME_ENGINE（未链接 slime_core 引擎）\n");
#endif

    // ---------------- 8. 吞吐基准 ----------------
    std::printf("\n---- 8. 吞吐基准（单线程，warm-up + best of 3，%lld 区块/项）----\n",
                static_cast<long long>(benchChunks));
    constexpr int kBlock = 1024; // 工作集: cx 4KB + cz 4KB + out 1KB（L1 常驻）
    std::vector<int32_t> cxBuf(kBlock);
    std::vector<uint32_t> czBuf(kBlock);
    std::vector<uint8_t> outBuf(kBlock);
    for (int i = 0; i < kBlock; ++i) {
        cxBuf[static_cast<size_t>(i)] = static_cast<int32_t>(i * 3 - 512); // 含负数
        czBuf[static_cast<size_t>(i)] = 0x9E3779B9U + static_cast<uint32_t>(i) * 2654435761U;
    }
    const uint32_t fixedCz = 0x1234ABCDU;
    const int32_t scanBase = 4 * 1000000; // 4 的倍数（对齐扫描）

    BenchResult rScalar = runBench(
        [&](int n) {
            for (int i = 0; i + 4 <= n; i += 4) {
                slime::fastmt::bedrockSlimeBatch4Scalar(cxBuf.data() + i, czBuf.data() + i, outBuf.data() + i);
            }
            g_sink += outBuf[0] + outBuf[static_cast<size_t>(n) - 1];
        },
        benchChunks, kBlock, 1, 3);

    BenchResult rAvx4 = runBench(
        [&](int n) {
            for (int i = 0; i + 4 <= n; i += 4) {
                slime::fastmt::bedrockSlimeBatch4(cxBuf.data() + i, czBuf.data() + i, outBuf.data() + i);
            }
            g_sink += outBuf[0] + outBuf[static_cast<size_t>(n) - 1];
        },
        benchChunks, kBlock, 1, 3);

    BenchResult rAvx8 = runBench(
        [&](int n) {
            for (int i = 0; i + 8 <= n; i += 8) {
                slime::fastmt::bedrockSlimeBatch8(cxBuf.data() + i, czBuf.data() + i, outBuf.data() + i);
            }
            g_sink += outBuf[0] + outBuf[static_cast<size_t>(n) - 1];
        },
        benchChunks, kBlock, 1, 3);

    BenchResult rAvx16 = runBench(
        [&](int n) {
            for (int i = 0; i + 16 <= n; i += 16) {
                slime::fastmt::bedrockSlimeBatch16(cxBuf.data() + i, czBuf.data() + i, outBuf.data() + i);
            }
            g_sink += outBuf[0] + outBuf[static_cast<size_t>(n) - 1];
        },
        benchChunks, kBlock, 1, 3);

    BenchResult rAvx32 = runBench(
        [&](int n) {
            for (int i = 0; i + 32 <= n; i += 32) {
                slime::fastmt::bedrockSlimeBatch32(cxBuf.data() + i, czBuf.data() + i, outBuf.data() + i);
            }
            g_sink += outBuf[0] + outBuf[static_cast<size_t>(n) - 1];
        },
        benchChunks, kBlock, 1, 3);

    BenchResult rScan = runBench(
        [&](int n) {
            slime::fastmt::bedrockSlimeScan(scanBase, static_cast<int32_t>(fixedCz), n, outBuf.data(), true);
            g_sink += outBuf[0] + outBuf[static_cast<size_t>(n) - 1];
        },
        benchChunks, kBlock, 1, 3);

    BenchResult rScanScalar = runBench(
        [&](int n) {
            slime::fastmt::bedrockSlimeScanScalar(scanBase, static_cast<int32_t>(fixedCz), n, outBuf.data(), true);
            g_sink += outBuf[0] + outBuf[static_cast<size_t>(n) - 1];
        },
        benchChunks, kBlock, 1, 3);

    std::printf("  基线 = 标量完整参考（624 步 init + 完整 twist，与引擎现状同构）\n");
    printBench("bedrockSlimeBatch4Scalar", rScalar, benchChunks, rScalar.chunksPerSec);
    printBench("bedrockSlimeBatch4  (AVX2 1 链)", rAvx4, benchChunks, rScalar.chunksPerSec);
    printBench("bedrockSlimeBatch8  (AVX2 2 链)", rAvx8, benchChunks, rScalar.chunksPerSec);
    printBench("bedrockSlimeBatch16 (AVX2 4 链)", rAvx16, benchChunks, rScalar.chunksPerSec);
    printBench("bedrockSlimeBatch32 (AVX2 8 链)", rAvx32, benchChunks, rScalar.chunksPerSec);
    printBench("bedrockSlimeScan         (AVX2)", rScan, benchChunks, rScalar.chunksPerSec);
    printBench("bedrockSlimeScanScalar（捷径）", rScanScalar, benchChunks, rScalar.chunksPerSec);

    const double target = 4000000.0;
    std::printf("\n  [目标] AVX2 内核 ≥ 4,000,000 区块/秒（单线程）：\n");
    std::printf("         batch4 = %.0f/s (%s)\n", rAvx4.chunksPerSec,
                rAvx4.chunksPerSec >= target ? "达标" : "未达标");
    std::printf("         batch16= %.0f/s (%s)\n", rAvx16.chunksPerSec,
                rAvx16.chunksPerSec >= target ? "达标" : "未达标");
    std::printf("         scan   = %.0f/s (%s)\n", rScan.chunksPerSec,
                rScan.chunksPerSec >= target ? "达标" : "未达标");
    std::printf("         扫描相对标量基线加速比 = %.2fx\n", rScan.chunksPerSec / rScalar.chunksPerSec);
    std::printf("         batch16 相对标量基线加速比 = %.2fx\n", rAvx16.chunksPerSec / rScalar.chunksPerSec);
    // 纯 SIMD 口径（同算法：标量捷径 vs AVX2 扫描），把算法优化与 SIMD 收益分开
    std::printf("         纯 SIMD 口径（标量捷径 → AVX2 扫描）= %.2fx\n", rScan.chunksPerSec / rScanScalar.chunksPerSec);

    // ---------------- 汇总 ----------------
    std::printf("\n================ 汇总：%d 项检查，%d 项失败 ================\n", g_checks, g_failures);
    if (g_failures == 0) {
        std::printf("RESULT: ALL PASS\n");
    } else {
        std::printf("RESULT: FAILED (%d)\n", g_failures);
    }
    std::printf("(sink=%llu)\n", static_cast<unsigned long long>(g_sink));
    return g_failures == 0 ? 0 : 1;
}
