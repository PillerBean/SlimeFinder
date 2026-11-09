// FastMatch.cpp - v3.3.0 AVX2 加速内核实现
//
// 本文件是唯一启用 AVX2 指令集的编译单元（CMake 对本文件单独加 /arch:AVX2 或 -mavx2）。
// 所有入口在运行期由调用方先检查 cpuHasAvx2()；不支持的 CPU 永不调用本文件函数，
// 从而保证整体程序仍可在仅 SSE2 的 CPU 上运行（引擎侧标量回退）。
// v3.3.1 新增：密度 g×g 窗口计数批量内核（任务书 P1-3.1）
// ---------------------------------------------------------------------------
// 朴素做法是“每个锚点、每行、逐字 popcount”，成本 ≈ n*g*(g/64+2) 条标量指令。
// 本内核改为按列聚合（列和法）：
//   1) 逐块（32 列 / 16 列）对 g 行做“竖直位累加”，得到每列的置位数 cs[c]（≤ g）；
//      用 vpsubb/vpsubw 累加 vpcmpeqb 的结果（0xFF = -1，减法即 +1），每行每 32 列
//      只要 4 条向量指令，且不需要 nibble popcount；
//   2) 对 cs 求前缀和 pref（SIMD 8 列 / 迭代），于是窗口计数
//      window(x) = pref[x+g] - pref[x]，每锚点 O(1)（阶段三每 8 个锚点 4 条指令）；
//   3) 锚点按 2048 列分块，工作集（cs 5KB + pref 13KB）常驻 L1/L2。
// 代价模型：列和法 ≈ (n+g)*g 次“每行每列”操作，逐窗口法 ≈ n*g*(g/64+2)；
// 因此锚点远少于窗口边长（n ≪ g）时列和法反而更慢，此时 densityWindowCountsAvx2
// 会自动改走标量实现（见文件末尾的调度）。
#include "core/FastMatch.h"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>

#include <immintrin.h>
#include <intrin.h>

namespace slime {

bool cpuHasAvx2() noexcept
{
    static const bool ok = []() -> bool {
        int regs[4] = { 0, 0, 0, 0 };
        __cpuid(regs, 0);
        if (regs[0] < 7) return false; // 不支持 CPUID.7
        __cpuid(regs, 1);
        const bool osxsave = (regs[2] & (1 << 27)) != 0;
        const bool avx = (regs[2] & (1 << 28)) != 0;
        if (!osxsave || !avx) return false;
        const unsigned long long xcr0 = _xgetbv(0);
        if ((xcr0 & 0x6) != 0x6) return false; // XMM+YMM 状态未启用
        __cpuidex(regs, 7, 0);
        return (regs[1] & (1 << 5)) != 0; // EBX.AVX2
    }();
    return ok;
}

namespace {

// 8 位 → 8×int32 的位展开表（值为 0/1），用于 vertSum 行差分
struct BitsTable {
    alignas(32) int32_t v[256][8];
    BitsTable()
    {
        for (int b = 0; b < 256; ++b) {
            for (int i = 0; i < 8; ++i) v[b][i] = (b >> i) & 1;
        }
    }
};

const BitsTable& bitsTable()
{
    static const BitsTable t;
    return t;
}

} // namespace

void strictRowAvx2(const uint64_t* const* rows, const uint64_t* enf, const uint64_t* grn,
                   int rowCount, int wpr, int x0, int axMin, int axMax, uint64_t colMask,
                   unsigned char* outFlags) noexcept
{
    if (axMax < axMin) return;
    if (rowCount <= 0) {
        const int64_t n = static_cast<int64_t>(axMax) - axMin + 1;
        for (int64_t i = 0; i < n; ++i) outFlags[i] = 1;
        return;
    }
    const __m256i maskVec = _mm256_set1_epi64x(static_cast<long long>(colMask));
    const __m256i sixtyThree = _mm256_set1_epi64x(63);
    const __m256i allOnes = _mm256_set1_epi64x(-1);

    int64_t xa = axMin;
    int64_t out = 0;
    for (; xa + 3 <= axMax; xa += 4, out += 4) {
        const int rx = static_cast<int>(xa - x0);
        const int w = rx >> 6;
        const int s = rx & 63;
        const __m256i sh = _mm256_set_epi64x(s + 3, s + 2, s + 1, s);
        const __m256i shLow = _mm256_and_si256(sh, sixtyThree);
        const __m256i needB = _mm256_cmpgt_epi64(sh, sixtyThree);
        const __m256i hiShB = _mm256_sub_epi64(_mm256_set1_epi64x(64), sh);
        const __m256i hiShC = _mm256_sub_epi64(_mm256_set1_epi64x(128), sh);
        __m256i acc = allOnes;
        for (int rr = 0; rr < rowCount; ++rr) {
            const uint64_t* row = rows[rr];
            const uint64_t a = row[w];
            const uint64_t b = (w + 1 < wpr) ? row[w + 1] : 0;
            const uint64_t c = (w + 2 < wpr) ? row[w + 2] : 0;
            const __m256i A = _mm256_set1_epi64x(static_cast<long long>(a));
            const __m256i B = _mm256_set1_epi64x(static_cast<long long>(b));
            const __m256i C = _mm256_set1_epi64x(static_cast<long long>(c));
            const __m256i low = _mm256_blendv_epi8(_mm256_srlv_epi64(A, shLow),
                                                   _mm256_srlv_epi64(B, shLow), needB);
            const __m256i hi = _mm256_blendv_epi8(_mm256_sllv_epi64(B, hiShB),
                                                  _mm256_sllv_epi64(C, hiShC), needB);
            __m256i seg = _mm256_or_si256(low, hi);
            if (colMask != ~0ULL) seg = _mm256_and_si256(seg, maskVec);
            const __m256i e = _mm256_set1_epi64x(static_cast<long long>(enf[rr]));
            const __m256i g = _mm256_set1_epi64x(static_cast<long long>(grn[rr]));
            acc = _mm256_and_si256(acc, _mm256_cmpeq_epi64(_mm256_and_si256(seg, e), g));
        }
        const int m = _mm256_movemask_epi8(acc);
        for (int i = 0; i < 4; ++i) {
            outFlags[out + i] = (((m >> (8 * i)) & 0xFF) == 0xFF) ? 1 : 0;
        }
    }
    // 尾部（不足 4 个窗口）：标量等价实现
    for (; xa <= axMax; ++xa, ++out) {
        unsigned char ok = 1;
        for (int rr = 0; rr < rowCount; ++rr) {
            const uint64_t* row = rows[rr];
            const int rxx = static_cast<int>(xa - x0);
            const int ww = rxx >> 6;
            const int sh = rxx & 63;
            uint64_t seg = row[ww] >> sh;
            if (sh != 0 && ww + 1 < wpr) seg |= row[ww + 1] << (64 - sh);
            if (colMask != ~0ULL) seg &= colMask;
            if ((seg & enf[rr]) != grn[rr]) {
                ok = 0;
                break;
            }
        }
        outFlags[out] = ok;
    }
}

// ======================= 密度 g×g 窗口计数（P1-3.1） =======================

namespace {

constexpr int64_t kDensityMaxG = 512;                                // 窗口边长上限
constexpr int64_t kDensityTile = 2048;                               // 每块锚点数（32 的倍数）
// 单块需要的列范围 ≤ [32 对齐下取整(t0), 32 对齐上取整(t1+g)] ≤ Tile + MaxG + 32
constexpr int64_t kDensityCsCap = kDensityTile + kDensityMaxG + 64;   // 2624
constexpr int64_t kDensityPrefCap = kDensityCsCap + kDensityMaxG + 8; // 3144

// 取从位 (w*64 + s) 开始的 64 位（越界部分补 0；要求 s ∈ {0,16,32,48}）
inline uint64_t rowBitsAt(const uint64_t* row, int64_t wpr, int64_t w, int s) noexcept
{
    uint64_t v = (w < wpr) ? (row[w] >> s) : 0ULL;             // 低 64-s 位来自 row[w]
    if (s != 0 && (w + 1) < wpr) v |= row[w + 1] << (64 - s);  // 高位补 row[w+1] 的低 s 位
    return v;
}

// 调试开关（A/B 对照用）：SLIME_DENSITY_MODE = "scalar" → 恒走标量实现；
// "avx2" → AVX2 可用时恒走 AVX2 内核（跳过节流用的代价模型）；其它/未设置 → 自动。
int densityModeOverride() noexcept
{
    static const int mode = []() -> int {
        const char* v = std::getenv("SLIME_DENSITY_MODE");
        if (v == nullptr) return 0;
        if (std::strcmp(v, "scalar") == 0) return -1;
        if (std::strcmp(v, "avx2") == 0) return 1;
        return 0;
    }();
    return mode;
}

} // namespace

bool densityWindowCountsScalar(const uint64_t* const* rows, int g, int64_t wpr,
                               int64_t axBegin, int64_t axEnd, uint32_t* out) noexcept
{
    if (rows == nullptr || out == nullptr || g < 1 || g > static_cast<int>(kDensityMaxG) || wpr < 0) return false;
    if (axEnd < axBegin) return true;
    const int64_t totalBits = wpr * 64;
    for (int64_t x = axBegin; x <= axEnd; ++x) {
        uint32_t cnt = 0;
        int64_t c0 = x < 0 ? 0 : x;              // 负列按 0 位处理
        int64_t c1 = x + g;
        if (c1 > totalBits) c1 = totalBits;      // 行尾之外的列按 0 位处理
        for (int j = 0; j < g && c0 < c1; ++j) {
            const uint64_t* row = rows[j];
            int64_t c = c0;
            while (c < c1) {
                const int64_t w = c >> 6;
                const int s = static_cast<int>(c & 63);
                int64_t take = 64 - s;
                if (take > c1 - c) take = c1 - c;
                const uint64_t seg = (row[w] >> s) & (take >= 64 ? ~0ULL : ((1ULL << take) - 1ULL));
                cnt += static_cast<uint32_t>(std::popcount(seg));
                c += take;
            }
        }
        out[x - axBegin] = cnt;
    }
    return true;
}

namespace {

// AVX2 内核（调用方已保证：rows/out 非空、1 ≤ g ≤ 512、wpr ≥ 0、0 ≤ axBegin ≤ axEnd）
void densityWindowCountsAvx2Impl(const uint64_t* const* rows, int g, int64_t wpr,
                                 int64_t axBegin, int64_t axEnd, uint32_t* out) noexcept
{
    const __m256i zero = _mm256_setzero_si256();
    // 字节累加路径（g ≤ 255）：每 32 列一个 256 位累加器，字节 lane k ↔ 列 b+k，掩码 1<<(k&7)。
    // expandBytes 把 vpshufb 的源重排成“字节 k 取 32 位值的第 (k>>3) 个字节”，
    // 这样字节 lane k 的掩码位 (k&7) 恰好对应列 b+k（vpshufb 以 128 位为单位，故两半各排一次）。
    const __m256i expandBytes = _mm256_setr_epi8(
        0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1,
        2, 2, 2, 2, 2, 2, 2, 2, 3, 3, 3, 3, 3, 3, 3, 3);
    const __m256i maskBytes = _mm256_setr_epi8(
        1, 2, 4, 8, 16, 32, 64, static_cast<char>(128),
        1, 2, 4, 8, 16, 32, 64, static_cast<char>(128),
        1, 2, 4, 8, 16, 32, 64, static_cast<char>(128),
        1, 2, 4, 8, 16, 32, 64, static_cast<char>(128));
    // 字累加路径（256 ≤ g ≤ 512）：每 16 列一个 256 位累加器，字 lane k ↔ 列 b+k，掩码 1<<(k&15)
    const __m256i maskWords = _mm256_setr_epi16(
        1, 2, 4, 8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096, 8192, 16384,
        static_cast<short>(32768));

    alignas(32) uint16_t cs[kDensityCsCap];      // 每列置位数（≤ g ≤ 512，uint16 足够）
    alignas(32) uint32_t pref[kDensityPrefCap];  // 列和前缀（单块内 ≤ 2592*512 < 2^32，不会溢出）

    const bool wide = (g > 255);                 // 是否需要 16 位累加器
    for (int64_t t0 = axBegin; t0 <= axEnd; t0 += kDensityTile) {
        const int64_t t1 = (t0 + kDensityTile - 1 <= axEnd) ? (t0 + kDensityTile - 1) : axEnd;
        const int64_t csBase = t0 & ~static_cast<int64_t>(31);        // 32 对齐（axBegin ≥ 0）
        const int64_t csEnd = (t1 + g + 31) & ~static_cast<int64_t>(31);
        const int64_t csLen = csEnd - csBase;                         // ≤ 2048 + 512 + 32

        // ---- 阶段一：列和（竖直方向按位累加） ----
        int64_t idx = 0;
        if (!wide) {
            for (int64_t b = csBase; b < csEnd; b += 32, idx += 32) {
                const int64_t w = b >> 6;
                const int s = static_cast<int>(b & 63);               // 32 对齐 ⇒ 0 或 32
                __m256i acc = zero;
                for (int j = 0; j < g; ++j) {
                    const uint64_t v = rowBitsAt(rows[j], wpr, w, s);
                    const __m256i bcast = _mm256_set1_epi32(static_cast<int32_t>(static_cast<uint32_t>(v)));
                    const __m256i bv = _mm256_shuffle_epi8(bcast, expandBytes);
                    // (bv & mask) == mask ⟺ 该位为 1 → 0xFF，做减法即“该位置 1 则 +1”
                    acc = _mm256_sub_epi8(acc, _mm256_cmpeq_epi8(_mm256_and_si256(bv, maskBytes), maskBytes));
                }
                _mm256_storeu_si256(reinterpret_cast<__m256i*>(cs + idx),
                                    _mm256_cvtepu8_epi16(_mm256_castsi256_si128(acc)));
                _mm256_storeu_si256(reinterpret_cast<__m256i*>(cs + idx + 16),
                                    _mm256_cvtepu8_epi16(_mm256_extracti128_si256(acc, 1)));
            }
        } else {
            for (int64_t b = csBase; b < csEnd; b += 16, idx += 16) {
                const int64_t w = b >> 6;
                const int s = static_cast<int>(b & 63);               // 16 对齐 ⇒ 0/16/32/48
                __m256i acc = zero;
                for (int j = 0; j < g; ++j) {
                    const uint64_t v = rowBitsAt(rows[j], wpr, w, s);
                    const __m256i bv = _mm256_set1_epi16(static_cast<int16_t>(static_cast<uint16_t>(v)));
                    acc = _mm256_sub_epi16(acc, _mm256_cmpeq_epi16(_mm256_and_si256(bv, maskWords), maskWords));
                }
                _mm256_storeu_si256(reinterpret_cast<__m256i*>(cs + idx), acc);
            }
        }

        // ---- 阶段二：前缀和 pref[i] = Σ_{k<i} cs[k]（i = 0..csLen） ----
        pref[0] = 0;
        {
            uint32_t run = 0;
            int64_t i = 0;
            for (; i + 8 <= csLen; i += 8) {
                const __m128i v = _mm_loadu_si128(reinterpret_cast<const __m128i*>(cs + i));
                __m256i x = _mm256_cvtepu16_epi32(v);                 // 8 × uint32
                x = _mm256_add_epi32(x, _mm256_slli_si256(x, 4));     // 128 位 lane 内前缀
                x = _mm256_add_epi32(x, _mm256_slli_si256(x, 8));
                // 补上“低 128 位总和”给高 128 位（跨 lane 进位）
                const __m256i lowLaneAll = _mm256_permute2x128_si256(x, x, 0x00);
                const __m256i carry = _mm256_shuffle_epi32(lowLaneAll, 0xFF);
                x = _mm256_add_epi32(x, _mm256_blend_epi32(zero, carry, 0xF0));
                x = _mm256_add_epi32(x, _mm256_set1_epi32(static_cast<int32_t>(run)));
                _mm256_storeu_si256(reinterpret_cast<__m256i*>(pref + i + 1), x);
                run = static_cast<uint32_t>(_mm256_extract_epi32(x, 7));
            }
            for (; i < csLen; ++i) {
                run += cs[i];
                pref[i + 1] = run;
            }
        }

        // ---- 阶段三：窗口计数 = pref[x+g] - pref[x]（8 锚点 / 迭代） ----
        const int64_t a0 = t0 - csBase;
        const int64_t n = t1 - t0 + 1;
        int64_t i = 0;
        for (; i + 8 <= n; i += 8) {
            const int64_t a = a0 + i;
            const __m256i hi = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(pref + a + g));
            const __m256i lo = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(pref + a));
            _mm256_storeu_si256(reinterpret_cast<__m256i*>(out + (t0 + i - axBegin)),
                                _mm256_sub_epi32(hi, lo));
        }
        for (; i < n; ++i) {
            const int64_t a = a0 + i;
            out[t0 + i - axBegin] = pref[a + g] - pref[a];
        }
    }
}

} // namespace

bool densityWindowCountsAvx2(const uint64_t* const* rows, int g, int64_t wordsPerRow,
                             int64_t axBegin, int64_t axEnd, uint32_t* out) noexcept
{
    if (rows == nullptr || out == nullptr || g < 1 || g > static_cast<int>(kDensityMaxG) || wordsPerRow < 0) {
        return false;
    }
    if (axEnd < axBegin) return true;
    // 代价模型：列和法 ≈ (n+g)*g 次“每行每列”操作；逐窗口法 ≈ n*g*(g/64+2) 条标量指令。
    // 锚点数远小于窗口边长时列和法为 g 列做无用功，此时改走标量更快。
    const double n = static_cast<double>(axEnd - axBegin + 1);
    const double gd = static_cast<double>(g);
    const double colCost = (n + gd) * gd;
    const double winCost = n * gd * (gd / 64.0 + 2.0);
    const int mode = densityModeOverride();
    if (mode < 0) return densityWindowCountsScalar(rows, g, wordsPerRow, axBegin, axEnd, out);
    if (!cpuHasAvx2() || axBegin < 0) {
        return densityWindowCountsScalar(rows, g, wordsPerRow, axBegin, axEnd, out);
    }
    if (mode == 0 && colCost > winCost) {
        return densityWindowCountsScalar(rows, g, wordsPerRow, axBegin, axEnd, out);
    }
    densityWindowCountsAvx2Impl(rows, g, wordsPerRow, axBegin, axEnd, out);
    return true;
}

void vertSumDiffAvx2(int* vertSum, const uint64_t* newRow, const uint64_t* oldRow, int wpr, int w,
                     uint64_t tailMask) noexcept
{
    (void)tailMask;
    const BitsTable& tbl = bitsTable();
    for (int wi = 0; wi < wpr; ++wi) {
        const uint64_t nw = newRow ? newRow[wi] : 0ULL;
        const uint64_t ow = oldRow ? oldRow[wi] : 0ULL;
        int* base = vertSum + static_cast<int64_t>(wi) * 64;
        for (int byte = 0; byte < 8; ++byte) {
            const int nb = static_cast<int>((nw >> (byte * 8)) & 0xFFULL);
            const int ob = static_cast<int>((ow >> (byte * 8)) & 0xFFULL);
            __m256i cur = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(base + byte * 8));
            const __m256i add = _mm256_load_si256(reinterpret_cast<const __m256i*>(tbl.v[nb]));
            const __m256i sub = _mm256_load_si256(reinterpret_cast<const __m256i*>(tbl.v[ob]));
            cur = _mm256_add_epi32(cur, _mm256_sub_epi32(add, sub));
            _mm256_storeu_si256(reinterpret_cast<__m256i*>(base + byte * 8), cur);
        }
        (void)w;
    }
}


// v3.6.18：列垂直和 → 窗口计数（SIMD 批量）。逐 8 锚点：
//   cnt[i] = pref8[i+K] - pref8[i]，用两次 256 位减法 + 一次 64 位减法（K 不是 8 的倍数时补齐）。
bool densityWindowSumsAvx2(const int32_t* sums, int64_t count, int K, int32_t* out) noexcept
{
    if (!sums || !out || count <= 0 || K <= 0) return false;
    // 直接按 K 做「K 次错位加载求和」成本 K/8 次向量加；对 K≤16 更划算，
    // 大于 16 时用前缀和（标量建前缀 + 向量相减）。
    if (K <= 16) {
        int64_t i = 0;
        for (; i + 8 <= count; i += 8) {
            __m256i acc = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(sums + i));
            for (int c = 1; c < K; ++c) {
                acc = _mm256_add_epi32(acc,
                                       _mm256_loadu_si256(reinterpret_cast<const __m256i*>(sums + i + c)));
            }
            _mm256_storeu_si256(reinterpret_cast<__m256i*>(out + i), acc);
        }
        for (; i < count; ++i) {
            int32_t s = 0;
            for (int c = 0; c < K; ++c) s += sums[i + c];
            out[i] = s;
        }
        return true;
    }
    // 大 K：前缀和
    std::vector<int32_t> pref(static_cast<size_t>(count) + static_cast<size_t>(K) + 1, 0);
    for (int64_t i = 0; i < count + K; ++i) pref[static_cast<size_t>(i) + 1] = pref[static_cast<size_t>(i)] + sums[i];
    int64_t i = 0;
    for (; i + 8 <= count; i += 8) {
        const __m256i a = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(pref.data() + i + K));
        const __m256i b = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(pref.data() + i));
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(out + i), _mm256_sub_epi32(a, b));
    }
    for (; i < count; ++i) out[i] = pref[static_cast<size_t>(i) + static_cast<size_t>(K)] - pref[static_cast<size_t>(i)];
    return true;
}

// v3.7.7：窗口计数 → 命中位掩码（不落中间计数数组）
bool densityHitMaskAvx2(const int32_t* sums, int64_t count, int K, int32_t threshold,
                        uint64_t* outWords, int64_t outWordCount) noexcept
{
    if (!sums || !outWords || count <= 0 || K <= 0 || outWordCount * 64 < count) return false;
    for (int64_t w = 0; w < outWordCount; ++w) outWords[w] = 0;
    const __m256i thresh = _mm256_set1_epi32(threshold);
    int64_t i = 0;
    if (K <= 16) {
        for (; i + 8 <= count; i += 8) {
            __m256i acc = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(sums + i));
            for (int c = 1; c < K; ++c) {
                acc = _mm256_add_epi32(acc,
                                       _mm256_loadu_si256(reinterpret_cast<const __m256i*>(sums + i + c)));
            }
            const __m256i hit = _mm256_cmpgt_epi32(acc, thresh);
            const int bits = _mm256_movemask_ps(_mm256_castsi256_ps(hit));
            if (bits) {
                const int64_t w = i >> 6;
                outWords[w] |= static_cast<uint64_t>(static_cast<unsigned>(bits)) << (i & 63);
            }
        }
    } else {
        std::vector<int32_t> pref(static_cast<size_t>(count) + static_cast<size_t>(K) + 1, 0);
        for (int64_t j = 0; j < count + K; ++j)
            pref[static_cast<size_t>(j) + 1] = pref[static_cast<size_t>(j)] + sums[j];
        for (; i + 8 <= count; i += 8) {
            const __m256i a = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(pref.data() + i + K));
            const __m256i b = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(pref.data() + i));
            const __m256i cnt = _mm256_sub_epi32(a, b);
            const __m256i hit = _mm256_cmpgt_epi32(cnt, thresh);
            const int bits = _mm256_movemask_ps(_mm256_castsi256_ps(hit));
            if (bits) {
                const int64_t w = i >> 6;
                outWords[w] |= static_cast<uint64_t>(static_cast<unsigned>(bits)) << (i & 63);
            }
        }
    }
    // 尾部（不足 8 个）用标量补齐
    for (; i < count; ++i) {
        int32_t cnt = 0;
        for (int c = 0; c < K; ++c) cnt += sums[i + c];
        if (cnt > threshold) outWords[i >> 6] |= 1ULL << (i & 63);
    }
    return true;
}

} // namespace slime
