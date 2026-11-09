// opt_probe9.cpp - v3.6.20 探针：逐条检验「另一个 AI」的提速想法（不整合，只测量）
//
//   ① nextInt(10) 是否真的没有拒绝采样？
//   ② 17 位分块查表的 48 位 LCG（q = (A31*hi + T[lo]) & 0x7FFFFFFF）是否正确/更快？
//   ③ 模 10 判定去除法（16≡1 mod 5 → 半字节求和）是否正确/更快？
//   ④ 「只找一个匹配：期望检查 10^m 个锚点」是否成立？
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {

constexpr uint64_t kMask = (1ULL << 48) - 1;
constexpr uint64_t kMult = 0x5DEECE66DULL;
constexpr uint64_t kAdd = 0xBULL;
constexpr uint32_t kA31 = static_cast<uint32_t>(kMult & 0x7FFFFFFFULL);

template <typename Fn>
double timeMs(Fn&& fn, int reps = 3)
{
    double best = 1e30;
    for (int r = 0; r < reps; ++r) {
        const auto t0 = std::chrono::steady_clock::now();
        fn();
        const double ms =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        if (ms < best) best = ms;
    }
    return best;
}

// Java 真实语义：bits = s>>>17（31 位）；当 bits - bits%10 + 9 < 0 时重抽
inline bool javaNextInt10(uint64_t s, int* draws) noexcept
{
    int n = 0;
    for (;;) {
        s = (s * kMult + kAdd) & kMask;
        const int32_t bits = static_cast<int32_t>(s >> 17);
        const int32_t val = bits % 10;
        ++n;
        if (bits - val + 9 >= 0) {
            if (draws) *draws = n;
            return val == 0;
        }
    }
}

// ① 拒绝采样：bits ≥ 2^31-9 时 bits - bits%10 + 9 < 0
void testReject()
{
    std::printf("== ① nextInt(10) 的拒绝采样 ==\n");
    // 直接在「bits 空间」找会被拒绝的取值：bits ∈ [2147483640, 2147483647]
    int32_t bad[8] = { 0 };
    for (int i = 0; i < 8; ++i) bad[i] = 2147483640 + i;
    std::printf("  被拒绝的 bits 区间：[2147483640, 2147483647]，共 8 个（占 2^31 的 %.2e）\n", 8.0 / 2147483648.0);
    for (int i = 0; i < 8; ++i) {
        const int32_t val = bad[i] % 10;
        std::printf("   bits=%d → val=%d → bits-val+9=%d %s\n", bad[i], val, bad[i] - val + 9,
                    (bad[i] - val + 9 < 0) ? "会重抽" : "不重抽");
    }
    // 实际抽 2^28 个随机 48 位状态，统计触发次数与「naive %10」的差异
    uint64_t s = 0x123456789ABCULL & kMask;
    int64_t n = 0;
    int64_t rejects = 0;
    int64_t naiveWrong = 0;
    const int64_t N = 1LL << 28;
    for (int64_t i = 0; i < N; ++i) {
        const uint64_t s1 = (s * kMult + kAdd) & kMask;
        const int32_t bits = static_cast<int32_t>(s1 >> 17);
        const int32_t val = bits % 10;
        if (bits - val + 9 < 0) {
            ++rejects;
            // naive 认 val==0；真实值要再抽一次
            const bool naive = (val == 0);
            const bool real = javaNextInt10(s, nullptr);
            if (naive != real) ++naiveWrong;
        }
        ++n;
        s = s1;
    }
    std::printf("  抽 %lld 个后缀状态：触发拒绝 %lld 次（实测频率 %.2e，理论 %.2e）\n",
                static_cast<long long>(n), static_cast<long long>(rejects),
                static_cast<double>(rejects) / static_cast<double>(n), 8.0 / 2147483648.0);
    std::printf("  其中「直接用 %%10」会判错 %lld 次\n", static_cast<long long>(naiveWrong));
    std::printf("  ⇒ 结论：AI 的「bound=10 永远不拒绝」不成立；1 亿区块约有 %.2f 个区块会踩到\n\n",
                1e8 * 8.0 / 2147483648.0);
}

// ② 17 位分块查表
void testTableLcg()
{
    std::printf("== ② 17 位分块查表（512KB T[lo]）==\n");
    const int kBits = 17;
    const uint32_t kLo = 1u << kBits;
    std::vector<uint32_t> T(kLo);
    for (uint32_t lo = 0; lo < kLo; ++lo) {
        const uint64_t r = kMult * static_cast<uint64_t>(lo) + kAdd;
        T[lo] = static_cast<uint32_t>((r >> kBits) & 0xFFFFFFFFULL);
    }
    // 正确性：与直接乘法比对 1000 万个状态
    uint64_t s = 0x9876543210ULL & kMask;
    int mismatch = 0;
    for (int i = 0; i < 10000000; ++i) {
        const uint64_t s1 = (s * kMult + kAdd) & kMask;
        const uint32_t qDirect = static_cast<uint32_t>(s1 >> 17);
        const uint32_t hi = static_cast<uint32_t>(s >> kBits);
        const uint32_t lo = static_cast<uint32_t>(s & (kLo - 1));
        const uint32_t qTable = (kA31 * hi + T[lo]) & 0x7FFFFFFFu;
        if (qDirect != qTable) ++mismatch;
        s = s1;
    }
    std::printf("  正确性：1000 万状态中不一致 %d 个\n", mismatch);

    // 速度：连续 2^26 个状态，分别走 直接乘法 / 查表 / 现有 AVX2 位置
    const int64_t N = 1 << 26;
    uint64_t accA = 0, accB = 0;
    const double msDirect = timeMs([&] {
        uint64_t st = 0x111111111111ULL & kMask, a = 0;
        for (int64_t i = 0; i < N; ++i) {
            st = (st * kMult + kAdd) & kMask;
            a += (static_cast<uint32_t>(st >> 17) % 10u) == 0u ? 1 : 0;
        }
        accA = a;
    });
    const double msTable = timeMs([&] {
        uint64_t st = 0x111111111111ULL & kMask, a = 0;
        for (int64_t i = 0; i < N; ++i) {
            const uint32_t hi = static_cast<uint32_t>(st >> kBits);
            const uint32_t lo = static_cast<uint32_t>(st & (kLo - 1));
            const uint32_t q = (kA31 * hi + T[lo]) & 0x7FFFFFFFu;
            a += (q % 10u) == 0u ? 1 : 0;
            st = (kMult * st + kAdd) & kMask;
        }
        accB = a;
    });
    std::printf("  直接乘法 %.2f ms | 查表 %.2f ms（含状态推进）| 结果一致=%s\n", msDirect, msTable,
                accA == accB ? "是" : "否");
    std::printf("  每区块：直接 %.3f ns | 查表 %.3f ns（参考：现有 AVX2 约 0.86 ns）\n\n",
                msDirect * 1e6 / N, msTable * 1e6 / N);
}

// ③ 半字节求和 mod 5
void testNibbleMod5()
{
    std::printf("== ③ 模 10 去除法（16≡1 mod 5 → 半字节和）==\n");
    uint64_t s = 0xABCDEF123456ULL & kMask;
    int mismatch = 0;
    for (int i = 0; i < 10000000; ++i) {
        const uint64_t s1 = (s * kMult + kAdd) & kMask;
        const uint32_t q = static_cast<uint32_t>(s1 >> 17);
        const bool direct = (q % 10u) == 0u;
        // 半字节求和
        uint32_t x = q;
        uint32_t sum = 0;
        for (int k = 0; k < 8; ++k) {
            sum += x & 0xFu;
            x >>= 4;
        }
        const bool nibble = ((q & 1u) == 0u) && (sum % 5u == 0u);
        if (direct != nibble) ++mismatch;
        s = s1;
    }
    std::printf("  正确性：1000 万状态中不一致 %d 个\n", mismatch);

    const int64_t N = 1 << 26;
    uint64_t accA = 0, accB = 0;
    const double msMagic = timeMs([&] {
        uint64_t st = 0x111111111111ULL & kMask, a = 0;
        for (int64_t i = 0; i < N; ++i) {
            st = (st * kMult + kAdd) & kMask;
            a += (static_cast<uint32_t>(st >> 17) % 10u) == 0u ? 1 : 0;
        }
        accA = a;
    });
    const double msNibble = timeMs([&] {
        uint64_t st = 0x111111111111ULL & kMask, a = 0;
        for (int64_t i = 0; i < N; ++i) {
            st = (st * kMult + kAdd) & kMask;
            uint32_t q = static_cast<uint32_t>(st >> 17);
            uint32_t sum = 0;
            uint32_t x = q;
            for (int k = 0; k < 8; ++k) {
                sum += x & 0xFu;
                x >>= 4;
            }
            a += (((q & 1u) == 0u) && (sum % 5u == 0u)) ? 1 : 0;
        }
        accB = a;
    });
    std::printf("  魔术乘 %%10 %.2f ms | 半字节和 %.2f ms | 结果一致=%s\n", msMagic, msNibble,
                accA == accB ? "是" : "否");
    std::printf("  每区块：%%10 %.3f ns | 半字节 %.3f ns\n\n", msMagic * 1e6 / N, msNibble * 1e6 / N);
}

// ④ 「只找一个匹配：期望 10^m 个锚点」
void testExpectedAnchors()
{
    std::printf("== ④ 只找一个匹配的期望锚点数 ==\n");
    // 用真公式生成位图，再统计「行优先顺序扫描到第一个命中」平均要检查多少锚点
    const int W = 1024, H = 1024;
    std::vector<uint8_t> b(static_cast<size_t>(W) * H);
    for (int z = 0; z < H; ++z) {
        for (int x = 0; x < W; ++x) {
            // 简化：直接按 10% 密度生成（与真公式的密度一致，用于验证 10^m 阶）
            const uint64_t h = (static_cast<uint64_t>(x) * 0x9E3779B97F4A7C15ULL) ^
                               (static_cast<uint64_t>(z) * 0xC2B2AE3D27D4EB4FULL);
            const uint64_t v = h * 6364136223846793005ULL + 1442695040888963407ULL;
            b[static_cast<size_t>(z) * W + x] = ((v >> 33) % 10u) == 0u ? 1 : 0;
        }
    }
    const int shapes[3][2] = { { 3, 1 }, { 2, 2 }, { 3, 3 } };
    for (auto& sh : shapes) {
        const int pw = sh[0], ph = sh[1];
        const double p = 1.0;
        double expected = 1.0;
        for (int i = 0; i < pw * ph; ++i) expected *= 10.0;
        int64_t checked = 0, found = 0;
        for (int z = 0; z + ph <= H && found < 200; ++z) {
            for (int x = 0; x + pw <= W && found < 200; ++x) {
                bool ok = true;
                for (int dz = 0; dz < ph && ok; ++dz)
                    for (int dx = 0; dx < pw; ++dx)
                        if (!b[static_cast<size_t>(z + dz) * W + x + dx]) { ok = false; break; }
                ++checked;
                if (ok) { ++found; break; }
            }
        }
        std::printf("  图案 %dx%d（m=%d）：理论 10^m ≈ %.1e，实测平均检查 %.0f 个锚点（%lld 次采样）\n",
                    pw, ph, pw * ph, expected, found > 0 ? static_cast<double>(checked) / found : -1.0,
                    static_cast<long long>(found));
        (void)p;
    }
    std::printf("  ⇒ 5x5 全绿（m=25）期望 10^25 个锚点，正向扫描不可能；只能靠逆解/格约化\n\n");
}

} // namespace

int main()
{
    testReject();
    testTableLcg();
    testNibbleMod5();
    testExpectedAnchors();
    return 0;
}
