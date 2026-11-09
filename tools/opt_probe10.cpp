// opt_probe10.cpp - 探针：检验「多级筛选 / 哈希连接 / 偏移排序」相对现有位图 AND 的优劣
//   AI 主张：黑盒通用最优 ≈ 稀疏候选生成 + 多级筛选（期望 1.111N 次判定）；
//            我们现有实现是「位图 + 偏移移位 AND」（O(mN/64)）。
//   这里在同一份数据上直接对打。
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

struct Pat {
    const char* name;
    int w, h;
};

// 生成 10% 密度的位图（与史莱姆区块密度一致）
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

inline bool getBit(const std::vector<uint64_t>& bits, int wpr, int x, int z)
{
    return (bits[static_cast<size_t>(z) * wpr + (x >> 6)] >> (x & 63)) & 1ULL;
}

void run(const Pat& p, const std::vector<uint64_t>& bits, int wpr, int W, int H)
{
    const int gx = p.w, gz = p.h;
    const int m = gx * gz;
    // 三种方法都在「完整字」范围内比较（位图 AND 需要右邻字，避免边缘实现差异）
    const int xMax = (wpr - 1) * 64;
    const int64_t anchors = static_cast<int64_t>(xMax - gx + 1) * (H - gz + 1);

    // ---------- A：位图 + 偏移移位 AND（现有做法） ----------
    uint64_t cntA = 0;
    const double msA = timeMs([&] {
        int64_t total = 0;
        for (int z = 0; z + gz <= H; ++z) {
            for (int w = 0; w + 1 < wpr; ++w) {
                uint64_t res = ~0ULL;
                for (int dz = 0; dz < gz; ++dz) {
                    const uint64_t* row = &bits[static_cast<size_t>(z + dz) * wpr];
                    uint64_t v = row[w];
                    for (int dx = 0; dx < gx; ++dx) {
                        if (dx == 0) v &= row[w];   // 注意：dx=0 时不能写 (x << 64)，那是 UB
                        else v &= (row[w] >> dx) | (row[w + 1] << (64 - dx));
                    }
                    res &= v;
                }
                // 只统计与标量路径相同的锚点范围（x + gx <= xMax）
                const int hi = xMax - gx - w * 64;
                const uint64_t mask = (hi >= 63) ? ~0ULL : (hi < 0 ? 0ULL : ((1ULL << (hi + 1)) - 1ULL));
                total += std::popcount(res & mask);
            }
        }
        cntA = static_cast<uint64_t>(total);
    });

    // ---------- B：多级筛选（逐偏移早退，标量） ----------
    uint64_t cntB = 0;
    const double msB = timeMs([&] {
        int64_t total = 0;
        for (int z = 0; z + gz <= H; ++z) {
            for (int x = 0; x + gx <= xMax; ++x) {
                bool ok = true;
                for (int dz = 0; dz < gz && ok; ++dz)
                    for (int dx = 0; dx < gx; ++dx)
                        if (!getBit(bits, wpr, x + dx, z + dz)) { ok = false; break; }
                total += ok ? 1 : 0;
            }
        }
        cntB = static_cast<uint64_t>(total);
    });

    // ---------- C：哈希连接（先查两个偏移，再验证其余） ----------
    uint64_t cntC = 0;
    const double msC = timeMs([&] {
        int64_t total = 0;
        for (int z = 0; z + gz <= H; ++z) {
            for (int x = 0; x + gx <= xMax; ++x) {
                if (!getBit(bits, wpr, x, z)) continue;                 // 偏移 0
                if (!getBit(bits, wpr, x + gx - 1, z + gz - 1)) continue; // 对角偏移
                bool ok = true;
                for (int dz = 0; dz < gz && ok; ++dz)
                    for (int dx = 0; dx < gx; ++dx) {
                        if (dx == 0 && dz == 0) continue;
                        if (dx == gx - 1 && dz == gz - 1) continue;
                        if (!getBit(bits, wpr, x + dx, z + dz)) { ok = false; break; }
                    }
                total += ok ? 1 : 0;
            }
        }
        cntC = static_cast<uint64_t>(total);
    });

    const double per = static_cast<double>(anchors);
    std::printf("%-10s m=%-4d 位图AND %8.2f ms (%5.3f ns/锚点) | 多级筛选 %8.2f ms (%5.3f) | 哈希连接 %8.2f ms (%5.3f) | 一致=%s\n",
                p.name, m, msA, msA * 1e6 / per, msB, msB * 1e6 / per, msC, msC * 1e6 / per,
                (cntA == cntB && cntB == cntC) ? "是" : "否");
}

} // namespace

int main()
{
    const int W = 4096, H = 2048;
    int wpr = 0;
    std::vector<uint64_t> bits;
    makeBits(bits, W, H, wpr);
    std::printf("数据：%dx%d 锚点范围、密度 10%%（真实史莱姆区块密度）\n", W, H);
    std::printf("标注：ns/锚点 越低越好；「一致」= 三种方法命中数完全相同\n\n");
    const Pat pats[] = {
        { "稀疏 3x1", 3, 1 },
        { "3x3", 3, 3 },
        { "5x5", 5, 5 },
        { "12x12", 12, 12 },
        { "32x32", 32, 32 },
    };
    for (const auto& p : pats) run(p, bits, wpr, W, H);
    return 0;
}
