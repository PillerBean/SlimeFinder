// opt_probe6.cpp - v3.6.16 第六支探针：引擎扫描循环的「结构开销」拆解
//   引擎 scanGrid 的 Java 分支实际做的是：
//     acc |= (uint64)slime << (xr & 63);  每 64 个区块写回一个 64 位字
//   这里逐层叠加，定位「探针 1.385 ns」与「引擎 1.865 ns」之间那 0.48 ns 花在哪。
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

void run(int W, int H)
{
    const auto rbs = static_cast<size_t>(H);
    std::vector<uint64_t> rowBase(rbs), xTail(static_cast<size_t>(W));
    for (int z = 0; z < H; ++z) rowBase[static_cast<size_t>(z)] = javaRowBase(12345, z);
    for (int x = 0; x < W; ++x) xTail[static_cast<size_t>(x)] = javaXTail(x);
    const int wpr = (W + 63) / 64;
    std::vector<uint64_t> rowW(static_cast<size_t>(wpr) * H, 0);

    const int64_t cells = static_cast<int64_t>(W) * H;
    std::printf("== %dx%d = %.1f M 区块 ==\n", W, H, static_cast<double>(cells) / 1e6);

    uint64_t cA = 0, cB = 0, cC = 0, cD = 0;

    // A：纯计数（探针基线）
    const double msA = timeMs([&] {
        uint64_t acc = 0;
        for (int z = 0; z < H; ++z) {
            const uint64_t rb = rowBase[static_cast<size_t>(z)];
            for (int x = 0; x < W; ++x) acc += slimeTail(rb, xTail[static_cast<size_t>(x)]) ? 1 : 0;
        }
        cA = acc;
    });

    // B：引擎式「每 64 区块攒一个 64 位字」写回（含变量移位）
    const double msB = timeMs([&] {
        uint64_t bits = 0;
        for (int z = 0; z < H; ++z) {
            const uint64_t rb = rowBase[static_cast<size_t>(z)];
            uint64_t* out = &rowW[static_cast<size_t>(z) * wpr];
            int x = 0;
            for (; x + 64 <= W; x += 64) {
                uint64_t acc = 0;
                for (int i = 0; i < 64; ++i)
                    acc |= static_cast<uint64_t>(slimeTail(rb, xTail[static_cast<size_t>(x + i)])) << i;
                if (acc) out[x >> 6] |= acc;
            }
            for (; x < W; ++x)
                if (slimeTail(rb, xTail[static_cast<size_t>(x)])) out[x >> 6] |= 1ULL << (x & 63);
        }
        bits = rowW[static_cast<size_t>(wpr) - 1];
        cB = bits;
    });

    // C：同 B，但移位用递增计数器（去掉 (x+i) 的与运算）
    const double msC = timeMs([&] {
        for (int z = 0; z < H; ++z) {
            const uint64_t rb = rowBase[static_cast<size_t>(z)];
            uint64_t* out = &rowW[static_cast<size_t>(z) * wpr];
            int x = 0;
            for (; x + 64 <= W; x += 64) {
                uint64_t acc = 0;
                uint64_t bit = 1;
                for (int i = 0; i < 64; ++i, bit <<= 1)
                    acc |= slimeTail(rb, xTail[static_cast<size_t>(x + i)]) ? bit : 0ULL;
                if (acc) out[x >> 6] |= acc;
            }
            for (; x < W; ++x)
                if (slimeTail(rb, xTail[static_cast<size_t>(x)])) out[x >> 6] |= 1ULL << (x & 63);
        }
        cC = rowW[0];
    });

    // D：同 C，但每行的 rowBase 取用「预算表」（模拟引擎的 tbl 指针路径）
    const double msD = timeMs([&] {
        const uint64_t* tbl = xTail.data();
        for (int z = 0; z < H; ++z) {
            const uint64_t rb = rowBase[static_cast<size_t>(z)];
            uint64_t* out = &rowW[static_cast<size_t>(z) * wpr];
            int x = 0;
            for (; x + 64 <= W; x += 64) {
                uint64_t acc = 0;
                uint64_t bit = 1;
                for (int i = 0; i < 64; ++i, bit <<= 1)
                    acc |= slimeTail(rb, tbl[static_cast<size_t>(x + i)]) ? bit : 0ULL;
                if (acc) out[x >> 6] |= acc;
            }
            for (; x < W; ++x)
                if (slimeTail(rb, tbl[static_cast<size_t>(x)])) out[x >> 6] |= 1ULL << (x & 63);
        }
        cD = rowW[0];
    });

    const double per = static_cast<double>(cells);
    const auto rep = [per](const char* n, double ms) {
        std::printf("%-42s %8.2f ms | %6.3f ns/区块\n", n, ms, ms * 1e6 / per);
    };
    rep("A 纯计数（基线）", msA);
    rep("B + 64 位攒字写回（引擎式，变量移位）", msB);
    rep("C 同上，改递增位掩码", msC);
    rep("D 同上，xTail 走指针", msD);
    std::printf("校验：%llu / %llu / %llu\n\n", static_cast<unsigned long long>(cA & 1),
                static_cast<unsigned long long>(cB & 1), static_cast<unsigned long long>(cD & 1));
}

} // namespace

int main()
{
    run(4096, 4096);
    run(16384, 16384);
    return 0;
}
