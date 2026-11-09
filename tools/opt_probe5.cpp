// opt_probe5.cpp - v3.6.16 第五支探针
//   D1：谓词「直接内联」对比「函数指针调用」的开销（定位引擎那 ~20% 结构开销）
//   B0：密度窗口「列和滑动」（引擎现用思路）
//   B1：「k = g²」时的 AND 快路（先 g 行纵向 AND，再 g 列横向 AND）
//   两者结果必须一致（用于验证快路可替代慢路径的特定情形）
#include <bit>
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
inline bool slimeV0(uint64_t base, uint64_t xtail) noexcept
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
bool slimeViaPtr(uint64_t base, uint64_t xtail) noexcept
{
    return slimeV0(base, xtail);
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

void runCallOverhead()
{
    const int W = 4096, H = 2048;
    const int64_t cells = static_cast<int64_t>(W) * H;
    std::vector<uint64_t> rb(static_cast<size_t>(H)), xt(static_cast<size_t>(W));
    for (int z = 0; z < H; ++z) rb[static_cast<size_t>(z)] = javaRowBase(12345, z);
    for (int x = 0; x < W; ++x) xt[static_cast<size_t>(x)] = javaXTail(x);

    uint64_t c0 = 0, c1 = 0;
    const double msInline = timeMs([&] {
        uint64_t acc = 0;
        for (int z = 0; z < H; ++z) {
            const uint64_t b = rb[static_cast<size_t>(z)];
            for (int x = 0; x < W; ++x) acc += slimeV0(b, xt[static_cast<size_t>(x)]) ? 1 : 0;
        }
        c0 = acc;
    });
    bool (*fn)(uint64_t, uint64_t) noexcept = &slimeViaPtr;
    const double msPtr = timeMs([&] {
        uint64_t acc = 0;
        for (int z = 0; z < H; ++z) {
            const uint64_t b = rb[static_cast<size_t>(z)];
            for (int x = 0; x < W; ++x) acc += fn(b, xt[static_cast<size_t>(x)]) ? 1 : 0;
        }
        c1 = acc;
    });
    std::printf("== D1 调用开销（%.1f M 区块）==\n", static_cast<double>(cells) / 1e6);
    std::printf("%-26s %8.2f ms | %6.3f ns/区块\n", "直接内联", msInline, msInline * 1e6 / cells);
    std::printf("%-26s %8.2f ms | %6.3f ns/区块（+%.0f%%）\n", "函数指针调用", msPtr, msPtr * 1e6 / cells,
                (msPtr / msInline - 1.0) * 100.0);
    std::printf("一致：%s\n\n", c0 == c1 ? "是 ✔" : "否 ✘");
}

void runDensity(int g)
{
    const int W = 8192, H = 2048;
    const int64_t cells = static_cast<int64_t>(W) * H;
    std::printf("== 密度 g=%d（%dx%d = %.1f M 区块）==\n", g, W, H, static_cast<double>(cells) / 1e6);

    std::vector<uint8_t> pix(static_cast<size_t>(cells));
    for (int z = 0; z < H; ++z) {
        const uint64_t rb = javaRowBase(12345, z);
        for (int x = 0; x < W; ++x) pix[static_cast<size_t>(z) * W + x] = slimeV0(rb, javaXTail(x)) ? 1 : 0;
    }
    const int wpr = (W + 63) / 64;
    std::vector<uint64_t> bits(static_cast<size_t>(wpr) * H, 0);
    for (int z = 0; z < H; ++z)
        for (int x = 0; x < W; ++x)
            if (pix[static_cast<size_t>(z) * W + x])
                bits[static_cast<size_t>(z) * wpr + (x >> 6)] |= 1ULL << (x & 63);

    // ---- B0：列和滑动 ----
    uint64_t cnt0 = 0;
    const double ms0 = timeMs([&] {
        std::vector<int16_t> col(static_cast<size_t>(W), 0);
        int64_t acc = 0;
        for (int z = 0; z < H; ++z) {
            for (int x = 0; x < W; ++x)
                col[static_cast<size_t>(x)] += pix[static_cast<size_t>(z) * W + x];
            if (z >= 1)
                for (int x = 0; x < W; ++x)
                    col[static_cast<size_t>(x)] -= pix[static_cast<size_t>(z - 1) * W + x];
            if (z < g - 1) continue;
            int sum = 0;
            for (int x = 0; x < W; ++x) {
                sum += col[static_cast<size_t>(x)];
                if (x >= g) sum -= col[static_cast<size_t>(x - g)];
                if (x >= g - 1 && sum == g * g) ++acc;
            }
        }
        cnt0 = static_cast<uint64_t>(acc);
    });

    // ---- B1：k = g² 的 AND 快路 ----
    uint64_t cnt1 = 0;
    const double ms1 = timeMs([&] {
        std::vector<uint64_t> v(static_cast<size_t>(wpr), 0);
        int64_t acc = 0;
        for (int z = 0; z + g <= H; ++z) {
            const uint64_t* r0 = &bits[static_cast<size_t>(z) * wpr];
            for (int w = 0; w < wpr; ++w) {
                uint64_t x = ~0ULL;
                for (int dz = 0; dz < g; ++dz) x &= r0[static_cast<size_t>(dz) * wpr + w];
                v[static_cast<size_t>(w)] = x;
            }
            // 横向：g 列全 1
            for (int w = 0; w < wpr; ++w) {
                const uint64_t cur = v[static_cast<size_t>(w)];
                const uint64_t nxt = (w + 1 < wpr) ? v[static_cast<size_t>(w + 1)] : 0ULL;
                uint64_t res = cur;
                for (int dc = 1; dc < g; ++dc) res &= (cur >> dc) | (nxt << (64 - dc));
                acc += static_cast<int64_t>(std::popcount(res));
            }
        }
        cnt1 = static_cast<uint64_t>(acc);
    });

    const double per = static_cast<double>(cells);
    std::printf("%-26s %8.2f ms | %6.3f ns/区块 | %llu\n", "B0 列和滑动", ms0, ms0 * 1e6 / per,
                static_cast<unsigned long long>(cnt0));
    std::printf("%-26s %8.2f ms | %6.3f ns/区块 | %llu\n", "B1 AND 快路（k=g²）", ms1, ms1 * 1e6 / per,
                static_cast<unsigned long long>(cnt1));
    std::printf("命中一致：%s（%llu vs %llu）\n\n", cnt0 == cnt1 ? "是 ✔" : "否 ✘",
                static_cast<unsigned long long>(cnt0), static_cast<unsigned long long>(cnt1));
}

} // namespace

int main()
{
    runCallOverhead();
    runDensity(3);
    runDensity(5);
    return 0;
}
