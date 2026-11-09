// FastMt.h - AVX2 四通道批量 MT19937 内核（基岩版史莱姆区块判定）
//
// 设计约束：
//   * 本头文件不含任何 SIMD 类型、不需要 /arch:AVX2，可被项目任意编译单元安全包含
//     （Qt 无关，仅依赖 <cstdint>）。
//   * 实现位于独立编译单元 src/core/FastMt.cpp（CMake 对该文件单独启用
//     /arch:AVX2 或 -mavx2；未启用时该文件自动编译为标量回退）。
//   * 所有 AVX2 入口在运行期先经 cpuHasAvx2() 检查；CPU 不支持时内部自动走标量，
//     因此这些函数可以无条件调用（结果与调用标量版本完全一致）。
//
// 语义（与 src/core/SlimeEngine.cpp 的 isSlimeBedrockFast / bedrockFirstDraw 逐位一致）：
//   seed = low32(cx * 0x1F1F1F1F) ^ uint32(cz)
//   以 seed 作为 mt[0] 完成 624 步 MT19937 初始化 → twist → 取首次加扰输出 v
//   区块是史莱姆 ⟺ v % 10 == 0
//
// 环境变量（调试用）：
//   SLIME_FORCE_SCALAR=1  强制所有批量/扫描入口走标量路径（A/B 对照，结果必须一致）
//   SLIME_MT_TABLE=1      允许种子查表路径（默认关闭；见 bedrockChunkSeedTable）
#pragma once

#include <cstdint>

namespace slime {
namespace fastmt {

// ---------------------------------------------------------------------------
// 运行时 CPU 能力检测
// ---------------------------------------------------------------------------

// AVX2 可用性（CPUID.0 叶 ≥7 + CPUID.1 OSXSAVE/AVX + XCR0 YMM + CPUID.7.EBX[5]），
// 结果缓存；线程安全，可在任意位置调用（包括静态初始化期之后的任何时候）。
bool cpuHasAvx2() noexcept;

// ---------------------------------------------------------------------------
// 批量判定（每项 out[j] = 0/1）
// ---------------------------------------------------------------------------

// 4 区块批量：AVX2 单链（4 lane = 4 区块）。不可用时内部自动回退标量。
void bedrockSlimeBatch4(const int32_t* cx, const uint32_t* czu, uint8_t* outSlime4) noexcept;

// 8 区块批量：AVX2 双链交织（2×4 lane，隐藏乘法延迟）。不可用时回退标量。
void bedrockSlimeBatch8(const int32_t* cx, const uint32_t* czu, uint8_t* outSlime8) noexcept;

// 16 区块批量：AVX2 四链交织（4×4 lane）。不可用时回退标量。
void bedrockSlimeBatch16(const int32_t* cx, const uint32_t* czu, uint8_t* outSlime16) noexcept;

// 32 区块批量：AVX2 八链交织（8×4 lane，吞吐最高，扫描内核主体）。不可用时回退标量。
void bedrockSlimeBatch32(const int32_t* cx, const uint32_t* czu, uint8_t* outSlime32) noexcept;

// ---------------------------------------------------------------------------
// 标量参考实现（与 SlimeEngine.cpp 完全同构：完整 624 步 init + 完整 twist）
// ---------------------------------------------------------------------------

// 区块种子：low32(cx * 0x1F1F1F1F) ^ czu
uint32_t bedrockChunkSeedScalar(int32_t cx, uint32_t czu) noexcept;

// 首次加扰输出（完整 624 步 init + 完整 twist + temper），与引擎逐位一致。
// 注意：在启用 /arch:AVX2 的构建里，MSVC 会对本函数的 twist 循环自动向量化出
// AVX2 指令（已用 dumpbin 核对），因此它是“对照测试用”的参考实现，不要把它当作
// 无 AVX2 老 CPU 的运行期回退（那种场景请使用未启用 /arch:AVX2 的构建）。
uint32_t bedrockFirstDrawScalar(uint32_t seed) noexcept;

// 首次加扰输出（代数等价捷径：只推进 init 链上的 mt[0]/mt[1]/mt[397]，
// 见 FastMt.cpp 顶部证明）。结果与 bedrockFirstDrawScalar 逐位相同，仅更快。
uint32_t bedrockFirstDrawScalarShortcut(uint32_t seed) noexcept;

// 4 区块批量（标量，完整参考实现：624 步 init + 完整 twist）。
// 用途：一致性对照与非 AVX2 平台的基准基线。
// 注意：在启用 /arch:AVX2 的构建里 MSVC 会对它的 twist 循环自动向量化出 AVX2 指令，
// 因此它不适合被“无 AVX2 老 CPU”在运行期调用；那种场景请使用未启用 /arch:AVX2 的
// 构建（batch*/scan 的运行期回退走的是不含 AVX 指令的标量捷径路径）。
void bedrockSlimeBatch4Scalar(const int32_t* cx, const uint32_t* czu, uint8_t* outSlime4) noexcept;

// 4 区块批量（AVX2，字面照搬“624 步 init + 完整 twist”的 4 lane 版本，
// 仅用于验证上面的代数捷径与 AVX2 通道机制；结果与 bedrockSlimeBatch4 相同）
void bedrockSlimeBatch4FullTwist(const int32_t* cx, const uint32_t* czu, uint8_t* outSlime4) noexcept;

// ---------------------------------------------------------------------------
// 连续扫描
// ---------------------------------------------------------------------------

// 固定 cz，对 cx = cx0, cx0+1, ..., cx0+count-1 连续 count 个区块判定，
// 结果（0/1）写入 out[0..count-1]。
//   count        : 任意 ≥0（0 直接返回，out 可为空）
//   cx 溢出      : 按 int32 回绕处理（与引擎 static_cast<int32_t>(x) 语义一致）
//   startAlignedTo4 : 调用方声明 cx0 是 4 的倍数（true 时省去头部补齐；
//                     传 false 或声明不实时本函数会自动用标量补齐头部，结果不变）
// AVX2 可用时按 32/16/4 区块批量执行（标量处理头部与尾部），否则纯标量。
void bedrockSlimeScan(int32_t cx0, int32_t cz, int32_t count, uint8_t* out, bool startAlignedTo4) noexcept;

// 纯标量版扫描（与 bedrockSlimeScan 结果逐位一致；用于对照/非 AVX2 平台）。
// 该函数及其整个调用链不含任何 AVX 指令（dumpbin 核对），可安全用于无 AVX2 的 CPU。
void bedrockSlimeScanScalar(int32_t cx0, int32_t cz, int32_t count, uint8_t* out, bool startAlignedTo4) noexcept;

// ---------------------------------------------------------------------------
// 跨步采样扫描（采样估算模式：cx = cx0 + i*step，cz 固定）
// ---------------------------------------------------------------------------

// 固定 cz，对 cx = cx0 + i*step（i = 0..count-1）逐点判定，结果（0/1）写入 out[0..count-1]。
//   step  : ≥1（传 ≤0 时按 1 处理）；cx 按 32 位回绕（与 static_cast<int32_t> 语义一致）
//   count : 任意 ≥0（0 直接返回，out 可为空）
// AVX2 可用时一次处理 4/32 个采样点（32 = 8 链 × 4 lane 交织，隐藏 vpmulld 延迟），
// 不可用时自动回退标量；SLIME_FORCE_SCALAR=1 亦强制标量。
// 与逐点调用 bedrockFirstDrawScalar（等价 bedrockSlimeScanScalar）结果逐位一致。
void bedrockSlimeScanStrided(int32_t cx0, uint32_t czu, int32_t count, int32_t step, uint8_t* out) noexcept;

// 纯标量对照版（结果与 bedrockSlimeScanStrided 逐位一致）。
// 种子生成方式受 bedrockSeedTableEnabled() 影响（开启则查表），两条分支结果一致。
void bedrockSlimeScanStridedScalar(int32_t cx0, uint32_t czu, int32_t count, int32_t step, uint8_t* out) noexcept;

// ---------------------------------------------------------------------------
// 区块种子查表（任务书 P0-2.2）
// ---------------------------------------------------------------------------

// low32(cx * 0x1F1F1F1F) ^ czu 的查表实现：
//   T[lo] = low32(lo * 0x1F1F1F1F)，lo = cx & 0xFFFF（65536 项 × 4B = 256KB，
//   首次调用时一次性构建）；
//   low32(cx*K) = T[lo] + (cx >> 16) * low32(K << 16)   （uint32 回绕加法）
// 结果与 bedrockChunkSeedScalar 逐位一致（kernel_selftest 用 20 万随机 + 边界值验证）。
// 该函数始终走查表（它本身就是“查表版”），不受开关影响。
uint32_t bedrockChunkSeedTable(int32_t cx, uint32_t czu) noexcept;

// 查表开关：只影响“可选走查表”的路径——bedrockSlimeScanStridedScalar 的种子生成
// 与 bedrockSlimeScanScalarTable 的种子生成。默认关闭（保持既有路径字节级不变）；
// 首次调用时读取环境变量 SLIME_MT_TABLE（非空且首字符非 '0' 即视为开启），
// 之后可用本函数在运行期覆盖。
// 注意：开关只改变种子的计算方式，判定结果逐位一致；AVX2 批量内核不读该开关
//（其种子计算本身只占极小的常数开销）。
void setBedrockSeedTableEnabled(bool enabled) noexcept;
bool bedrockSeedTableEnabled() noexcept;

// 标量扫描的“查表对照”实现（A/B 用）：
//   开关开启 → 每次种子用 bedrockChunkSeedTable
//   开关关闭 → 与 bedrockSlimeScanScalar 完全同构（同一份模板代码，仅种子函数不同），
//              便于在同一个二进制里做公平对照。
// 无论开关状态，结果都与 bedrockSlimeScanScalar 逐位一致。
// 注意：查表路径的建表循环用 #pragma loop(no_vector) 保证不含 AVX 指令，但本函数
// 位于启用 /arch:AVX2 的编译单元中，编译器仍可能对其它部分使用 AVX2；无 AVX2 的
// 老机器请使用未启用 /arch:AVX2 的构建（与下面对 bedrockSlimeBatch4Scalar 的说明一致）。
void bedrockSlimeScanScalarTable(int32_t cx0, int32_t cz, int32_t count, uint8_t* out, bool startAlignedTo4) noexcept;

} // namespace fastmt
} // namespace slime
