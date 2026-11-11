#include "cache/CachePack.h"

#include <zstd.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>
#include <limits>
#include <system_error>

namespace slime::cache {
namespace {

constexpr char kMagic[8] = { 'S', 'L', 'M', '3', 'P', 'K', 'G', '\0' };
constexpr uint32_t kVersion = 3;
constexpr uint8_t kCodecRaw = 0;
constexpr uint8_t kCodecZstd = 1;
constexpr size_t kBlockBytes = static_cast<size_t>(kPackBlockChunks) * kPackBlockChunks / 8;
constexpr size_t kHeaderBytes = 64;
constexpr size_t kRecordBytes = 16;

struct BlockRecord {
    uint32_t rawBytes = static_cast<uint32_t>(kBlockBytes);
    uint32_t compressedBytes = 0;
    uint8_t codec = kCodecRaw;
    uint32_t checksum = 0;
};

template <typename T>
void appendPod(std::vector<uint8_t>& out, const T& v)
{
    const auto* p = reinterpret_cast<const uint8_t*>(&v);
    out.insert(out.end(), p, p + sizeof(T));
}

bool readPod(std::ifstream& in, void* dst, size_t n)
{
    return static_cast<bool>(in.read(reinterpret_cast<char*>(dst), static_cast<std::streamsize>(n)));
}

uint32_t checksum32(const uint8_t* data, size_t n) noexcept
{
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < n; ++i) {
        h ^= data[i];
        h *= 16777619u;
    }
    return h;
}

bool alignedRect(const PackRect& r) noexcept
{
    return r.valid() && r.width() % kPackBlockChunks == 0 && r.height() % kPackBlockChunks == 0;
}

std::filesystem::path tempPackPath(const std::filesystem::path& path)
{
    auto tmp = path;
    tmp += L".tmp";
    return tmp;
}

void setBit(std::vector<uint8_t>& bits, size_t index, bool value)
{
    if (!value) return;
    bits[index / 8] |= static_cast<uint8_t>(1u << (index % 8));
}

bool getBit(const std::vector<uint8_t>& bits, size_t index)
{
    return (bits[index / 8] >> (index % 8)) & 1u;
}

} // namespace

bool writePack(const std::filesystem::path& path, const PackRect& rect, const uint8_t* bitmap,
               size_t bitmapBytes, std::string* err)
{
    const auto fail = [&](const std::string& msg) {
        if (err) *err = msg;
        return false;
    };
    if (!bitmap) return fail("pack 位图为空");
    if (!alignedRect(rect)) return fail("pack 范围必须为 1024 区块对齐且非空");
    const uint64_t blocksX = static_cast<uint64_t>(rect.width() / kPackBlockChunks);
    const uint64_t blocksZ = static_cast<uint64_t>(rect.height() / kPackBlockChunks);
    const uint64_t blockCount = blocksX * blocksZ;
    if (blockCount == 0 || blockCount > kPackMaxBlocks) return fail("pack 区块数超出 1..16");
    const size_t needed = static_cast<size_t>((rect.area() + 7) / 8);
    if (needed > bitmapBytes) return fail("pack 位图长度不足");

    std::vector<std::vector<uint8_t>> compressedBlocks;
    compressedBlocks.reserve(static_cast<size_t>(blockCount));
    std::vector<BlockRecord> records;
    records.reserve(static_cast<size_t>(blockCount));
    std::vector<uint8_t> raw(kBlockBytes);
    uint64_t rawTotal = 0;
    uint64_t compTotal = 0;

    for (uint64_t bz = 0; bz < blocksZ; ++bz) {
        for (uint64_t bx = 0; bx < blocksX; ++bx) {
            std::fill(raw.begin(), raw.end(), 0);
            for (int row = 0; row < kPackBlockChunks; ++row) {
                const int64_t zRel = static_cast<int64_t>(bz) * kPackBlockChunks + row;
                const int64_t xRelBase = static_cast<int64_t>(bx) * kPackBlockChunks;
                for (int col = 0; col < kPackBlockChunks; ++col) {
                    const size_t src = static_cast<size_t>((zRel * rect.width()) + xRelBase + col);
                    if ((bitmap[src / 8] >> (src % 8)) & 1u) {
                        setBit(raw, static_cast<size_t>(row) * kPackBlockChunks + col, true);
                    }
                }
            }

            std::vector<uint8_t> comp(ZSTD_compressBound(raw.size()));
            const size_t z = ZSTD_compress(comp.data(), comp.size(), raw.data(), raw.size(), 9);
            BlockRecord rec;
            rec.rawBytes = static_cast<uint32_t>(raw.size());
            if (ZSTD_isError(z) || z >= raw.size()) {
                compressedBlocks.emplace_back(raw);
                rec.codec = kCodecRaw;
                rec.compressedBytes = rec.rawBytes;
            } else {
                comp.resize(z);
                compressedBlocks.push_back(std::move(comp));
                rec.codec = kCodecZstd;
                rec.compressedBytes = static_cast<uint32_t>(compressedBlocks.back().size());
            }
            rec.checksum = checksum32(raw.data(), raw.size());
            rawTotal += rec.rawBytes;
            compTotal += rec.compressedBytes;
            records.push_back(rec);
        }
    }

    const auto tmp = tempPackPath(path);
    std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
    if (!out) return fail("无法创建 pack 临时文件");

    std::vector<uint8_t> header;
    header.insert(header.end(), kMagic, kMagic + sizeof(kMagic));
    appendPod(header, kVersion);
    appendPod(header, rect.minX);
    appendPod(header, rect.maxX);
    appendPod(header, rect.minZ);
    appendPod(header, rect.maxZ);
    appendPod(header, static_cast<uint32_t>(kPackBlockChunks));
    appendPod(header, static_cast<uint32_t>(kPackBlockChunks));
    appendPod(header, static_cast<uint32_t>(blocksX));
    appendPod(header, static_cast<uint32_t>(blocksZ));
    appendPod(header, static_cast<uint32_t>(blockCount));
    appendPod(header, rawTotal);
    appendPod(header, compTotal);
    out.write(reinterpret_cast<const char*>(header.data()), static_cast<std::streamsize>(header.size()));
    for (const auto& r : records) {
        out.write(reinterpret_cast<const char*>(&r.rawBytes), sizeof(r.rawBytes));
        out.write(reinterpret_cast<const char*>(&r.compressedBytes), sizeof(r.compressedBytes));
        out.write(reinterpret_cast<const char*>(&r.codec), sizeof(r.codec));
        const uint8_t pad[3] = { 0, 0, 0 };
        out.write(reinterpret_cast<const char*>(pad), sizeof(pad));
        out.write(reinterpret_cast<const char*>(&r.checksum), sizeof(r.checksum));
    }
    for (const auto& b : compressedBlocks) {
        out.write(reinterpret_cast<const char*>(b.data()), static_cast<std::streamsize>(b.size()));
    }
    out.flush();
    if (!out) {
        out.close();
        std::error_code ec;
        std::filesystem::remove(tmp, ec);
        return fail("写 pack 失败");
    }
    out.close();
    std::error_code ec;
    std::filesystem::remove(path, ec);
    std::filesystem::rename(tmp, path, ec);
    if (ec) {
        std::filesystem::remove(tmp, ec);
        return fail("替换 pack 文件失败");
    }
    return true;
}

bool peekPack(const std::filesystem::path& path, PackInfo& info, std::string* err)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        if (err) *err = "无法打开 pack";
        return false;
    }
    char magic[8]{};
    uint32_t version = 0;
    int32_t minX = 0, maxX = 0, minZ = 0, maxZ = 0;
    uint32_t cols = 0, rows = 0, blocksX = 0, blocksZ = 0, blockCount = 0;
    uint64_t rawBytes = 0, compBytes = 0;
    if (!readPod(in, magic, sizeof(magic)) || !readPod(in, &version, sizeof(version)) ||
        !readPod(in, &minX, sizeof(minX)) || !readPod(in, &maxX, sizeof(maxX)) ||
        !readPod(in, &minZ, sizeof(minZ)) || !readPod(in, &maxZ, sizeof(maxZ)) ||
        !readPod(in, &cols, sizeof(cols)) || !readPod(in, &rows, sizeof(rows)) ||
        !readPod(in, &blocksX, sizeof(blocksX)) || !readPod(in, &blocksZ, sizeof(blocksZ)) ||
        !readPod(in, &blockCount, sizeof(blockCount)) || !readPod(in, &rawBytes, sizeof(rawBytes)) ||
        !readPod(in, &compBytes, sizeof(compBytes))) {
        if (err) *err = "pack header 不完整";
        return false;
    }
    const PackRect rect{ minX, maxX, minZ, maxZ };
    if (std::memcmp(magic, kMagic, sizeof(kMagic)) != 0 || version != kVersion) {
        if (err) *err = "不是 v3 pack 或版本不匹配";
        return false;
    }
    if (!alignedRect(rect) || cols != kPackBlockChunks || rows != kPackBlockChunks ||
        blockCount == 0 || blockCount > kPackMaxBlocks ||
        blocksX * blocksZ != blockCount || rawBytes != static_cast<uint64_t>(blockCount) * kBlockBytes ||
        compBytes == 0) {
        if (err) *err = "pack 参数非法";
        return false;
    }
    info.rect = rect;
    info.blockCols = cols;
    info.blockRows = rows;
    info.blockCount = blockCount;
    info.rawBytes = rawBytes;
    info.compressedBytes = compBytes;
    return true;
}

bool readPack(const std::filesystem::path& path, PackInfo& info, std::vector<uint8_t>& out,
              std::string* err)
{
    PackInfo h;
    if (!peekPack(path, h, err)) return false;
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        if (err) *err = "无法打开 pack";
        return false;
    }
    in.seekg(static_cast<std::streamoff>(kHeaderBytes), std::ios::beg);
    std::vector<BlockRecord> records(h.blockCount);
    for (auto& r : records) {
        uint8_t pad[3]{};
        if (!readPod(in, &r.rawBytes, sizeof(r.rawBytes)) ||
            !readPod(in, &r.compressedBytes, sizeof(r.compressedBytes)) ||
            !readPod(in, &r.codec, sizeof(r.codec)) || !readPod(in, pad, sizeof(pad)) ||
            !readPod(in, &r.checksum, sizeof(r.checksum))) {
            if (err) *err = "pack block 表不完整";
            return false;
        }
        if (r.rawBytes != kBlockBytes || r.compressedBytes == 0 ||
            r.compressedBytes > kBlockBytes || (r.codec != kCodecRaw && r.codec != kCodecZstd)) {
            if (err) *err = "pack block 记录非法";
            return false;
        }
    }
    out.assign(static_cast<size_t>((h.rect.area() + 7) / 8), 0);
    if (out.empty()) {
        if (err) *err = "pack 范围为空";
        return false;
    }
    const auto blocksX = static_cast<uint64_t>(h.rect.width() / kPackBlockChunks);
    const auto blocksZ = static_cast<uint64_t>(h.rect.height() / kPackBlockChunks);
    std::vector<uint8_t> compressed(kBlockBytes);
    std::vector<uint8_t> raw(kBlockBytes);
    for (uint64_t bz = 0; bz < blocksZ; ++bz) {
        for (uint64_t bx = 0; bx < blocksX; ++bx) {
            const size_t bi = static_cast<size_t>(bz * blocksX + bx);
            const BlockRecord& rec = records[bi];
            if (!in.read(reinterpret_cast<char*>(compressed.data()), rec.compressedBytes)) {
                if (err) *err = "pack 数据不完整";
                return false;
            }
            const uint8_t* src = compressed.data();
            size_t srcLen = rec.compressedBytes;
            if (rec.codec == kCodecZstd) {
                const size_t z = ZSTD_decompress(raw.data(), raw.size(), compressed.data(), rec.compressedBytes);
                if (ZSTD_isError(z) || z != raw.size()) {
                    if (err) *err = "pack zstd 解压失败";
                    return false;
                }
                src = raw.data();
                srcLen = raw.size();
            } else if (rec.compressedBytes != raw.size()) {
                if (err) *err = "pack raw 块长度非法";
                return false;
            }
            if (checksum32(src, srcLen) != rec.checksum) {
                if (err) *err = "pack 校验失败";
                return false;
            }
            for (int row = 0; row < kPackBlockChunks; ++row) {
                const int64_t zRel = static_cast<int64_t>(bz) * kPackBlockChunks + row;
                const int64_t xRelBase = static_cast<int64_t>(bx) * kPackBlockChunks;
                for (int col = 0; col < kPackBlockChunks; ++col) {
                    const size_t srcBit = static_cast<size_t>(row) * kPackBlockChunks + col;
                    if ((src[srcBit / 8] >> (srcBit % 8)) & 1u) {
                        const size_t dst = static_cast<size_t>(zRel * h.rect.width() + xRelBase + col);
                        out[dst / 8] |= static_cast<uint8_t>(1u << (dst % 8));
                    }
                }
            }
        }
    }
    info = h;
    return true;
}

} // namespace slime::cache
