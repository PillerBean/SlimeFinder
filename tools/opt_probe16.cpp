// opt_probe16.cpp - 整字批处理内核（scanWordsOr）vs 逐字调用（slimeMask64 + OR）
//   目的：验证上一轮 rdtsc 剩下的 23% 缺口是否来自「每 64 区块一次跨 TU 调用 + 常量重建」。
#include "core/FastJava.h"

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

constexpr uint64_t kMask = (1ULL << 48) - 1;
constexpr uint64_t kMult = 0x5DEECE66DULL;
constexpr uint64_t kAdd = 0xBULL;
constexpr uint64_t kXor = 987234911ULL;

inline int32_t imul32(int32_t a, int32_t b) noexcept
{
    return static_cast<int32_t>(static_cast<uint32_t>(a) * static_cast<uint32_t>(b));
}
inline uint64_t javaRowBase(int64_t seed, int32_t cz) noexcept
{
    const int32_t t3 = imul32(cz, cz);
    const int32_t t4 = imul32(cz, 389711);
    uint64_t b = static_cast<uint64_t>(seed);
    b += static_cast<uint64_t>(static_cast<int64_t>(t3) * 4392871LL);
    b += static_cast<uint64_t>(static_cast<int64_t>(t4));
    return b;
}

} // namespace

int main()
{
    const int W = 10000, H = 10000;
    const int wpr = W / 64;              // 156 个整字（余 16 个区块单独处理，这里只测整字）
    const int64_t chunks = static_cast<int64_t>(W) * H;
    std::vector<uint64_t> bitsA(static_cast<size_t>(wpr) * H, 0);
    std::vector<uint64_t> bitsB(static_cast<size_t>(wpr) * H, 0);
    std::vector<uint64_t> rowBase(static_cast<size_t>(H));
    for (int z = 0; z < H; ++z) rowBase[static_cast<size_t>(z)] = javaRowBase(12345, z - H / 2);

    const double msA = timeMs([&] {
        for (int z = 0; z < H; ++z) {
            const uint64_t rb = rowBase[static_cast<size_t>(z)];
            uint64_t* row = &bitsA[static_cast<size_t>(z) * wpr];
            for (int w = 0; w < wpr; ++w)
                row[w] |= fastjava::slimeMask64(rb, w * 64 - W / 2);
        }
    });

    const double msB = timeMs([&] {
        for (int z = 0; z < H; ++z) {
            const uint64_t rb = rowBase[static_cast<size_t>(z)];
            uint64_t* row = &bitsB[static_cast<size_t>(z) * wpr];
            fastjava::scanWordsOr(rb, -W / 2, wpr, row);
        }
    });

    const bool same = std::memcmp(bitsA.data(), bitsB.data(), bitsA.size() * 8) == 0;
    const double per = static_cast<double>(chunks) * (static_cast<double>(wpr) * 64) / W;
    std::printf("Java 扫描 10000x10000（只计整字部分 %.1f M 区块）\n", per / 1e6);
    std::printf("  A 逐字调用 slimeMask64 + OR : %8.2f ms | %.3f ns/区块\n", msA, msA * 1e6 / per);
    std::printf("  B 整字批处理 scanWordsOr     : %8.2f ms | %.3f ns/区块 | 结果一致=%s\n", msB, msB * 1e6 / per,
                same ? "是" : "否");
    std::printf("  提升：%.2fx\n", msA / (msB > 1e-9 ? msB : 1e-9));
    return 0;
}
