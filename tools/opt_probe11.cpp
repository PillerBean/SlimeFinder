// opt_probe11.cpp - 检验「可分离位图 AND」「哈希表双点」「q 奇偶预筛」「q 位敏感性」
//   （对应外部 AI 清单的第 1/2/4/6 条；第 3 条差分生成另测，第 5/7 条已在此前轮次否证）
#include <bit>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {

constexpr uint64_t kMask = (1ULL << 48) - 1;
constexpr uint64_t kMult = 0x5DEECE66DULL;
constexpr uint64_t kAdd = 0xBULL;
constexpr uint64_t kXor = 987234911ULL;

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

void makeBits(std::vector<uint64_t>& bits, int W, int H, int& wpr)
{
    wpr = (W + 63) / 64;
    bits.assign(static_cast<size_t>(wpr) * H, 0);
    for (int z = 0; z < H; ++z) {
        for (int x = 0; x < W; ++x) {
            const uint64_t h = (static_cast<uint64_t>(x) * 0x9E3779B97F4A7C15ULL) ^
                               (static_cast<uint64_t>(z) * 0xC2B2AE3D27D4EB4FULL);
            const uint64_t v = h * 6364136223846793005ULL + 1442695040888963407ULL;
            if (((v >> 33) % 10u) == 0u) bits[static_cast<size_t>(z) * wpr + (x >> 6)] |= 1ULL << (x & 63);
        }
    }
}

// 真实公式位图（Java 版判定，密度 ~10%）
void makeRealBits(std::vector<uint64_t>& bits, int W, int H, int& wpr, int64_t seed)
{
    wpr = (W + 63) / 64;
    bits.assign(static_cast<size_t>(wpr) * H, 0);
    auto imul = [](int32_t a, int32_t b) {
        return static_cast<int32_t>(static_cast<uint32_t>(a) * static_cast<uint32_t>(b));
    };
    for (int z = 0; z < H; ++z) {
        const int32_t cz = z - H / 2;
        const int32_t t3 = imul(cz, cz);
        const int32_t t4 = imul(cz, 389711);
        const uint64_t rowBase = static_cast<uint64_t>(seed) +
                                 static_cast<uint64_t>(static_cast<int64_t>(t3) * 4392871LL) +
                                 static_cast<uint64_t>(static_cast<int64_t>(t4));
        for (int x = 0; x < W; ++x) {
            const int32_t cx = x - W / 2;
            const int32_t t1 = imul(imul(cx, cx), 4987142);
            const int32_t t2 = imul(cx, 5947611);
            uint64_t u = rowBase + static_cast<uint64_t>(static_cast<int64_t>(t1)) +
                         static_cast<uint64_t>(static_cast<int64_t>(t2));
            u ^= kXor;
            u = (u ^ kMult) & kMask;
            const uint64_t s1 = (u * kMult + kAdd) & kMask;
            if ((static_cast<uint32_t>(s1 >> 17) % 10u) == 0u) {
                bits[static_cast<size_t>(z) * wpr + (x >> 6)] |= 1ULL << (x & 63);
            }
        }
    }
}

// 行内 AND：R[z] 的 bit x = 「row z 上从 x 开始连续 w 个格全为 1」
void rowPass(const std::vector<uint64_t>& bits, int wpr, int W, int H, int gx, std::vector<uint64_t>& R)
{
    R.assign(bits.size(), 0);
    for (int z = 0; z < H; ++z) {
        const uint64_t* row = &bits[static_cast<size_t>(z) * wpr];
        uint64_t* dst = &R[static_cast<size_t>(z) * wpr];
        for (int w = 0; w + 1 < wpr; ++w) {
            uint64_t v = row[w];
            for (int dx = 1; dx < gx; ++dx) v &= (row[w] >> dx) | (row[w + 1] << (64 - dx));
            dst[w] = v;
        }
    }
}

void runSeparable(int gx, int gz, int W, int H, int wpr, const std::vector<uint64_t>& bits,
                  const char* tag = "");

void runSeparableReal(int gx, int gz, int W, int H, int wpr, const std::vector<uint64_t>& bits)
{
    runSeparable(gx, gz, W, H, wpr, bits, "real");
}

void runSeparable(int gx, int gz, int W, int H, int wpr, const std::vector<uint64_t>& bits,
                  const char* tag)
{
    // 全偏移 AND（现有做法）
    uint64_t cntA = 0;
    const double msA = timeMs([&] {
        int64_t total = 0;
        for (int z = 0; z + gz <= H; ++z) {
            for (int w = 0; w + 1 < wpr; ++w) {
                uint64_t res = ~0ULL;
                for (int dz = 0; dz < gz; ++dz) {
                    const uint64_t* row = &bits[static_cast<size_t>(z + dz) * wpr];
                    uint64_t v = row[w];
                    for (int dx = 1; dx < gx; ++dx) v &= (row[w] >> dx) | (row[w + 1] << (64 - dx));
                    res &= v;
                }
                total += std::popcount(res);
            }
        }
        cntA = static_cast<uint64_t>(total);
    });

    // 可分离 AND：行内一遍 + 行间一遍
    std::vector<uint64_t> R;
    uint64_t cntB = 0;
    const double msB = timeMs([&] {
        rowPass(bits, wpr, W, H, gx, R);
        int64_t total = 0;
        for (int z = 0; z + gz <= H; ++z) {
            const uint64_t* r0 = &R[static_cast<size_t>(z) * wpr];
            for (int w = 0; w < wpr; ++w) {
                uint64_t res = ~0ULL;
                for (int dz = 0; dz < gz; ++dz) res &= r0[static_cast<size_t>(dz) * wpr + w];
                total += std::popcount(res);
            }
        }
        cntB = static_cast<uint64_t>(total);
    });

    const double anchors = static_cast<double>(W) * H;
    const int m = gx * gz;
    std::printf("  %s%2dx%-2d m=%-4d 全偏移 AND %7.2f ms (%6.3f ns/锚点) | 可分离 %7.2f ms (%6.3f ns/锚点) | 倍率 %5.2fx | 一致=%s\n",
                tag, gx, gz, m, msA, msA * 1e6 / anchors, msB, msB * 1e6 / anchors,
                msA / (msB > 1e-9 ? msB : 1e-9), (cntA == cntB) ? "是" : "否");
}

// ---------- 双点：位测试 vs 开放寻址哈希表 ----------
volatile uint64_t sink = 0;

void runHashJoin(int W, int H, int wpr, const std::vector<uint64_t>& bits)
{
    // 位测试版（上一轮实测 1.39 ns/锚点）
    uint64_t cntA = 0;
    const double msA = timeMs([&] {
        int64_t total = 0;
        for (int z = 0; z < H; ++z) {
            const uint64_t* row = &bits[static_cast<size_t>(z) * wpr];
            for (int x = 0; x < W; ++x) {
                const uint64_t w = row[x >> 6];
                if (!((w >> (x & 63)) & 1ULL)) continue;
                ++total;
            }
        }
        cntA = static_cast<uint64_t>(total);
        sink = cntA;
    });

    // 开放寻址哈希表版：把 (0,0) 命中点插入表，再逐锚点探测
    std::vector<uint64_t> table;
    uint64_t cntB = 0;
    const double msB = timeMs([&] {
        size_t cap = 1;
        while (cap < static_cast<size_t>(W) * H / 4) cap <<= 1;   // 负载因子 ≈ 0.25
        table.assign(cap, ~0ULL);
        const size_t mask = cap - 1;
        for (int z = 0; z < H; ++z) {
            const uint64_t* row = &bits[static_cast<size_t>(z) * wpr];
            for (int x = 0; x < W; ++x) {
                if (!((row[x >> 6] >> (x & 63)) & 1ULL)) continue;
                const uint64_t key = (static_cast<uint64_t>(static_cast<uint32_t>(x)) << 32) |
                                     static_cast<uint32_t>(z);
                size_t i = (key * 0x9E3779B97F4A7C15ULL) & mask;
                while (table[i] != ~0ULL) i = (i + 1) & mask;
                table[i] = key;
            }
        }
        int64_t total = 0;
        for (int z = 0; z < H; ++z) {
            for (int x = 0; x < W; ++x) {
                const uint64_t key = (static_cast<uint64_t>(static_cast<uint32_t>(x)) << 32) |
                                     static_cast<uint32_t>(z) + 1;
                size_t i = (key * 0x9E3779B97F4A7C15ULL) & mask;
                while (table[i] != ~0ULL && table[i] != key) i = (i + 1) & mask;
                total += (table[i] == key) ? 1 : 0;
            }
        }
        cntB = static_cast<uint64_t>(total);
    });
    const double anchors = static_cast<double>(W) * H;
    std::printf("  双点筛选：位测试 %6.2f ms (%5.3f ns/锚点) | 哈希表 %6.2f ms (%5.3f ns/锚点) | 倍率 %5.2fx\n",
                msA, msA * 1e6 / anchors, msB, msB * 1e6 / anchors, msB / (msA > 1e-9 ? msA : 1e-9));
    (void)cntA;
    (void)cntB;
}

// ---------- q 奇偶预筛 ----------
void runParity()
{
    uint64_t acc[3] = { 0, 0, 0 };
    const double a = timeMs([&] {
        uint64_t s = 0x123456789ABCULL & kMask, n = 0;
        for (int i = 0; i < (1 << 22); ++i) {
            s = (s * kMult + kAdd) & kMask;
            n += (static_cast<uint32_t>(s >> 17) % 10u) == 0u ? 1 : 0;
        }
        acc[0] = n;
    });
    const double b = timeMs([&] {
        uint64_t s = 0x123456789ABCULL & kMask, n = 0;
        for (int i = 0; i < (1 << 22); ++i) {
            s = (s * kMult + kAdd) & kMask;
            const uint32_t q = static_cast<uint32_t>(s >> 17);
            n += (((q & 1u) == 0u) && (q % 5u == 0u)) ? 1 : 0;
        }
        acc[1] = n;
    });
    const double c = timeMs([&] {
        uint64_t s = 0x123456789ABCULL & kMask, n = 0;
        for (int i = 0; i < (1 << 22); ++i) {
            s = (s * kMult + kAdd) & kMask;
            const uint32_t q = static_cast<uint32_t>(s >> 17);
            if (q & 1u) continue;
            n += (q % 5u) == 0u ? 1 : 0;
        }
        acc[2] = n;
    });
    const double N = 1 << 22;
    std::printf("  q 判定：%%10 直接 %6.3f ns/区块 | 奇偶+%%5 %6.3f | 奇偶先行 continue %6.3f | 一致=%s\n",
                a * 1e6 / N, b * 1e6 / N, c * 1e6 / N,
                (acc[0] == acc[1] && acc[1] == acc[2]) ? "是" : "否");
}

// ---------- 3) 差分生成 u vs 预计算表 ----------
void runDifferencing()
{
    const int N = 1 << 22;
    const uint64_t rowBase = 12345;   // 只比较 x 项（z 项固定）
    auto imul = [](int32_t a, int32_t b) {
        return static_cast<int32_t>(static_cast<uint32_t>(a) * static_cast<uint32_t>(b));
    };
    auto tailOf = [&](int32_t x) {
        const int32_t t1 = imul(imul(x, x), 4987142);
        const int32_t t2 = imul(x, 5947611);
        return static_cast<uint64_t>(static_cast<int64_t>(t1)) + static_cast<uint64_t>(static_cast<int64_t>(t2));
    };
    std::vector<uint64_t> tbl(static_cast<size_t>(N));
    for (int i = 0; i < N; ++i) tbl[static_cast<size_t>(i)] = tailOf(i);
    uint64_t a1 = 0, a2 = 0;
    const double msTable = timeMs([&] {
        uint64_t n = 0;
        for (int i = 0; i < N; ++i) {
            uint64_t s = (rowBase + tbl[static_cast<size_t>(i)]) ^ kXor;
            s = (s ^ kMult) & kMask;
            s = (s * kMult + kAdd) & kMask;
            n += (static_cast<uint32_t>(s >> 17) % 10u) == 0u ? 1 : 0;
        }
        a1 = n;
    });
    const double msDiff = timeMs([&] {
        uint64_t n = 0;
        // 注意：F = sext(t1) + sext(t2)，两项必须分别在 mod 2^32 下差分，再各自符号扩展后相加
        // （直接把两项加成一个 uint32 再符号扩展是错的——这正是第一版对不上的原因）
        uint32_t t1 = 0;                                        // C1*x^2 at x=0
        uint32_t dt1 = static_cast<uint32_t>(4987142u);        // C1*(2*0+1)
        const uint32_t ddt1 = static_cast<uint32_t>(2u * 4987142u);
        uint32_t t2 = 0;
        uint32_t dt2 = 5947611u;
        for (int i = 0; i < N; ++i) {
            const uint64_t s0 = rowBase +
                                static_cast<uint64_t>(static_cast<int64_t>(static_cast<int32_t>(t1))) +
                                static_cast<uint64_t>(static_cast<int64_t>(static_cast<int32_t>(t2)));
            uint64_t s = s0 ^ kXor;
            s = (s ^ kMult) & kMask;
            s = (s * kMult + kAdd) & kMask;
            n += (static_cast<uint32_t>(s >> 17) % 10u) == 0u ? 1 : 0;
            t1 += dt1;
            dt1 += ddt1;
            t2 += dt2;
        }
        a2 = n;
    });
    std::printf("  每区块：查表 %.3f ns | 二阶差分 %.3f ns | 结果一致=%s\n",
                msTable * 1e6 / N, msDiff * 1e6 / N, a1 == a2 ? "是" : "否");
}

// ---------- q 对 x 各位的敏感性 ----------
void runSensitivity()
{
    const int32_t x0 = 1234567;
    auto qof = [](int32_t x) {
        const int32_t t1 = static_cast<int32_t>(static_cast<uint32_t>(static_cast<int32_t>(
                              static_cast<uint32_t>(x) * static_cast<uint32_t>(x))) * 4987142u);
        const int32_t t2 = static_cast<int32_t>(static_cast<uint32_t>(x) * 5947611u);
        uint64_t u = static_cast<uint64_t>(static_cast<int64_t>(t1)) +
                     static_cast<uint64_t>(static_cast<int64_t>(t2)) + static_cast<uint64_t>(12345);
        u ^= kXor;
        u = (u ^ kMult) & kMask;
        const uint64_t s1 = (u * kMult + kAdd) & kMask;
        return static_cast<uint32_t>(s1 >> 17);
    };
    std::printf("  q 位敏感性（翻转 x 的每一位，q 的高 8 位变化率）：\n    ");
    for (int b = 0; b < 32; ++b) {
        const int32_t x1 = x0 ^ (1 << b);
        const uint32_t qa = qof(x0), qb = qof(x1);
        const bool hiChanged = ((qa >> 23) != (qb >> 23));
        std::printf("%s", hiChanged ? "X" : ".");
        if (b == 15) std::printf("\n    ");
    }
    std::printf("\n    （X = 高 8 位改变；越均匀越说明「低位粗筛」不可行）\n");
}

} // namespace

int main()
{
    const int W = 4096, H = 2048;
    int wpr = 0;
    std::vector<uint64_t> bits;
    makeBits(bits, W, H, wpr);
    std::printf("数据：%dx%d、密度 10%%\n\n== 1) 可分离位图 AND（行内 + 行间）==\n", W, H);
    runSeparable(3, 3, W, H, wpr, bits);
    runSeparable(5, 5, W, H, wpr, bits);
    runSeparable(12, 12, W, H, wpr, bits);
    runSeparable(32, 32, W, H, wpr, bits);
    {
        std::vector<uint64_t> rb;
        int rwpr = 0;
        const int RW = 2048, RH = 1024;
        makeRealBits(rb, RW, RH, rwpr, 12345);
        int ones = 0;
        for (size_t i = 0; i < rb.size(); ++i) ones += std::popcount(rb[i]);
        std::printf("\n== 1b) 真实公式位图（%dx%d，密度 %.2f%%）==\n", RW, RH,
                    100.0 * ones / (static_cast<double>(RW) * RH));
        runSeparableReal(5, 5, RW, RH, rwpr, rb);
        runSeparableReal(32, 32, RW, RH, rwpr, rb);
    }
    std::printf("\n== 2) 双点筛选：位测试 vs 开放寻址哈希表 ==\n");
    runHashJoin(W, H, wpr, bits);
    std::printf("\n== 4) q 奇偶预筛 ==\n");
    runParity();
    std::printf("\n== 3) 差分生成 u vs 预计算表 ==\n");
    runDifferencing();
    std::printf("\n== 6) q 对 x 各位的敏感性 ==\n");
    runSensitivity();
    return 0;
}
