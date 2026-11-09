// opt_probe15.cpp - 两个「框架层」问题：
//   ① 位图分配 + 清零到底多少钱？（池化能收回多少）
//   ② 多层行图案（不规则图案）用「按行图案分组」的可分离 AND 还能省多少？
#include <bit>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {

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

void testAlloc()
{
    const int W = 10000, H = 10000;
    const int64_t words = (static_cast<int64_t>(W) + 63) / 64 * H;   // ≈ 12.5 MB
    std::printf("== ① 位图分配/清零（%dx%d，%.1f MB）==\n", W, H, words * 8.0 / 1e6);
    const double msAlloc = timeMs([&] {
        std::vector<uint64_t> v(static_cast<size_t>(words), 0);
        if (v[0] == 12345) std::printf("?");
    }, 5);
    std::vector<uint64_t> pool(static_cast<size_t>(words), 0);
    const double msClear = timeMs([&] { std::memset(pool.data(), 0, pool.size() * 8); }, 5);
    const double ns = 1e6 * 1.0;
    std::printf("  每次新分配（含清零）: %.3f ms（%.3f ns/区块）\n", msAlloc, msAlloc * ns / (W * H));
    std::printf("  池化后仅 memset     : %.3f ms（%.3f ns/区块）\n", msClear, msClear * ns / (W * H));
    const double savedNs = (msAlloc - msClear) * 1e6 / (static_cast<double>(W) * H);
    std::printf("  ⇒ 池化最多省 %.3f ns/区块 = 扫描(1.16 ns)的 %.1f%%\n\n", savedNs, savedNs / 1.16 * 100.0);
}

// ---------- ② 不规则图案：按「不同行图案」分组做行内 AND ----------
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

inline uint64_t rowMaskedAnd(uint64_t v, uint64_t need, int gx)
{
    // need 的位 i = 该行第 i 列要求为 1；只处理窗口落在字内的情况
    uint64_t r = ~0ULL;
    for (int dx = 0; dx < gx; ++dx)
        if ((need >> dx) & 1ULL) r &= v >> dx;
    return r;
}

void testPatterns(int W, int H, int wpr, int gx, int gz, int distinctRows, const char* name)
{
    std::vector<uint64_t> bits;
    makeBits(bits, W, H, wpr);
    // 行图案：distinctRows 种，循环使用；每种是该行需要 AND 的列掩码
    std::vector<uint64_t> rowNeed(static_cast<size_t>(gz));
    for (int dz = 0; dz < gz; ++dz) {
        uint64_t m = 0;
        for (int dx = 0; dx < gx; ++dx) {
            if (((dz * 7 + dx * 3) % (distinctRows + 1)) != 0) m |= 1ULL << dx;   // 造一些 0/1 混合
        }
        rowNeed[static_cast<size_t>(dz)] = m;
    }
    const double anchors = static_cast<double>(W) * H;

    // A：全偏移（对每个 (dx,dz) 都移位 AND）
    uint64_t cA = 0;
    const double msA = timeMs([&] {
        int64_t total = 0;
        for (int z = 0; z + gz <= H; ++z) {
            for (int w = 0; w < wpr; ++w) {
                uint64_t res = ~0ULL;
                for (int dz = 0; dz < gz; ++dz) {
                    const uint64_t row = bits[static_cast<size_t>(z + dz) * wpr + w];
                    for (int dx = 0; dx < gx; ++dx)
                        if ((rowNeed[static_cast<size_t>(dz)] >> dx) & 1ULL) res &= row >> dx;
                }
                total += std::popcount(res);
            }
        }
        cA = static_cast<uint64_t>(total);
    });

    // B：正确的通用形态 —— 每个「行图案 dz」单独生成一份行内匹配位图 R[dz]，
    //    锚点 (x,z) 的结果 = AND_{dz} R[dz][z+dz]。成本 = gz×(W-1) 次移位 + gz 次行 AND/锚点，
    //    而不是 W×H 次。
    uint64_t cB = 0;
    const double msB = timeMs([&] {
        std::vector<std::vector<uint64_t>> R(static_cast<size_t>(gz),
                                             std::vector<uint64_t>(static_cast<size_t>(wpr) * H, 0));
        for (int dz = 0; dz < gz; ++dz) {
            const uint64_t need = rowNeed[static_cast<size_t>(dz)];
            for (int z = 0; z < H; ++z)
                for (int w = 0; w < wpr; ++w)
                    R[static_cast<size_t>(dz)][static_cast<size_t>(z) * wpr + w] =
                        rowMaskedAnd(bits[static_cast<size_t>(z) * wpr + w], need, gx);
        }
        int64_t total = 0;
        for (int z = 0; z + gz <= H; ++z) {
            for (int w = 0; w < wpr; ++w) {
                uint64_t res = ~0ULL;
                for (int dz = 0; dz < gz; ++dz) {
                    res &= R[static_cast<size_t>(dz)][static_cast<size_t>(z + dz) * wpr + w];
                }
                total += std::popcount(res);
            }
        }
        cB = static_cast<uint64_t>(total);
    });

    std::printf("  %-22s %dx%d（%d 种行图案）全偏移 %7.3f | 分组可分离 %7.3f (×%.2f) ns/锚点 | 一致=%s\n", name, gx,
                gz, distinctRows, msA * 1e6 / anchors, msB * 1e6 / anchors, msA / (msB > 1e-12 ? msB : 1e-12),
                (cA == cB) ? "是" : "否");
}

} // namespace

int main()
{
    testAlloc();
    const int W = 2048, H = 1024;
    int wpr = 0;
    std::printf("== ② 不规则图案（行图案不同）==\n");
    testPatterns(W, H, wpr, 8, 8, 2, "8x8 两种行图案");
    testPatterns(W, H, wpr, 8, 8, 8, "8x8 八种行图案");
    testPatterns(W, H, wpr, 32, 32, 2, "32x32 两种");
    testPatterns(W, H, wpr, 32, 32, 32, "32x32 三十二种");
    return 0;
}
