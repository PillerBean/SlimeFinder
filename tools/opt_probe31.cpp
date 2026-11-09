// opt_probe31.cpp - 「位切片计数」可行性微基准（不整合，只测核心原语）
//
// 目标：把「窗口内 K 个比特相加再与阈值比较」从「每锚点循环 K 次」换成
//   「跨 64 个锚点同时做的位切片加法（ripple-carry）+ 位切片阈值比较」。
// 本探针只测这个原语本身的吞吐，并与朴素逐锚点写法对比、校验结果一致。
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;
constexpr int kN = 1 << 20;   // 每个掩码的字数（64*1M = 6710 万锚点当量）

// 位切片加法：把 K 个 1 位值累加成 5 个位平面（K <= 31）
inline void addBit(uint64_t* p, uint64_t v, int planes)
{
    uint64_t carry = v;
    for (int i = 0; i < planes && carry; ++i) {
        const uint64_t t = p[i] & carry;
        p[i] ^= carry;
        carry = t;
    }
}

// 位切片阈值比较：count >= thr（thr <= 31，planes = 5）
inline uint64_t geThreshold(const uint64_t* p, int thr)
{
    // 从最高位向低位做「字典序 >= thr」的位切片比较
    uint64_t eq = ~0ULL;   // 前缀相等
    uint64_t gt = 0ULL;    // 前缀已大于
    uint64_t pass = 0;
    for (int i = 4; i >= 0; --i) {
        const uint64_t tb = (thr >> i) & 1ULL;
        if (tb) {
            eq &= p[i];            // thr 位为 1：count 必须也是 1 才继续相等
        } else {
            pass |= eq & p[i];     // thr 位为 0、count 位为 1 ⇒ count > thr
            eq &= ~p[i];
        }
    }
    return pass | eq;              // 相等也算「>=」
}

// 朴素：逐个锚点累加 K 位（模拟当前引擎的「每锚点 K 次加」）
inline void naiveMask(const uint64_t* masks, int K, int thr, uint64_t* out, int n)
{
    for (int w = 0; w < n; ++w) {
        uint64_t res = 0;
        for (int b = 0; b < 64; ++b) {
            int cnt = 0;
            for (int k = 0; k < K; ++k) cnt += static_cast<int>((masks[static_cast<size_t>(k) * n + w] >> b) & 1ULL);
            if (cnt >= thr) res |= 1ULL << b;
        }
        out[w] = res;
    }
}

// 位切片：一次处理 64 个锚点
inline void bitslicedMask(const uint64_t* masks, int K, int thr, uint64_t* out, int n, int planes)
{
    for (int w = 0; w < n; ++w) {
        uint64_t p[5] = { 0, 0, 0, 0, 0 };
        for (int k = 0; k < K; ++k) addBit(p, masks[static_cast<size_t>(k) * n + w], planes);
        // count >= thr：等于 thr 或严格大于
        uint64_t eqThr = ~0ULL;
        for (int i = 0; i < planes; ++i) {
            const uint64_t tb = (thr >> i) & 1ULL;
            eqThr &= tb ? p[i] : ~p[i];
        }
        out[w] = geThreshold(p, thr) | eqThr;
    }
}


// 位切片 + 转置布局：把 25 个掩码按「每个 word 连续 25 个 uint64」存放，
// 每个 64 锚点只需顺序读 200 字节（约 3 条缓存行），避免跨 8 MB 步长的随机访问。
inline void bitslicedTransposed(const uint64_t* t, int K, int thr, uint64_t* out, int n, int planes)
{
    for (int w = 0; w < n; ++w) {
        const uint64_t* base = t + static_cast<size_t>(w) * K;
        uint64_t p[5] = { 0, 0, 0, 0, 0 };
        for (int k = 0; k < K; ++k) addBit(p, base[k], planes);
        uint64_t eqThr = ~0ULL;
        for (int i = 0; i < planes; ++i) {
            const uint64_t tb = (thr >> i) & 1ULL;
            eqThr &= tb ? p[i] : ~p[i];
        }
        out[w] = geThreshold(p, thr) | eqThr;
    }
}

} // namespace

int main()
{
    const int K = 25;      // g=5 的窗口格数
    const int thr = 13;    // 约 0.5 密度
    const int planes = 5;  // 支持到 31

    std::vector<uint64_t> masks(static_cast<size_t>(K) * kN);
    uint64_t seed = 12345;
    for (auto& m : masks) {
        seed = seed * 6364136223846793005ULL + 1442695040888963407ULL;
        m = seed;
    }
    std::vector<uint64_t> outA(kN), outB(kN);

    const auto t0 = Clock::now();
    naiveMask(masks.data(), K, thr, outA.data(), kN);
    const double naiveMs = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();

    const auto t1 = Clock::now();
    bitslicedMask(masks.data(), K, thr, outB.data(), kN, planes);
    const double bsMs = std::chrono::duration<double, std::milli>(Clock::now() - t1).count();

    size_t diff = 0;
    for (int i = 0; i < kN; ++i)
        if (outA[i] != outB[i]) ++diff;

    // 转置布局：t[w*K + k] = masks[k*n + w]
    std::vector<uint64_t> trans(static_cast<size_t>(kN) * K);
    for (int w = 0; w < kN; ++w)
        for (int k = 0; k < K; ++k) trans[static_cast<size_t>(w) * K + k] = masks[static_cast<size_t>(k) * kN + w];
    std::vector<uint64_t> outC(kN);
    const auto t2 = Clock::now();
    bitslicedTransposed(trans.data(), K, thr, outC.data(), kN, planes);
    const double trMs = std::chrono::duration<double, std::milli>(Clock::now() - t2).count();
    size_t diffC = 0;
    for (int i = 0; i < kN; ++i)
        if (outA[i] != outC[i]) ++diffC;

    const double anchors = static_cast<double>(kN) * 64.0;
    std::printf("K=%d 阈值=%d，锚点当量 %.1f 亿\n", K, thr, anchors / 1e8);
    std::printf("  朴素逐锚点：%8.2f ms → %6.2f ns/锚点\n", naiveMs, naiveMs * 1e6 / anchors);
    std::printf("  位切片：    %8.2f ms → %6.2f ns/锚点\n", bsMs, bsMs * 1e6 / anchors);
    std::printf("  位切片+转置：%8.2f ms → %6.2f ns/锚点（不一致 %zu）\n", trMs, trMs * 1e6 / anchors, diffC);
    std::printf("  加速比：位切片 %.2fx，位切片+转置 %.2fx | 校验 %s\n", naiveMs / bsMs, naiveMs / trMs,
                (diff == 0 && diffC == 0) ? "通过" : "失败");
    return 0;
}
