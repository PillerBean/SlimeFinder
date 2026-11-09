// opt_probe19.cpp - 验证「GPU 引擎路径比裸内核慢 15 倍」的猜测：开销在 CPU 侧回读转换
//   模拟引擎的做法：GPU 返回每个区块 1 字节，CPU 逐行把它们打包成 64 位位图字。
//   与「GPU 直接输出 64 位打包字、CPU 只做按字 OR」对比。
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

} // namespace

int main()
{
    const int W = 8192, H = 8192;                 // 67M 区块（与探针引擎级场景一致）
    const int64_t chunks = static_cast<int64_t>(W) * H;
    const int wpr = W / 64;
    std::vector<uint8_t> bytes(static_cast<size_t>(chunks));
    // 造 10% 的随机位（与史莱姆密度一致），模拟 GPU 回读的字节位图
    for (int64_t i = 0; i < chunks; ++i) {
        const uint64_t h = static_cast<uint64_t>(i) * 0x9E3779B97F4A7C15ULL;
        bytes[static_cast<size_t>(i)] = (((h >> 33) % 10u) == 0u) ? 1 : 0;
    }
    std::vector<uint64_t> words(static_cast<size_t>(wpr) * H, 0);

    // A：逐区块把字节打包成位图字（引擎现做法的等价物）
    const double msA = timeMs([&] {
        for (int z = 0; z < H; ++z) {
            const uint8_t* src = &bytes[static_cast<size_t>(z) * W];
            uint64_t* dst = &words[static_cast<size_t>(z) * wpr];
            for (int w = 0; w < wpr; ++w) {
                uint64_t acc = 0;
                for (int b = 0; b < 64; ++b) acc |= static_cast<uint64_t>(src[w * 64 + b]) << b;
                dst[w] = acc;
            }
        }
    });

    // B：GPU 直接给打包字，CPU 只按字 OR
    std::vector<uint64_t> packed(static_cast<size_t>(wpr) * H, 0);
    for (size_t i = 0; i < packed.size(); ++i) packed[i] = words[i];
    const double msB = timeMs([&] {
        for (int z = 0; z < H; ++z) {
            const uint64_t* src = &packed[static_cast<size_t>(z) * wpr];
            uint64_t* dst = &words[static_cast<size_t>(z) * wpr];
            for (int w = 0; w < wpr; ++w) dst[w] |= src[w];
        }
    });

    const double per = static_cast<double>(chunks);
    std::printf("Java 亿级场景 %dx%d（%.1f M 区块）\n", W, H, per / 1e6);
    std::printf("  A 逐区块字节→位图（引擎现状等价）: %8.2f ms | %6.3f ns/区块\n", msA, msA * 1e6 / per);
    std::printf("  B GPU 直接输出打包字，CPU 只 OR  : %8.2f ms | %6.3f ns/区块 | 加速 %.1fx\n", msB,
                msB * 1e6 / per, msA / (msB > 1e-9 ? msB : 1e-9));
    std::printf("\n（参考：裸 GPU 内核 268M 区块 27.2 ms = 0.10 ns/区块；引擎级 GPU 扫描 103.7 ms/67M = 1.55 ns/区块）\n");
    return 0;
}
