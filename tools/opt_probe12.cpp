// opt_probe12.cpp - 探索：把「可分离 AND」再往前推两步
//   A 全偏移 AND（W×H 次移位，现有做法）
//   B 可分离（行内 W-1 次 + 行间 H-1 次）
//   C 行内改用「倍增前缀 AND」（log2 W 次） + 行间 H-1 次
//   D 行内倍增 + 行间用「稀疏表」O(1) 查询（log H 层预计算）
// 所有变体都只统计「窗口完全落在单个 64 位字内」的锚点，保证可比且可逐位校验。
#include <bit>
#include <chrono>
#include <cstdint>
#include <cstdio>
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

// 行内：窗口全在字内（丢掉每个字末尾 gx-1 位）
inline uint64_t rowShiftAnd(uint64_t v, int gx)
{
    for (int dx = 1; dx < gx; ++dx) v &= v >> dx;
    return v;
}

inline uint64_t rowDoubling(uint64_t v, int gx)
{
    int k = 1;
    while (k < gx) {
        const int s = (k < 64) ? k : 63;
        v &= v >> s;
        k += s;
    }
    return v;
}

void run(int gx, int gz, int W, int H, int wpr, const std::vector<uint64_t>& bits)
{
    const int interior = 64 - (gx - 1);
    const double anchors = static_cast<double>(interior) * static_cast<double>(wpr) * H;

    // A：全偏移
    uint64_t cntA = 0;
    const double msA = timeMs([&] {
        int64_t total = 0;
        for (int z = 0; z + gz <= H; ++z) {
            for (int w = 0; w < wpr; ++w) {
                uint64_t res = ~0ULL;
                for (int dz = 0; dz < gz; ++dz) {
                    const uint64_t row = bits[static_cast<size_t>(z + dz) * wpr + w];
                    res &= rowShiftAnd(row, gx);
                }
                total += std::popcount(res);
            }
        }
        cntA = static_cast<uint64_t>(total);
    });

    // B：可分离（行内逐位移 + 行间逐行）
    std::vector<uint64_t> R(static_cast<size_t>(wpr) * H, 0);
    uint64_t cntB = 0;
    const double msB = timeMs([&] {
        for (int z = 0; z < H; ++z) {
            for (int w = 0; w < wpr; ++w) {
                R[static_cast<size_t>(z) * wpr + w] =
                    rowShiftAnd(bits[static_cast<size_t>(z) * wpr + w], gx);
            }
        }
        int64_t total = 0;
        for (int z = 0; z + gz <= H; ++z) {
            for (int w = 0; w < wpr; ++w) {
                uint64_t res = ~0ULL;
                for (int dz = 0; dz < gz; ++dz) res &= R[static_cast<size_t>(z + dz) * wpr + w];
                total += std::popcount(res);
            }
        }
        cntB = static_cast<uint64_t>(total);
    });

    // C：行内倍增 + 行间逐行
    uint64_t cntC = 0;
    const double msC = timeMs([&] {
        for (int z = 0; z < H; ++z) {
            for (int w = 0; w < wpr; ++w) {
                R[static_cast<size_t>(z) * wpr + w] = rowDoubling(bits[static_cast<size_t>(z) * wpr + w], gx);
            }
        }
        int64_t total = 0;
        for (int z = 0; z + gz <= H; ++z) {
            for (int w = 0; w < wpr; ++w) {
                uint64_t res = ~0ULL;
                for (int dz = 0; dz < gz; ++dz) res &= R[static_cast<size_t>(z + dz) * wpr + w];
                total += std::popcount(res);
            }
        }
        cntC = static_cast<uint64_t>(total);
    });

    // D：行内倍增 + 行间稀疏表（O(1) 查询）
    uint64_t cntD = 0;
    const double msD = timeMs([&] {
        for (int z = 0; z < H; ++z) {
            for (int w = 0; w < wpr; ++w) {
                R[static_cast<size_t>(z) * wpr + w] = rowDoubling(bits[static_cast<size_t>(z) * wpr + w], gx);
            }
        }
        int k = 0;
        while ((1 << (k + 1)) <= gz) ++k;              // floor(log2(gz))
        const int span = 1 << k;
        // 稀疏表：st[lvl][z] = AND of rows z .. z+2^lvl-1
        std::vector<std::vector<uint64_t>> st(static_cast<size_t>(k + 1));
        st[0] = R;
        for (int lvl = 1; lvl <= k; ++lvl) {
            st[static_cast<size_t>(lvl)].assign(static_cast<size_t>(wpr) * H, ~0ULL);
            const int half = 1 << (lvl - 1);
            for (int z = 0; z + (1 << lvl) <= H; ++z) {
                for (int w = 0; w < wpr; ++w) {
                    st[static_cast<size_t>(lvl)][static_cast<size_t>(z) * wpr + w] =
                        st[static_cast<size_t>(lvl - 1)][static_cast<size_t>(z) * wpr + w] &
                        st[static_cast<size_t>(lvl - 1)][static_cast<size_t>(z + half) * wpr + w];
                }
            }
        }
        int64_t total = 0;
        for (int z = 0; z + gz <= H; ++z) {
            const uint64_t* lo = &st[static_cast<size_t>(k)][static_cast<size_t>(z) * wpr];
            const uint64_t* hi = &st[static_cast<size_t>(k)][static_cast<size_t>(z + gz - span) * wpr];
            for (int w = 0; w < wpr; ++w) total += std::popcount(lo[w] & hi[w]);
        }
        cntD = static_cast<uint64_t>(total);
    });

    const bool ok = (cntA == cntB && cntB == cntC && cntC == cntD);
    std::printf("  %2dx%-2d(W×H=%4d) A 全偏移 %7.3f | B 可分离 %7.3f (×%.1f) | C 倍增行内 %7.3f (×%.1f) | D 倍增+稀疏表 %7.3f (×%.1f) ns/锚点 | 一致=%s\n",
                gx, gz, gx * gz, msA * 1e6 / anchors, msB * 1e6 / anchors, msA / (msB > 1e-12 ? msB : 1e-12),
                msC * 1e6 / anchors, msA / (msC > 1e-12 ? msC : 1e-12), msD * 1e6 / anchors,
                msA / (msD > 1e-12 ? msD : 1e-12), ok ? "是" : "否");
}

} // namespace

int main()
{
    const int W = 4096, H = 2048;
    int wpr = 0;
    std::vector<uint64_t> bits;
    makeBits(bits, W, H, wpr);
    std::printf("数据 %dx%d，密度 10%%；只统计窗口完整落在 64 位字内的锚点\n\n", W, H);
    run(3, 3, W, H, wpr, bits);
    run(5, 5, W, H, wpr, bits);
    run(12, 12, W, H, wpr, bits);
    run(32, 32, W, H, wpr, bits);
    run(64, 64, W, H, wpr, bits);
    return 0;
}
