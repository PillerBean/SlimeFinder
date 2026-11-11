// CacheStore.h - v3.12.3 本地缓存目录与 index.json 管理（仅 Bedrock）
//
// 目录布局：
//   <root>/index.json
//   <root>/local/*.slmz
//   <root>/downloaded/*.slmz
//
// 本类只管理 pack 文件和索引，不负责搜索位图拼装；读取 pack 使用 CachePack。
// 索引是唯一可变文件；pack 本身不可修改（下载 pack 只读，本地 pack 也只在构建时新增）。
#pragma once

#include "cache/CachePack.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace slime {
class CacheManager;
}

namespace slime::cache {

struct CachePackEntry {
    std::string file;      // 相对 root，例如 local/pack-000001.slmz
    std::string source;    // "local" / "downloaded"
    PackRect rect;
    uint64_t rawBytes = 0;
    uint64_t compressedBytes = 0;
    int64_t createdAt = 0; // Unix seconds；0 = 未知
};

class CacheStore {
public:
    static constexpr const char* kLocalDir = "local";
    static constexpr const char* kDownloadedDir = "downloaded";
    static constexpr const char* kIndexFile = "index.json";

    explicit CacheStore(std::filesystem::path root = {});

    void setRoot(std::filesystem::path root);
    const std::filesystem::path& root() const noexcept { return m_root; }

    // 加载 index.json；目录不存在时创建，索引不存在时视为空。
    bool load(std::string* err = nullptr);
    // 原子写 index.json。
    bool save(std::string* err = nullptr);

    const std::vector<CachePackEntry>& packs() const noexcept { return m_packs; }
    int64_t totalCompressedBytes() const noexcept;
    int64_t totalRawBytes() const noexcept;

    // 新增一个已经写出的 pack。source 必须为 local/downloaded；文件必须位于对应目录。
    bool addPack(const std::filesystem::path& path, const std::string& source,
                 std::string* err = nullptr);

    // 删除一个 pack（本地或下载均允许；下载 pack 只能整包删除，不能修改内容）。
    bool removePack(const std::string& file, std::string* err = nullptr);

    // 按覆盖范围查找包含该坐标的 pack；返回 nullptr 表示没有缓存覆盖。
    const CachePackEntry* findContaining(int32_t x, int32_t z) const noexcept;

    // 读取一个 pack 的完整位图（行主序，长度 ceil(area/8)）。
    bool readPackBitmap(const CachePackEntry& entry, std::vector<uint8_t>& out,
                        std::string* err = nullptr) const;

    // 把所有 pack 读入现有 CacheManager（供引擎复用；小样本阶段用）。
    bool fillCacheManager(CacheManager& target, std::string* err = nullptr) const;

    // 把现有 CacheManager 的完整 1024 对齐子区域写成 local packs。
    // 未对齐/不足一个 1024 块的边缘不会被写入（避免把未计算区域声明为已覆盖）。
    bool writeFromCacheManager(const CacheManager& source, std::string* err = nullptr);

    // 用当前 CacheManager 全量替换 local packs（下载包不动）。
    // 用于构建保存：避免每次保存都追加重复 pack。
    bool replaceLocalFromCacheManager(const CacheManager& source, std::string* err = nullptr);

private:
    bool ensureDirs(std::string* err);
    std::filesystem::path indexPath() const;
    std::string relativePath(const std::filesystem::path& p) const;

    std::filesystem::path m_root;
    std::vector<CachePackEntry> m_packs;
};

} // namespace slime::cache
