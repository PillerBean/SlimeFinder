// FastMatch.h - v3.3.0 AVX2 加速内核（严格匹配位并行 / 密度滑动窗口）
//
// 说明：实现位于独立编译单元 FastMatch.cpp（仅该文件启用 /arch:AVX2 或 -mavx2），
// 所有函数在运行时先由 cpuHasAvx2() 检查；不可用时调用方走标量实现（完全等价）。
// 头文件不暴露任何 SIMD 类型，可被 Qt 无关的核心引擎安全包含。
#pragma once

#include <cstdint>

namespace slime {

// 运行时 AVX2 检测（OSXSAVE + XCR0 YMM + CPUID.7.EBX.AVX2），结果缓存
bool cpuHasAvx2() noexcept;

// ---- 严格匹配：一次判定 4 个连续窗口（x 方向，xa..xa+3） ----
// rows     : 位图各行首地址（长度 rowCount，元素为 grid.rowBits(az+rr)）
// enf/grn  : 图案各行掩码（长度 rowCount）
// rowCount : 图案行数（≥1）
// wpr      : 位图每行 64 位字数
// rxBase   : 窗口 0 的行内相对位偏移（xa - x0），要求 rxBase+3 不越行
// colMask  : 低 cols 位掩码（cols<64 时用于裁剪；cols==64 传 ~0ULL）
// 返回     : 位 i（0..3）置 1 表示第 i 个窗口匹配
// ---- 严格匹配：整行一次处理 ----
// 对锚点行 az 的全部窗口（x ∈ [axMin, axMax]）逐窗口输出标记（1=匹配，0=不匹配）。
// rows 为 az..az+rowCount-1 各行位图首地址；outFlags 长度须 ≥ axMax-axMin+1。
// 相比逐块调用避免重复构造常量与行指针，实测显著更快。
// 适用：图案行多且每行约束少（行通过率高）时优于标量早退；引擎按图案特征自适应选择。
void strictRowAvx2(const uint64_t* const* rows, const uint64_t* enf, const uint64_t* grn,
                   int rowCount, int wpr, int x0, int axMin, int axMax, uint64_t colMask,
                   unsigned char* outFlags) noexcept;

// ---- 密度：vertSum 行差分（vertSum[rx] += bit(newRow,rx) - bit(oldRow,rx)） ----
// oldRow 可为 nullptr（只加不减；用于块首行）
void vertSumDiffAvx2(int* vertSum, const uint64_t* newRow, const uint64_t* oldRow, int wpr,
                     int w, uint64_t tailMask) noexcept;

// ---- 密度：g×g 窗口计数批量内核（任务书 P1-3.1） ----
// 用途：位图已就绪、需要对一批锚点求 g×g 窗口内置位数（无滑动资格的路径：
//       智能范围 / 提前停止 / 分层 / 采样估算等）。
// rows        : 位图各行首地址数组，长度 = g（窗口行数 = 列数 = g；rows[j] 为第 j 行，
//               每行 wordsPerRow 个 uint64，位序 bit i 对应列 i）
// g           : 窗口边长，1 ≤ g ≤ 512
// wordsPerRow : 每行 64 位字数（行内有效列 = wordsPerRow*64；越界列按 0 位处理，
//               内核不会越界读，因此末尾锚点的窗口允许“探出”行尾）
// axBegin/axEnd: 锚点列下标范围（相对列 0，含端点）；axEnd < axBegin 时无输出
// out         : 输出数组，长度 ≥ axEnd-axBegin+1，out[x - axBegin] = 窗口内 1 的个数
//
// 返回值：true = 全部结果已写入 out；false = 参数非法（rows/out 为 nullptr、
//         g ∉ [1,512]、wordsPerRow < 0），此时不写 out。
// 内部：cpuHasAvx2() 为真、axBegin ≥ 0 且规模合适时走 AVX2 内核（列和 + 前缀和，
//       见 FastMatch.cpp 顶部说明），否则自动走等价的标量实现——两条路径结果
//       逐位一致（kernel_selftest 用朴素逐窗口实现全量比对）。
// 调试开关（环境变量，默认自动）：SLIME_DENSITY_MODE = "scalar" 强制标量实现、
//       "avx2" 强制 AVX2 内核（跳过节流用的代价模型）；其它值 = 自动。用于 A/B。
// v3.6.18：把「列垂直和」滑窗换成 SIMD 批量（一次 8/16 个锚点）——
//   输入 sums[0..count+K-1]（每列的 K 行垂直和），输出 out[i] = sum(sums[i..i+K-1])。
//   语义上等价于标量滑窗，但把「每锚点一次加一次减」换成「每 8 个锚点 2 次向量加减」。
//   返回 false 表示参数非法（调用方回退标量路径）。
bool densityWindowSumsAvx2(const int32_t* sums, int64_t count, int K, int32_t* out) noexcept;

// v3.7.7：只返回「窗口计数 > threshold」的命中位掩码（每 64 个锚点一个 uint64）。
//   与 densityWindowSumsAvx2 相比不再写/读 count*4 字节的中间数组（1 亿锚点就是 400MB，
//   实测这层带宽是匹配阶段的主要成本），命中稀疏时后续只需遍历置位。
//   返回 false 表示参数非法（调用方回退到计数数组路径）。
bool densityHitMaskAvx2(const int32_t* sums, int64_t count, int K, int32_t threshold,
                        uint64_t* outWords, int64_t outWordCount) noexcept;

bool densityWindowCountsAvx2(const uint64_t* const* rows, int g, int64_t wordsPerRow,
                             int64_t axBegin, int64_t axEnd, uint32_t* out) noexcept;

// 纯标量参考实现（朴素：每窗口逐行 popcount），签名与语义同上，恒返回 true
//（参数非法时返回 false）。
// 注意：本函数位于启用 /arch:AVX2 的编译单元，MSVC 可能对其自动向量化出 AVX2 指令，
// 因此它只适合 AVX2 机器上的对照/回退，不要当作无 AVX2 老机器的运行期回退。
bool densityWindowCountsScalar(const uint64_t* const* rows, int g, int64_t wordsPerRow,
                               int64_t axBegin, int64_t axEnd, uint32_t* out) noexcept;

} // namespace slime
