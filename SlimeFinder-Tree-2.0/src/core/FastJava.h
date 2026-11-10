// FastJava.h - v3.6.16 Java 版判定 AVX2 内核（16 lane）
//
// 与 FastMatch/FastMt 相同的隔离策略：实现放在独立编译单元（仅该文件 /arch:AVX2），
// 调用方必须先问 fastjava::available()；不可用时走引擎原有的标量路径，结果逐位一致。
#pragma once

#include <cstdint>

namespace fastjava {

// 运行期 AVX2 检测（与 FastMatch::cpuHasAvx2 同源判断，结果缓存）
bool available() noexcept;

// 生成 16 个连续区块的命中位掩码（bit i = 区块 cx0+i 是史莱姆区块）
// rowBase 由 javaRowBase(worldSeed, cz) 给出（等于 worldSeed + z 项）
uint32_t slimeMask16(uint64_t rowBase, int32_t cx0) noexcept;

// v3.6.16：一次处理 64 个连续区块（内部 4×16 lane，常量提到循环外）——
//   引擎按 64 位字写回位图，用这个入口可以少 3 次函数调用与常量重建。
uint64_t slimeMask64(uint64_t rowBase, int32_t cx0) noexcept;

// v3.6.20：整行批量入口 —— 常量在行内只建一次，省掉「每 64 区块一次函数调用 + 常量重建」。
//   写 outWords[0..ceil(count/64)-1]，尾字只写有效位（其余为 0）。
void scanRowJava(uint64_t rowBase, int32_t cx0, int32_t count, uint64_t* outWords) noexcept;

// v3.6.21：整字批处理 —— 只处理「完整 64 区块字」，常量在整批内只建一次，
//   结果直接 OR 进位图，省掉「每字一次函数调用 + 常量重建」和临时缓冲。
//   调用方负责保证 wordCount 个字都是完整的（不含尾块）。
void scanWordsOr(uint64_t rowBase, int32_t cx0, int32_t wordCount, uint64_t* rowW) noexcept;

} // namespace fastjava
