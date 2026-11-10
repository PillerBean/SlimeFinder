// kernel_selftest.cpp - v3.3.1 SIMD 内核独立自检 + 基准（不链接引擎，不依赖第三方库）
//
// 覆盖三项交付：
//   A) 基岩版区块种子查表（任务书 P0-2.2）
//        - bedrockChunkSeedTable 与 bedrockChunkSeedScalar / 64 位乘法 低 32 位 逐位一致
//        - 表开关语义（SLIME_MT_TABLE 默认值 + setBedrockSeedTableEnabled 覆盖）
//        - bedrockSlimeScanScalarTable（查表）与 bedrockSlimeScanScalar（直接乘法）一致
//        - A/B 基准：固定 cz、2000 万区块连续扫描，各跑 3 次取最好
//   B) 跨步采样扫描（cx = cx0 + i*step，cz 固定）
//        - bedrockSlimeScanStrided / ...Scalar 与独立教科书 MT19937 参考逐位一致
//        - step ∈ {1,2,3,8,64}、负坐标、INT32 边界回绕、count 0..9 与 1000
//        - 基准：step=8、2000 万采样点，AVX2 vs 标量
//   C) 密度 g×g 窗口计数内核（任务书 P1-3.1）
//        - densityWindowCountsAvx2 / densityWindowCountsScalar 与朴素实现逐位一致
//          （朴素 A：逐窗口逐行 popcount；朴素 B：逐格取位，二者先互相校验）
//        - g ∈ {1..512} 多种取值、跨界锚点范围、窗口探出行尾、负锚点、非法参数
//        - 基准：64 行 × 512 字（32768 列）位图、全部锚点，g ∈ {5,10,32}
//   D) 构建期嵌入的 OpenCL 内核（v3.3.1 新增）
//        - kSlimeKernelSource 长度/校验和与生成期记录一致（embed_kernel.cmake 写入）
//        - 与磁盘 src/gpu/slime_kernel.cl 逐字节比对（可打开时）
//        - 两个 __kernel 入口签名完整（防止转义/换行注入破坏源码文本）
//
// 用法：kernel_selftest.exe [--correct-only]（只跑 bit-exact 校验，跳过基准）
//   SLIME_SELFTEST_QUICK=1   缩短基准规模（开发期快速回归）
//   SLIME_FORCE_SCALAR=1     内核强制标量（A/B 对照，结果必须一致）
//   SLIME_MT_TABLE=1         允许种子查表路径（见 FastMt.h）
//   SLIME_DENSITY_MODE=scalar|avx2  强制密度内核走某条路径（见 FastMatch.h）
// 退出码：0 = 全部通过；1 = 存在失败项

#include "core/FastMatch.h"
#include "core/FastMt.h"
#include "slime_kernel_embed.h" // 构建期生成的嵌入内核（长度/校验和/逐字节比对）

#include <algorithm>
#include <bit>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include <vector>

#ifdef _WIN32
#    define WIN32_LEAN_AND_MEAN
#    include <windows.h> // MultiByteToWideChar：以宽字符打开含中文的工程路径
#endif

namespace {

int g_checks = 0;
int g_failures = 0;
uint64_t g_sink = 0;
bool g_bench = true; // --correct-only 时置 false（跳过基准，只做 bit-exact 校验）

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

// ======================= 独立参考实现（与本项目代码零共享） =======================
// 教科书式 MT19937：用 (i+1)%624 / (i+397)%624 取模写法 + 64 位中间乘法，
// 与 FastMt.cpp 的“代数捷径”实现思路完全不同，用于交叉验证。
struct RefMt {
    uint32_t s[624];

    explicit RefMt(uint32_t seed) noexcept
    {
        s[0] = seed;
        for (uint32_t i = 1; i < 624; ++i) {
            const uint64_t t = 1812433253ULL * static_cast<uint64_t>(s[i - 1] ^ (s[i - 1] >> 30)) + i;
            s[i] = static_cast<uint32_t>(t & 0xFFFFFFFFULL);
        }
    }

    uint32_t next() noexcept
    {
        for (int i = 0; i < 624; ++i) {
            const uint32_t y = (s[i] & 0x80000000U) | (s[(i + 1) % 624] & 0x7FFFFFFFU);
            s[i] = s[(i + 397) % 624] ^ (y >> 1) ^ ((y & 1U) ? 0x9908B0DFU : 0U);
        }
        uint32_t y = s[0];
        y ^= (y >> 11);
        y ^= (y << 7) & 0x9D2C5680U;
        y ^= (y << 15) & 0xEFC60000U;
        y ^= (y >> 18);
        return y;
    }
};

// 参考种子：64 位乘法取低 32 位（与查表实现完全独立的算法）
uint32_t refSeed(int32_t cx, uint32_t czu) noexcept
{
    const uint64_t p = static_cast<uint64_t>(static_cast<uint32_t>(cx)) * 0x1F1F1F1FULL;
    return static_cast<uint32_t>(p & 0xFFFFFFFFULL) ^ czu;
}

bool refSlimeSeed(uint32_t seed) noexcept
{
    RefMt mt(seed);
    return (mt.next() % 10U) == 0U;
}

bool refSlime(int32_t cx, uint32_t czu) noexcept
{
    return refSlimeSeed(refSeed(cx, czu));
}

// 参考跨步扫描：逐点调用独立参考
void refStrided(int32_t cx0, uint32_t czu, int32_t count, int32_t step, uint8_t* out) noexcept
{
    uint32_t cxs = static_cast<uint32_t>(cx0);
    const uint32_t st = static_cast<uint32_t>(step < 1 ? 1 : step);
    for (int32_t i = 0; i < count; ++i) {
        out[i] = refSlime(static_cast<int32_t>(cxs), czu) ? 1 : 0;
        cxs += st;
    }
}

// ======================= 计时 =======================

template <typename F>
double bestSeconds(F&& f, int runs)
{
    double best = 1e300;
    for (int r = 0; r < runs; ++r) {
        const auto t0 = std::chrono::steady_clock::now();
        f();
        const auto t1 = std::chrono::steady_clock::now();
        const double s = std::chrono::duration<double>(t1 - t0).count();
        if (s < best) best = s;
    }
    return best;
}

// ======================= 运行规模 =======================

bool quickMode() noexcept
{
    const char* v = std::getenv("SLIME_SELFTEST_QUICK");
    return v != nullptr && v[0] != '\0' && v[0] != '0';
}

long long benchChunks() noexcept
{
    return quickMode() ? 2000000LL : 20000000LL; // 默认 2000 万（任务书规模）
}

int benchRuns() noexcept
{
    return quickMode() ? 2 : 3; // 默认 3 次取最好
}

// ======================= A. 种子查表 =======================

void sectionSeedTable()
{
    std::printf("\n================ A. 基岩版种子查表（P0-2.2） ================\n");
    using namespace slime::fastmt;

    // ---- A1. 逐位一致：20 万随机 + 边界值 ----
    {
        std::mt19937_64 rng(0xC0FFEE123456789ULL);
        const int32_t edges[] = { 0, 1, -1, 2, -2, 3, -3, 65535, 65536, 65537, -65536, -65537,
                                  0x7FFF8000, -0x7FFF8000, 0x7FFFFFFF, INT32_MIN, INT32_MAX - 1,
                                  INT32_MIN + 1, 0x00010000, 0x0000FFFF };
        int bad = 0;
        int n = 0;
        for (int32_t cx : edges) {
            for (uint32_t cz : { 0U, 1U, 0xFFFFFFFFU, 0x1234ABCDU }) {
                const uint32_t a = bedrockChunkSeedScalar(cx, cz);
                const uint32_t b = bedrockChunkSeedTable(cx, cz);
                const uint32_t c = refSeed(cx, cz);
                ++n;
                if (a != b || b != c) {
                    ++bad;
                    if (bad <= 5) {
                        std::printf("      mismatch 边界: cx=%d cz=%08X scalar=%08X table=%08X ref=%08X\n", cx, cz, a, b, c);
                    }
                }
            }
        }
        const int randomN = quickMode() ? 50000 : 200000;
        for (int i = 0; i < randomN; ++i) {
            const int32_t cx = static_cast<int32_t>(static_cast<uint32_t>(rng()));
            const uint32_t cz = static_cast<uint32_t>(rng());
            const uint32_t a = bedrockChunkSeedScalar(cx, cz);
            const uint32_t b = bedrockChunkSeedTable(cx, cz);
            const uint32_t c = refSeed(cx, cz);
            ++n;
            if (a != b || b != c) {
                ++bad;
                if (bad <= 5) {
                    std::printf("      mismatch 随机: cx=%d cz=%08X scalar=%08X table=%08X ref=%08X\n", cx, cz, a, b, c);
                }
            }
        }
        expect(bad == 0, "bedrockChunkSeedTable == bedrockChunkSeedScalar == 64 位乘法低 32 位（" +
                             std::to_string(n) + " 组：边界 " + std::to_string(sizeof(edges) / sizeof(edges[0]) * 4) +
                             " + 随机 " + std::to_string(randomN) + "）");
    }

    // ---- A2. 表项抽查：T[lo] 必须等于 low32(lo*K) ----
    {
        int bad = 0, n = 0;
        for (uint32_t lo = 0; lo < 65536U; lo += 7U) { // 抽查 9362 项（全表步进 7）
            const uint32_t got = bedrockChunkSeedTable(static_cast<int32_t>(lo), 0U);
            const uint32_t want = lo * 0x1F1F1F1FU;
            ++n;
            if (got != want) ++bad;
        }
        expect(bad == 0, "表项 T[lo] 抽查 = low32(lo*0x1F1F1F1F)（" + std::to_string(n) + " 项）");
    }

    // ---- A3. 开关语义 + 查表扫描与直接乘法扫描一致 ----
    {
        const bool envDefault = bedrockSeedTableEnabled();
        const char* envVal = std::getenv("SLIME_MT_TABLE");
        const bool envExpect = envVal != nullptr && envVal[0] != '\0' && envVal[0] != '0';
        expect(envDefault == envExpect, "查表开关默认值来自 SLIME_MT_TABLE（当前 = " +
                                            std::string(envDefault ? "开" : "关") + "）");
        setBedrockSeedTableEnabled(true);
        const bool onOk = bedrockSeedTableEnabled();
        setBedrockSeedTableEnabled(false);
        const bool offOk = !bedrockSeedTableEnabled();
        expect(onOk && offOk, "setBedrockSeedTableEnabled 运行期可覆盖默认值");

        std::mt19937_64 rng(0xABCDEF0123456789ULL);
        std::vector<uint8_t> a, b;
        int bad = 0, cases = 0;
        const int32_t counts[] = { 0, 1, 2, 3, 4, 5, 7, 8, 15, 16, 17, 31, 33, 64, 1000 };
        for (bool tableOn : { false, true }) {
            setBedrockSeedTableEnabled(tableOn);
            for (int t = 0; t < 40; ++t) {
                const int32_t cx0 = static_cast<int32_t>(static_cast<uint32_t>(rng()));
                const int32_t cz = static_cast<int32_t>(static_cast<uint32_t>(rng()));
                for (int32_t n : counts) {
                    a.assign(static_cast<size_t>(n), 0xAA);
                    b.assign(static_cast<size_t>(n), 0x55);
                    const bool aligned = (rng() & 1ULL) != 0;
                    bedrockSlimeScanScalar(cx0, cz, n, a.data(), aligned);
                    bedrockSlimeScanScalarTable(cx0, cz, n, b.data(), aligned);
                    ++cases;
                    if (n > 0 && std::memcmp(a.data(), b.data(), static_cast<size_t>(n)) != 0) {
                        ++bad;
                        if (bad <= 3) std::printf("      mismatch 扫描: cx0=%d cz=%d n=%d aligned=%d\n", cx0, cz, n, aligned ? 1 : 0);
                    }
                }
            }
        }
        setBedrockSeedTableEnabled(envDefault);
        expect(bad == 0, "bedrockSlimeScanScalarTable 与 bedrockSlimeScanScalar 逐位一致（开关两态，" +
                             std::to_string(cases) + " 个用例）");
    }

    // ---- A4. A/B 基准：2000 万区块连续扫描（各 3 次取最好）+ 纯种子微基准 ----
    if (g_bench) {
        const long long chunks = benchChunks();
        const int runs = benchRuns();
        const int32_t cz = static_cast<int32_t>(0x1234ABCDU);
        const int32_t cx0 = 4 * 1000000; // 4 的倍数
        std::vector<uint8_t> outA(static_cast<size_t>(chunks));
        std::vector<uint8_t> outB(static_cast<size_t>(chunks));
        const bool envDefault = bedrockSeedTableEnabled();

        setBedrockSeedTableEnabled(false);
        const double tMul = bestSeconds(
            [&]() {
                bedrockSlimeScanScalar(cx0, cz, static_cast<int32_t>(chunks), outA.data(), true);
                g_sink += outA[0] + outA[static_cast<size_t>(chunks) - 1];
            },
            runs);
        setBedrockSeedTableEnabled(true);
        const double tTbl = bestSeconds(
            [&]() {
                bedrockSlimeScanScalarTable(cx0, cz, static_cast<int32_t>(chunks), outB.data(), true);
                g_sink += outB[0] + outB[static_cast<size_t>(chunks) - 1];
            },
            runs);
        setBedrockSeedTableEnabled(envDefault);

        bool same = std::memcmp(outA.data(), outB.data(), static_cast<size_t>(chunks)) == 0;
        expect(same, "查表扫描与直接乘法扫描在 " + std::to_string(chunks) + " 区块上输出完全一致");

        // 纯种子微基准（把种子成本从 MT 成本中分离）：6400 万次种子计算
        const long long seedN = quickMode() ? 8000000LL : 64000000LL;
        const double tSeedMul = bestSeconds(
            [&]() {
                uint64_t acc = 0;
                for (long long i = 0; i < seedN; ++i) {
                    acc += bedrockChunkSeedScalar(static_cast<int32_t>(static_cast<uint32_t>(i) * 3U + 12345U), 0x1234ABCDU);
                }
                g_sink += acc;
            },
            runs);
        const double tSeedTbl = bestSeconds(
            [&]() {
                uint64_t acc = 0;
                for (long long i = 0; i < seedN; ++i) {
                    acc += bedrockChunkSeedTable(static_cast<int32_t>(static_cast<uint32_t>(i) * 3U + 12345U), 0x1234ABCDU);
                }
                g_sink += acc;
            },
            runs);

        const double ms = static_cast<double>(chunks) / tMul / 1e6;
        const double mt = static_cast<double>(chunks) / tTbl / 1e6;
        std::printf("  [基准] 固定 cz=%08X，连续扫描 %lld 区块，%d 次取最好\n", static_cast<uint32_t>(cz), chunks, runs);
        std::printf("         直接乘法种子标量扫描 : %.3f s → %.3f M 区块/秒\n", tMul, ms);
        std::printf("         查表种子标量扫描     : %.3f s → %.3f M 区块/秒\n", tTbl, mt);
        std::printf("         比值（查表/乘法）    : %.3f x（>1 表示查表更快）\n", tMul / tTbl);
        std::printf("  [基准] 纯种子计算 %lld 次（不含 MT）\n", seedN);
        std::printf("         直接乘法 : %.3f s → %.1f M 次/秒（%.2f ns/次）\n", tSeedMul,
                    static_cast<double>(seedN) / tSeedMul / 1e6, tSeedMul * 1e9 / static_cast<double>(seedN));
        std::printf("         查表     : %.3f s → %.1f M 次/秒（%.2f ns/次）\n", tSeedTbl,
                    static_cast<double>(seedN) / tSeedTbl / 1e6, tSeedTbl * 1e9 / static_cast<double>(seedN));
        std::printf("         比值（查表/乘法）    : %.3f x（>1 表示查表更快）\n", tSeedMul / tSeedTbl);
        std::printf("  [结论] 见交付报告：种子计算（1 次 32 位乘法 ≈ 1 周期）在 MT 近似 400 步串行\n");
        std::printf("         递推面前占比极小；256KB 表还会带来 L2 数据依赖加载，实测结论以数字为准。\n");
    }
}

// ======================= B. 跨步采样扫描 =======================

void sectionStrided(int32_t czMain)
{
    std::printf("\n================ B. 跨步采样扫描（cx = cx0 + i*step） ================\n");
    using namespace slime::fastmt;

    // ---- B1. 与独立参考逐位一致：step ∈ {1,2,3,8,64}、负坐标、边界、count 0..9 与 1000 ----
    {
        const int32_t steps[] = { 1, 2, 3, 8, 64 };
        const int32_t bases[] = { 0, 1, -1, 7, -7, 1000000, -1000000, 0x7FFFFF00, -0x7FFFFF00,
                                  INT32_MAX - 10, INT32_MIN + 10, 0x7FFF8000, -0x7FFF8000 };
        std::vector<uint8_t> got, gotScalar, want;
        int bad = 0, cases = 0, pts = 0;
        for (int32_t step : steps) {
            for (int32_t cx0 : bases) {
                // 覆盖各批量档位边界：4/8/32 以及 32 的整数倍附近
                for (int32_t count : { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 11, 12, 15, 16, 17, 31, 32, 33, 35, 36,
                                       63, 64, 65, 1000 }) {
                    got.assign(static_cast<size_t>(count), 0x11);
                    gotScalar.assign(static_cast<size_t>(count), 0x22);
                    want.assign(static_cast<size_t>(count), 0x33);
                    bedrockSlimeScanStrided(cx0, static_cast<uint32_t>(czMain), count, step, got.data());
                    bedrockSlimeScanStridedScalar(cx0, static_cast<uint32_t>(czMain), count, step, gotScalar.data());
                    refStrided(cx0, static_cast<uint32_t>(czMain), count, step, want.data());
                    ++cases;
                    pts += count;
                    if (count > 0 &&
                        (std::memcmp(got.data(), want.data(), static_cast<size_t>(count)) != 0 ||
                         std::memcmp(gotScalar.data(), want.data(), static_cast<size_t>(count)) != 0)) {
                        ++bad;
                        if (bad <= 5) {
                            std::printf("      mismatch strided: cx0=%d cz=%d step=%d count=%d\n", cx0, czMain, step, count);
                        }
                    }
                }
            }
        }
        expect(bad == 0, "step∈{1,2,3,8,64} × 13 个基址（含负/边界回绕）× count 0..9,1000 与独立参考逐位一致（" +
                             std::to_string(cases) + " 个用例 / " + std::to_string(pts) + " 个采样点）");
    }

    // ---- B2. 随机用例（含大 count / 随机 step / 随机 cz） ----
    {
        std::mt19937_64 rng(0x5EED1234ULL);
        std::vector<uint8_t> got, want;
        int bad = 0, cases = 0, pts = 0;
        const int randomCases = quickMode() ? 60 : 300;
        for (int t = 0; t < randomCases; ++t) {
            const int32_t cx0 = static_cast<int32_t>(static_cast<uint32_t>(rng()));
            const uint32_t cz = static_cast<uint32_t>(rng());
            const int32_t step = static_cast<int32_t>(1 + rng() % 64ULL);
            const int32_t count = static_cast<int32_t>(rng() % 800ULL);
            got.assign(static_cast<size_t>(count), 0);
            want.assign(static_cast<size_t>(count), 0);
            bedrockSlimeScanStrided(cx0, cz, count, step, got.data());
            refStrided(cx0, cz, count, step, want.data());
            ++cases;
            pts += count;
            if (count > 0 && std::memcmp(got.data(), want.data(), static_cast<size_t>(count)) != 0) {
                ++bad;
                if (bad <= 5) std::printf("      mismatch 随机: cx0=%d cz=%08X step=%d count=%d\n", cx0, cz, step, count);
            }
        }
        expect(bad == 0, "随机 (cx0,cz,step,count) 与独立参考逐位一致（" + std::to_string(cases) + " 个用例 / " +
                             std::to_string(pts) + " 个采样点）");
    }

    // ---- B3. step=1 退化为连续扫描：与既有 bedrockSlimeScan 一致 ----
    {
        std::vector<uint8_t> a, b;
        int bad = 0, cases = 0;
        std::mt19937_64 rng(0x1234ABCDEFULL);
        for (int t = 0; t < 30; ++t) {
            const int32_t cx0 = static_cast<int32_t>(static_cast<uint32_t>(rng() % 100000ULL));
            const int32_t cz = static_cast<int32_t>(static_cast<uint32_t>(rng()));
            const int32_t n = 1 + static_cast<int32_t>(rng() % 200ULL);
            a.assign(static_cast<size_t>(n), 0);
            b.assign(static_cast<size_t>(n), 0);
            bedrockSlimeScan(cx0, cz, n, a.data(), false);
            bedrockSlimeScanStrided(cx0, static_cast<uint32_t>(cz), n, 1, b.data());
            ++cases;
            if (std::memcmp(a.data(), b.data(), static_cast<size_t>(n)) != 0) ++bad;
        }
        expect(bad == 0, "step=1 与既有 bedrockSlimeScan 逐位一致（" + std::to_string(cases) + " 个用例）");
    }

    // ---- B4. step ≤ 0 按 1 处理；nullptr / count ≤ 0 安全 ----
    {
        std::vector<uint8_t> a(16), b(16);
        const int32_t cx0 = 12345;
        bedrockSlimeScanStrided(cx0, 0x99U, 16, 1, a.data());
        bedrockSlimeScanStrided(cx0, 0x99U, 16, 0, b.data());
        bool ok = std::memcmp(a.data(), b.data(), a.size()) == 0;
        std::fill(b.begin(), b.end(), 0xEE);
        bedrockSlimeScanStrided(cx0, 0x99U, 16, -5, b.data());
        ok = ok && std::memcmp(a.data(), b.data(), a.size()) == 0;
        bedrockSlimeScanStrided(cx0, 0x99U, 0, 8, nullptr);
        bedrockSlimeScanStrided(cx0, 0x99U, -3, 8, nullptr);
        expect(ok, "step ≤ 0 按 1 处理；count ≤ 0 / out=nullptr 安全返回");
    }

    // ---- B5. 逐点与完整参考实现（624 步 init + 完整 twist）核对 ----
    {
        int bad = 0, n = 0;
        const int32_t steps[] = { 1, 2, 3, 8, 64 };
        const int32_t bases[] = { 0, -1, 123456, INT32_MAX - 70, INT32_MIN + 70 };
        for (int32_t step : steps) {
            for (int32_t cx0 : bases) {
                const int32_t count = 24;
                std::vector<uint8_t> got(static_cast<size_t>(count), 0);
                bedrockSlimeScanStrided(cx0, static_cast<uint32_t>(czMain), count, step, got.data());
                uint32_t cxu = static_cast<uint32_t>(cx0);
                for (int32_t i = 0; i < count; ++i) {
                    const uint32_t seed = bedrockChunkSeedScalar(static_cast<int32_t>(cxu), static_cast<uint32_t>(czMain));
                    const uint8_t want = (bedrockFirstDrawScalar(seed) % 10U) == 0U ? static_cast<uint8_t>(1) : static_cast<uint8_t>(0);
                    ++n;
                    if (got[static_cast<size_t>(i)] != want) {
                        ++bad;
                        if (bad <= 5) {
                            std::printf("      mismatch 完整参考: cx0=%d step=%d i=%d got=%u want=%u\n", cx0, step, i,
                                        static_cast<unsigned>(got[static_cast<size_t>(i)]), static_cast<unsigned>(want));
                        }
                    }
                    cxu += static_cast<uint32_t>(step);
                }
            }
        }
        expect(bad == 0, "跨步扫描 vs bedrockFirstDrawScalar（完整 init+twist）逐点一致（" +
                             std::to_string(n) + " 点）");
    }

    // ---- B6. 基准：step=8、2000 万采样点（cz 固定），AVX2 vs 标量 ----
    if (g_bench) {
        const long long count = benchChunks();
        const int runs = benchRuns();
        std::vector<uint8_t> outA(static_cast<size_t>(count));
        std::vector<uint8_t> outB(static_cast<size_t>(count));
        const int32_t cx0 = 1000000;

        const double tAvx = bestSeconds(
            [&]() {
                bedrockSlimeScanStrided(cx0, static_cast<uint32_t>(czMain), static_cast<int32_t>(count), 8, outA.data());
                g_sink += outA[0] + outA[static_cast<size_t>(count) - 1];
            },
            runs);
        const double tScalar = bestSeconds(
            [&]() {
                bedrockSlimeScanStridedScalar(cx0, static_cast<uint32_t>(czMain), static_cast<int32_t>(count), 8, outB.data());
                g_sink += outB[0] + outB[static_cast<size_t>(count) - 1];
            },
            runs);
        const bool same = std::memcmp(outA.data(), outB.data(), static_cast<size_t>(count)) == 0;
        expect(same, "跨步扫描 AVX2 与标量在 " + std::to_string(count) + " 个采样点上输出完全一致");

        std::printf("  [基准] step=8、%lld 个采样点（cz=%08X 固定），%d 次取最好\n", count,
                    static_cast<uint32_t>(czMain), runs);
        std::printf("         AVX2（8 链交织）: %.3f s → %.3f M 采样/秒\n", tAvx, static_cast<double>(count) / tAvx / 1e6);
        std::printf("         标量（捷径实现）: %.3f s → %.3f M 采样/秒\n", tScalar, static_cast<double>(count) / tScalar / 1e6);
        std::printf("         加速比          : %.2f x\n", tScalar / tAvx);

        // 其它 step 的 AVX2 吞吐（标量太慢，只测 AVX2 与 step=1 参考对比）
        for (int32_t step : { 2, 64 }) {
            const double t = bestSeconds(
                [&]() {
                    bedrockSlimeScanStrided(cx0, static_cast<uint32_t>(czMain), static_cast<int32_t>(count), step, outA.data());
                    g_sink += outA[0];
                },
                runs);
            std::printf("         step=%-2d AVX2     : %.3f s → %.3f M 采样/秒\n", step, t,
                        static_cast<double>(count) / t / 1e6);
        }
    }
}

// ======================= C. 密度窗口计数 =======================

// 朴素实现甲：逐窗口、逐行、逐字 popcount（与内核“列和 + 前缀”算法完全不同）
uint32_t naiveWindowPopcount(const uint64_t* const* rows, int g, int64_t wpr, int64_t x) noexcept
{
    const int64_t totalBits = wpr * 64;
    uint32_t cnt = 0;
    int64_t c0 = x < 0 ? 0 : x;
    int64_t c1 = x + g;
    if (c1 > totalBits) c1 = totalBits;
    for (int j = 0; j < g && c0 < c1; ++j) {
        const uint64_t* row = rows[j];
        for (int64_t c = c0; c < c1;) {
            const int64_t w = c >> 6;
            const int s = static_cast<int>(c & 63);
            int64_t take = 64 - s;
            if (take > c1 - c) take = c1 - c;
            const uint64_t seg = (row[w] >> s) & (take >= 64 ? ~0ULL : ((1ULL << take) - 1ULL));
            cnt += static_cast<uint32_t>(std::popcount(seg));
            c += take;
        }
    }
    return cnt;
}

// 朴素实现乙：逐格取位（最慢但最直观，用于校验甲与内核）
uint32_t naiveWindowBitByBit(const uint64_t* const* rows, int g, int64_t wpr, int64_t x) noexcept
{
    const int64_t totalBits = wpr * 64;
    uint32_t cnt = 0;
    for (int j = 0; j < g; ++j) {
        for (int64_t d = 0; d < g; ++d) {
            const int64_t c = x + d;
            if (c < 0 || c >= totalBits) continue;
            cnt += static_cast<uint32_t>((rows[j][c >> 6] >> (c & 63)) & 1ULL);
        }
    }
    return cnt;
}

void sectionDensity()
{
    std::printf("\n================ C. 密度 g×g 窗口计数（P1-3.1） ================\n");
    using namespace slime;

    // 校验覆盖 g ≤ 512 ⇒ 位图必须备足 512 行；基准按任务书规模用 64 行 × 512 字
    //（g ∈ {5,10,32} 只读前 g 行，故仍是“64×32768 位图”的口径）
    const int kRows = 512;
    const int64_t kWords = 512;                 // 32768 列
    const int64_t totalBits = kWords * 64;
    std::vector<std::vector<uint64_t>> bits(static_cast<size_t>(kRows),
                                            std::vector<uint64_t>(static_cast<size_t>(kWords), 0));
    std::vector<const uint64_t*> rows(static_cast<size_t>(kRows), nullptr);
    for (int j = 0; j < kRows; ++j) rows[static_cast<size_t>(j)] = bits[static_cast<size_t>(j)].data();

    std::mt19937_64 rng(0x1701D00DULL);
    for (int j = 0; j < kRows; ++j) {
        for (int64_t w = 0; w < kWords; ++w) {
            bits[static_cast<size_t>(j)][static_cast<size_t>(w)] = rng();
        }
    }
    // 稀疏化到约 1/10（更接近史莱姆区块密度）：每 64 位字保留约 6~7 个随机位
    for (int j = 0; j < kRows; ++j) {
        for (int64_t w = 0; w < kWords; ++w) {
            const uint64_t r = rng();
            bits[static_cast<size_t>(j)][static_cast<size_t>(w)] = r & rng() & rng();
        }
    }

    struct Cfg {
        int g;
        int64_t a0, a1;
    };
    std::vector<Cfg> cfgs;
    const int gs[] = { 1, 2, 3, 4, 5, 7, 8, 10, 16, 31, 32, 33, 63, 64, 65, 100, 127, 128, 200, 255, 256, 257, 300, 400, 512 };
    for (int g : gs) {
        const int64_t last = totalBits - 1;
        cfgs.push_back({ g, 0, std::min<int64_t>(last, g - 1) });
        cfgs.push_back({ g, 0, std::min<int64_t>(last, 200) });
        cfgs.push_back({ g, 1, 2 });
        cfgs.push_back({ g, 63, 66 });                       // 跨 64 位字边界
        cfgs.push_back({ g, 64, 64 });
        cfgs.push_back({ g, 127, 130 });
        cfgs.push_back({ g, 512, 612 });                     // 512 位（8 字）边界
        cfgs.push_back({ g, 2040, 2200 });                   // 2048 列分块边界
        cfgs.push_back({ g, totalBits - g - 2, totalBits - 1 }); // 窗口刚好到行尾
        cfgs.push_back({ g, totalBits - 1, totalBits - 1 });     // 窗口探出行尾（按 0 位补）
        cfgs.push_back({ g, 1000, 1000 + std::min<int64_t>(300, std::max<int64_t>(1, g)) });
        if (g <= 32) cfgs.push_back({ g, 0, last });         // 全量锚点
    }
    for (int g : { 1, 2, 5, 32, 64, 300 }) {                 // 负锚点（内核自动回退标量）
        cfgs.push_back({ g, -80, 40 });
        cfgs.push_back({ g, -5, -1 });
    }

    std::vector<uint32_t> got(static_cast<size_t>(totalBits + 8), 0);
    std::vector<uint32_t> ref(static_cast<size_t>(totalBits + 8), 0);

    // ---- C1. 内核（AVX2 入口 + 标量入口）与朴素实现逐位一致 ----
    {
        int bad = 0;
        int64_t checks = 0;
        int cfgCount = 0;
        for (const Cfg& c : cfgs) {
            const int64_t n = c.a1 - c.a0 + 1;
            if (n <= 0) continue;
            ++cfgCount;
            std::fill(got.begin(), got.begin() + static_cast<size_t>(n), 0xFFFFFFFFU);
            std::fill(ref.begin(), ref.begin() + static_cast<size_t>(n), 0);
            const bool ok = slime::densityWindowCountsAvx2(rows.data(), c.g, kWords, c.a0, c.a1, got.data());
            slime::densityWindowCountsScalar(rows.data(), c.g, kWords, c.a0, c.a1, ref.data());
            if (!ok) {
                ++bad;
                std::printf("      densityWindowCountsAvx2 返回 false: g=%d [%lld,%lld]\n", c.g,
                            static_cast<long long>(c.a0), static_cast<long long>(c.a1));
                continue;
            }
            // 朴素甲：全量比对
            for (int64_t i = 0; i < n; ++i) {
                const uint32_t want = naiveWindowPopcount(rows.data(), c.g, kWords, c.a0 + i);
                ++checks;
                if (got[static_cast<size_t>(i)] != want || ref[static_cast<size_t>(i)] != want) {
                    ++bad;
                    if (bad <= 8) {
                        std::printf("      mismatch 窗口: g=%d x=%lld avx2=%u scalar=%u naive=%u\n", c.g,
                                    static_cast<long long>(c.a0 + i), got[static_cast<size_t>(i)],
                                    ref[static_cast<size_t>(i)], want);
                    }
                }
            }
            // 朴素乙（逐格）：抽样比对朴素甲
            const int64_t stride = std::max<int64_t>(1, n / 16);
            for (int64_t i = 0; i < n; i += stride) {
                const uint32_t want2 = naiveWindowBitByBit(rows.data(), c.g, kWords, c.a0 + i);
                ++checks;
                if (got[static_cast<size_t>(i)] != want2) {
                    ++bad;
                    if (bad <= 8) {
                        std::printf("      mismatch 逐格: g=%d x=%lld avx2=%u bitwise=%u\n", c.g,
                                    static_cast<long long>(c.a0 + i), got[static_cast<size_t>(i)], want2);
                    }
                }
            }
        }
        expect(bad == 0, "densityWindowCountsAvx2/Scalar 与朴素实现逐位一致（" + std::to_string(cfgCount) +
                             " 组配置 / " + std::to_string(checks) + " 个窗口）");
    }

    // ---- C2. 参数校验：非法参数返回 false 且不写 out；空范围返回 true ----
    {
        uint32_t canary = 0xDEADBEEFU;
        bool ok = true;
        ok = ok && !slime::densityWindowCountsAvx2(nullptr, 8, kWords, 0, 10, got.data());
        ok = ok && !slime::densityWindowCountsAvx2(rows.data(), 8, kWords, 0, 10, nullptr);
        ok = ok && !slime::densityWindowCountsAvx2(rows.data(), 0, kWords, 0, 10, got.data());
        ok = ok && !slime::densityWindowCountsAvx2(rows.data(), 513, kWords, 0, 10, got.data());
        ok = ok && !slime::densityWindowCountsAvx2(rows.data(), 8, -1, 0, 10, got.data());
        ok = ok && slime::densityWindowCountsAvx2(rows.data(), 8, kWords, 100, 99, &canary);
        ok = ok && (canary == 0xDEADBEEFU);
        expect(ok, "非法参数返回 false 且不写 out；空范围返回 true");
    }

    // ---- C3. 结构化位图（全 0 / 全 1 / 交替）一致性 ----
    {
        int bad = 0;
        int64_t checks = 0;
        for (int mode = 0; mode < 3; ++mode) {
            for (int j = 0; j < kRows; ++j) {
                for (int64_t w = 0; w < kWords; ++w) {
                    uint64_t v = 0;
                    if (mode == 1) v = ~0ULL;
                    else if (mode == 2) v = (w & 1) ? 0xAAAAAAAAAAAAAAAAULL : 0x5555555555555555ULL;
                    bits[static_cast<size_t>(j)][static_cast<size_t>(w)] = v;
                }
            }
            for (int g : { 1, 5, 32, 64, 65, 257, 512 }) {
                const int64_t a1 = std::min<int64_t>(totalBits - 1, 300);
                std::vector<uint32_t> a(static_cast<size_t>(a1 + 1), 0), b(static_cast<size_t>(a1 + 1), 0);
                slime::densityWindowCountsAvx2(rows.data(), g, kWords, 0, a1, a.data());
                slime::densityWindowCountsScalar(rows.data(), g, kWords, 0, a1, b.data());
                for (int64_t x = 0; x <= a1; ++x) {
                    const uint32_t want = naiveWindowPopcount(rows.data(), g, kWords, x);
                    ++checks;
                    if (a[static_cast<size_t>(x)] != want || b[static_cast<size_t>(x)] != want) {
                        ++bad;
                        if (bad <= 5) {
                            std::printf("      mismatch 结构化: mode=%d g=%d x=%lld avx2=%u scalar=%u naive=%u\n", mode, g,
                                        static_cast<long long>(x), a[static_cast<size_t>(x)], b[static_cast<size_t>(x)], want);
                        }
                    }
                }
            }
        }
        expect(bad == 0, "结构化位图（全 0 / 全 1 / 交替）× g∈{1,5,32,64,65,257,512} 逐位一致（" +
                             std::to_string(checks) + " 个窗口）");
    }

    // 恢复随机位图供基准使用
    for (int j = 0; j < kRows; ++j) {
        for (int64_t w = 0; w < kWords; ++w) {
            const uint64_t r = rng();
            bits[static_cast<size_t>(j)][static_cast<size_t>(w)] = r & rng() & rng();
        }
    }

    // ---- C4. 基准：64 行 × 512 字位图、全部锚点，AVX2 vs 朴素 ----
    if (g_bench) {
        const int runs = benchRuns();
        std::printf("  [基准] 位图 64 行 × %lld 字（%lld 列，g ≤ 32 只读前 g 行），锚点覆盖全部窗口，%d 次取最好\n",
                    static_cast<long long>(kWords), static_cast<long long>(totalBits), runs);
        std::vector<uint32_t> out(static_cast<size_t>(totalBits + 8), 0);
        for (int g : { 5, 10, 32 }) {
            const int64_t a1 = totalBits - g;
            const int64_t n = a1 + 1;
            const double tNaiveLocal = bestSeconds(
                [&]() {
                    uint64_t acc = 0;
                    for (int64_t x = 0; x <= a1; ++x) acc += naiveWindowPopcount(rows.data(), g, kWords, x);
                    g_sink += acc;
                },
                runs);
            const double tScalar = bestSeconds(
                [&]() {
                    slime::densityWindowCountsScalar(rows.data(), g, kWords, 0, a1, out.data());
                    g_sink += out[0] + out[static_cast<size_t>(a1)];
                },
                runs);
            const double tAvx = bestSeconds(
                [&]() {
                    slime::densityWindowCountsAvx2(rows.data(), g, kWords, 0, a1, out.data());
                    g_sink += out[0] + out[static_cast<size_t>(a1)];
                },
                runs);
            std::printf("         g=%-3d 朴素(本地逐窗口) %8.3f ms (%6.1f ns/窗口) | 交付标量 %8.3f ms (%6.1f ns/窗口) | "
                        "AVX2 %7.3f ms (%5.1f ns/窗口) | 加速比 %.1fx / %.1fx\n",
                        g, tNaiveLocal * 1e3, tNaiveLocal * 1e9 / static_cast<double>(n), tScalar * 1e3,
                        tScalar * 1e9 / static_cast<double>(n), tAvx * 1e3, tAvx * 1e9 / static_cast<double>(n),
                        tNaiveLocal / tAvx, tScalar / tAvx);
        }
    }
}

// ======================= D) 构建期嵌入的 OpenCL 内核自检 =======================
// 背景（v3.3.1 实测教训）：embed_kernel.cmake 曾把「换行」写成 C 转义 \n 注入到
// kSlimeKernelSource 中（每 24 字节一个），导致 OpenCL 编译器在运行期才报错；
// 而当时的依赖跟踪失效又让旧对象被静默复用。这里在运行期把嵌入字符串与磁盘上的
// .cl 逐字节比对，任何编码/转义/换行差错都会在自检阶段立刻暴露。
bool readFileUtf8(const char* utf8Path, std::string& out)
{
    out.clear();
#ifdef _WIN32
    wchar_t wide[4096];
    const int wn = ::MultiByteToWideChar(CP_UTF8, 0, utf8Path, -1, wide,
                                         static_cast<int>(sizeof(wide) / sizeof(wide[0])));
    if (wn <= 0) return false;
    std::FILE* f = ::_wfopen(wide, L"rb");
#else
    std::FILE* f = std::fopen(utf8Path, "rb");
#endif
    if (!f) return false;
    char buf[65536];
    size_t got = 0;
    while ((got = std::fread(buf, 1, sizeof(buf), f)) > 0) out.append(buf, got);
    std::fclose(f);
    return !out.empty();
}

void sectionEmbeddedKernel()
{
    std::printf("\n[D] 构建期嵌入的 OpenCL 内核\n");
    const char* src = slimegpu::kSlimeKernelSource;
    const size_t n = std::strlen(src);
    expect(n == static_cast<size_t>(slimegpu::kSlimeKernelBytes),
           "嵌入长度与生成期一致（实测 " + std::to_string(n) + " 字节 / 记录 " +
               std::to_string(slimegpu::kSlimeKernelBytes) + " 字节）");

    uint32_t sum = 0;
    for (size_t i = 0; i < n; ++i) {
        sum = static_cast<uint32_t>(static_cast<uint64_t>(sum) * 31 + static_cast<unsigned char>(src[i]));
    }
    expect(sum == static_cast<uint32_t>(slimegpu::kSlimeKernelChecksum),
           "嵌入内容校验和与生成期一致（实测 " + std::to_string(sum) + " / 记录 " +
               std::to_string(slimegpu::kSlimeKernelChecksum) + "）");

    expect(std::strstr(src, "__kernel void scan_java_slime(") != nullptr &&
               std::strstr(src, "__kernel void scan_bedrock_slime(") != nullptr,
           "两个 __kernel 入口签名完整（未被转义/换行注入破坏）");

    const size_t crlf = 0; // 统计用（仅打印，不判定）
    (void)crlf;
    int lines = 1;
    for (size_t i = 0; i < n; ++i) {
        if (src[i] == '\n') ++lines;
    }
    std::printf("  嵌入源码：%d 行，首行 \"", lines);
    // 按字节截断时必须退回到 UTF-8 字符边界：在多字节字符中间切断会产生
    // 非法字节序列，证据日志里显示成 U+FFFD（污染审计发现的证据编码缺陷）
    size_t cut = 0;
    while (cut < n && src[cut] != '\n' && src[cut] != '\r' && cut < 70) ++cut;
    while (cut > 0 && (static_cast<unsigned char>(src[cut]) & 0xC0) == 0x80) --cut;
    for (size_t i = 0; i < cut; ++i) std::putchar(src[i]);
    std::printf("\"\n");

    std::string disk;
    if (readFileUtf8(SLIME_KERNEL_CL_PATH, disk)) {
        expect(disk.size() == n && std::memcmp(disk.data(), src, n) == 0,
               std::string("与磁盘 ") + SLIME_KERNEL_CL_PATH + " 逐字节一致（" +
                   std::to_string(disk.size()) + " 字节）");
    } else {
        std::printf("  [SKIP] 无法打开 %s（长度/校验和检查仍然有效）\n", SLIME_KERNEL_CL_PATH);
    }
}

} // namespace

int main(int argc, char** argv)
{
    std::setvbuf(stdout, nullptr, _IONBF, 0); // 崩溃时也能看到已打印的进度
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--correct-only") == 0) g_bench = false;
    }

    const char* forceScalar = std::getenv("SLIME_FORCE_SCALAR");
    const char* mtTable = std::getenv("SLIME_MT_TABLE");
    const char* densityMode = std::getenv("SLIME_DENSITY_MODE");

    std::printf("================ SlimeFinder v3.3.1 SIMD 内核自检/基准 ================\n");
    std::printf("FastMatch::cpuHasAvx2          = %s\n", slime::cpuHasAvx2() ? "是" : "否");
    std::printf("FastMt::cpuHasAvx2             = %s\n", slime::fastmt::cpuHasAvx2() ? "是" : "否");
    std::printf("SLIME_FORCE_SCALAR             = %s\n", forceScalar ? forceScalar : "(未设置)");
    std::printf("SLIME_MT_TABLE                 = %s（当前查表开关 = %s）\n", mtTable ? mtTable : "(未设置)",
                slime::fastmt::bedrockSeedTableEnabled() ? "开" : "关");
    std::printf("SLIME_DENSITY_MODE             = %s\n", densityMode ? densityMode : "(未设置/自动)");
    std::printf("基准规模                       = %s\n", quickMode() ? "QUICK" : "完整（2000 万）");

    const int32_t czMain = static_cast<int32_t>(0x51E9C0DEU); // 固定 cz（三项基准共用）

    sectionSeedTable();
    sectionStrided(czMain);
    sectionDensity();
    sectionEmbeddedKernel();

    if (!g_bench) {
        std::printf("\n（--correct-only：仅做 bit-exact 校验，未跑基准）\n");
    }

    std::printf("\n================ 汇总：%d 项检查，%d 项失败 ================\n", g_checks, g_failures);
    if (g_failures == 0) {
        std::printf("RESULT: ALL PASS\n");
    } else {
        std::printf("RESULT: FAILED (%d)\n", g_failures);
    }
    std::printf("(sink=%llu)\n", static_cast<unsigned long long>(g_sink));
    return g_failures == 0 ? 0 : 1;
}
