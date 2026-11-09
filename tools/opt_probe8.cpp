// opt_probe8.cpp - v3.6.16：直接测引擎里实际调用的 fastjava::slimeMask16/64
//   与 SLIME_FASTJAVA 关闭时的标量循环对比，确认「AVX2 内核本身」的真实吞吐。
#include "core/FastJava.h"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace {

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
inline uint64_t javaXTail(int32_t x) noexcept
{
    const int32_t t1 = imul32(imul32(x, x), 4987142);
    const int32_t t2 = imul32(x, 5947611);
    uint64_t s = static_cast<uint64_t>(static_cast<int64_t>(t1));
    s += static_cast<uint64_t>(static_cast<int64_t>(t2));
    return s;
}
inline bool slimeTail(uint64_t base, uint64_t xtail) noexcept
{
    uint64_t s = base + xtail;
    s ^= kXor;
    s = (s ^ kMult) & kMask;
    for (;;) {
        s = (s * kMult + kAdd) & kMask;
        const int bits = static_cast<int>(s >> 17);
        const int val = bits % 10;
        if (bits - val + 9 >= 0) return val == 0;
    }
}

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

} // namespace

int main()
{
    std::printf("AVX2 available=%d\n", fastjava::available() ? 1 : 0);
    const int W = 8192;      // 每行 8192 个区块（xTail 表 64KB，落在 L2 内）
    const int H = 4096;      // 总 33.5 M 区块
    const int64_t chunks = static_cast<int64_t>(W) * H;
    std::vector<uint64_t> rowBase(static_cast<size_t>(H)), xTail(static_cast<size_t>(W));
    for (int z = 0; z < H; ++z) rowBase[static_cast<size_t>(z)] = javaRowBase(12345, z);
    for (int x = 0; x < W; ++x) xTail[static_cast<size_t>(x)] = javaXTail(x);
    const int wpr = W / 64;
    std::vector<uint64_t> out(static_cast<size_t>(wpr) * H, 0);

    // A：标量整字（引擎关闭 AVX2 时的路径）
    uint64_t cA = 0;
    const double msA = timeMs([&] {
        uint64_t sig = 0;
        for (int z = 0; z < H; ++z) {
            const uint64_t rb = rowBase[static_cast<size_t>(z)];
            uint64_t* row = &out[static_cast<size_t>(z) * wpr];
            for (int w = 0; w < wpr; ++w) {
                uint64_t acc = 0;
                for (int i = 0; i < 64; ++i)
                    acc |= static_cast<uint64_t>(slimeTail(rb, xTail[static_cast<size_t>(w * 64 + i)])) << i;
                row[w] = acc;
                sig += acc;
            }
        }
        cA = sig;
    });

    // B：fastjava::slimeMask64
    uint64_t cB = 0;
    const double msB = timeMs([&] {
        uint64_t sig = 0;
        for (int z = 0; z < H; ++z) {
            const uint64_t rb = rowBase[static_cast<size_t>(z)];
            uint64_t* row = &out[static_cast<size_t>(z) * wpr];
            for (int w = 0; w < wpr; ++w) {
                const uint64_t acc = fastjava::slimeMask64(rb, w * 64);
                row[w] = acc;
                sig += acc;
            }
        }
        cB = sig;
    });

    // C：整行批量入口（常量每行只建一次）
    uint64_t cC = 0;
    const double msC = timeMs([&] {
        uint64_t sig = 0;
        for (int z = 0; z < H; ++z) {
            const uint64_t rb = rowBase[static_cast<size_t>(z)];
            uint64_t* row = &out[static_cast<size_t>(z) * wpr];
            fastjava::scanRowJava(rb, 0, W, row);
            for (int w = 0; w < wpr; ++w) sig += row[w];
        }
        cC = sig;
    });

    const double per = static_cast<double>(chunks);
    std::printf("%-26s %8.2f ms | %6.3f ns/区块\n", "A 标量整字", msA, msA * 1e6 / per);
    std::printf("%-26s %8.2f ms | %6.3f ns/区块\n", "B slimeMask64", msB, msB * 1e6 / per);
    std::printf("%-26s %8.2f ms | %6.3f ns/区块\n", "C scanRowJava(整行)", msC, msC * 1e6 / per);
    std::printf("一致：%s\n", (cA == cB && cB == cC) ? "是 ✔" : "否 ✘");
    return 0;
}
