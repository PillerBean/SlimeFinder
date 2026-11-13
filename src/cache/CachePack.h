// CachePack.h - v3.12.3：新的 zstd 分块缓存 pack 格式（仅 Bedrock）
//
// 设计目标：
//   * 目录 + 多 pack（index.json 由后续 CacheStore 负责）；
//   * 每 pack 原始约 8 MiB（8192×8192 区块），block 原始 128 KiB；
//   * 每个 block 独立 zstd 压缩，压缩不划算时存 raw；
//   * pack 之间/块之间不共享压缩字典，便于后续按块下载、随机读取。
//
// pack 内坐标使用**区块坐标（chunk）**，范围必须按 1024 区块对齐；
// 位图行主序、每行按 64 位字对齐之外的尾位补 0。
#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace slime::cache {

constexpr int kPackBlockChunks = 1024;   // 内部压缩 block：1024 x 1024 区块 = 128 KiB
constexpr int kPackTileChunks = 8192;    // v3.12.3 统一 pack 覆盖：8192 x 8192 区块
constexpr int kPackMaxBlocks = 64;       // 8192×8192 = 64 个 1024 block

struct PackRect {
    int32_t minX = 0;
    int32_t maxX = -1;
    int32_t minZ = 0;
    int32_t maxZ = -1;
    int64_t width() const noexcept { return static_cast<int64_t>(maxX) - minX + 1; }
    int64_t height() const noexcept { return static_cast<int64_t>(maxZ) - minZ + 1; }
    int64_t area() const noexcept { return width() * height(); }
    bool valid() const noexcept { return width() > 0 && height() > 0; }
};

struct PackInfo {
    PackRect rect;
    uint32_t blockCols = kPackBlockChunks;
    uint32_t blockRows = kPackBlockChunks;
    uint32_t blockCount = 0;
    uint64_t rawBytes = 0;
    uint64_t compressedBytes = 0;
};

// 写出一个 pack。bitmap 为整个 rect 的位图（行主序，1=史莱姆区块），
// 长度必须 >= ceil(area/8)。rect 的宽高必须是 1024 的整数倍。
bool writePack(const std::filesystem::path& path, const PackRect& rect,
               const uint8_t* bitmap, std::size_t bitmapBytes, std::string* err = nullptr);

// 读取 pack；out 为还原后的行主序位图，长度 ceil(area/8)。
bool readPack(const std::filesystem::path& path, PackInfo& info, std::vector<uint8_t>& out,
              std::string* err = nullptr);

// 只读 header + block 表，不解压数据（后续索引/校验用）。
bool peekPack(const std::filesystem::path& path, PackInfo& info, std::string* err = nullptr);

} // namespace slime::cache
