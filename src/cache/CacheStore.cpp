#include "cache/CacheStore.h"

#include <atomic>
#include <cstdlib>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "core/CacheManager.h"

#include <algorithm>
#include <chrono>
#include <set>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <system_error>

namespace slime::cache {
namespace {

std::string jsonEscape(const std::string& s)
{
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
        case '\\': out += "\\\\"; break;
        case '"': out += "\\\""; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default: out += c; break;
        }
    }
    return out;
}

bool parseString(const std::string& line, const std::string& key, std::string& value)
{
    const std::string needle = "\"" + key + "\"";
    size_t p = line.find(needle);
    if (p == std::string::npos) return false;
    p = line.find(':', p + needle.size());
    if (p == std::string::npos) return false;
    ++p;
    while (p < line.size() && (line[p] == ' ' || line[p] == '\t')) ++p;
    if (p >= line.size() || line[p] != '"') return false;
    ++p;
    std::string out;
    for (; p < line.size(); ++p) {
        const char c = line[p];
        if (c == '"') {
            value = out;
            return true;
        }
        if (c == '\\' && p + 1 < line.size()) {
            const char n = line[++p];
            switch (n) {
            case 'n': out += '\n'; break;
            case 'r': out += '\r'; break;
            case 't': out += '\t'; break;
            case '\\': out += '\\'; break;
            case '"': out += '"'; break;
            default: out += n; break;
            }
        } else {
            out += c;
        }
    }
    return false;
}

bool parseI64(const std::string& line, const std::string& key, int64_t& value)
{
    const std::string needle = "\"" + key + "\"";
    size_t p = line.find(needle);
    if (p == std::string::npos) return false;
    p = line.find(':', p + needle.size());
    if (p == std::string::npos) return false;
    ++p;
    while (p < line.size() && (line[p] == ' ' || line[p] == '\t')) ++p;
    const char* begin = line.c_str() + p;
    char* end = nullptr;
    const long long v = std::strtoll(begin, &end, 10);
    if (end == begin) return false;
    value = v;
    return true;
}

int64_t nowUnix()
{
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

} // namespace

CacheStore::CacheStore(std::filesystem::path root)
    : m_root(std::move(root))
{
}

void CacheStore::setRoot(std::filesystem::path root)
{
    m_root = std::move(root);
    m_packs.clear();
}

std::filesystem::path CacheStore::indexPath() const
{
    return m_root / kIndexFile;
}

bool CacheStore::ensureDirs(std::string* err)
{
    std::error_code ec;
    std::filesystem::create_directories(m_root / kLocalDir, ec);
    if (ec) {
        if (err) *err = "无法创建 local 缓存目录";
        return false;
    }
    std::filesystem::create_directories(m_root / kDownloadedDir, ec);
    if (ec) {
        if (err) *err = "无法创建 downloaded 缓存目录";
        return false;
    }
    return true;
}

std::string CacheStore::relativePath(const std::filesystem::path& p) const
{
    std::error_code ec;
    const auto rel = std::filesystem::relative(p, m_root, ec);
    if (ec) return {};
    return rel.generic_string();
}

bool CacheStore::load(std::string* err)
{
    m_packs.clear();
    if (!ensureDirs(err)) return false;
    std::ifstream in(indexPath(), std::ios::binary);
    if (!in) {
        // 无索引 = 空缓存
        return true;
    }
    std::string line;
    int64_t format = 0;
    std::vector<CachePackEntry> loaded;
    while (std::getline(in, line)) {
        if (line.find("\"format\"") != std::string::npos) {
            parseI64(line, "format", format);
            continue;
        }
        if (line.find("\"file\"") == std::string::npos) continue;
        CachePackEntry e;
        int64_t minX = 0, maxX = -1, minZ = 0, maxZ = -1, raw = 0, comp = 0, created = 0;
        if (!parseString(line, "file", e.file) || !parseString(line, "source", e.source) ||
            !parseI64(line, "minX", minX) || !parseI64(line, "maxX", maxX) ||
            !parseI64(line, "minZ", minZ) || !parseI64(line, "maxZ", maxZ) ||
            !parseI64(line, "rawBytes", raw) || !parseI64(line, "compressedBytes", comp)) {
            if (err) *err = "index.json 记录格式错误";
            return false;
        }
        parseI64(line, "createdAt", created);
        if (e.source != kLocalDir && e.source != kDownloadedDir) continue;
        if (e.file.rfind("local/", 0) != 0 && e.file.rfind("downloaded/", 0) != 0) continue;
        if (e.file.find("..") != std::string::npos) continue;
        e.rect = PackRect{ static_cast<int32_t>(minX), static_cast<int32_t>(maxX),
                           static_cast<int32_t>(minZ), static_cast<int32_t>(maxZ) };
        e.rawBytes = static_cast<uint64_t>(raw);
        e.compressedBytes = static_cast<uint64_t>(comp);
        e.createdAt = created;
        // 索引可保留指向缺失文件的记录吗？不保留，避免 UI 展示幽灵缓存。
        if (!std::filesystem::is_regular_file(m_root / e.file)) continue;
        loaded.push_back(std::move(e));
    }
    if (format != 0 && format != 3) {
        if (err) *err = "index.json 版本不匹配";
        return false;
    }
    m_packs = std::move(loaded);
    return true;
}

bool CacheStore::save(std::string* err)
{
    if (!ensureDirs(err)) return false;
    const auto tmp = indexPath().string() + ".new";
    std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
    if (!out) {
        if (err) *err = "无法写入 index.json";
        return false;
    }
    out << "{\n  \"format\": 3,\n  \"packs\": [\n";
    for (size_t i = 0; i < m_packs.size(); ++i) {
        const auto& p = m_packs[i];
        out << "    {\"file\":\"" << jsonEscape(p.file) << "\",\"source\":\"" << p.source
            << "\",\"minX\":" << p.rect.minX << ",\"maxX\":" << p.rect.maxX
            << ",\"minZ\":" << p.rect.minZ << ",\"maxZ\":" << p.rect.maxZ
            << ",\"rawBytes\":" << p.rawBytes << ",\"compressedBytes\":" << p.compressedBytes
            << ",\"createdAt\":" << p.createdAt << "}";
        if (i + 1 < m_packs.size()) out << ",";
        out << "\n";
    }
    out << "  ]\n}\n";
    out.flush();
    if (!out) {
        if (err) *err = "index.json 写入失败";
        return false;
    }
    out.close();
    std::error_code ec;
    std::filesystem::remove(indexPath(), ec);
    std::filesystem::rename(tmp, indexPath(), ec);
    if (ec) {
        std::filesystem::remove(tmp, ec);
        if (err) *err = "index.json 替换失败";
        return false;
    }
    return true;
}

int64_t CacheStore::totalCompressedBytes() const noexcept
{
    int64_t n = 0;
    for (const auto& p : m_packs) n += static_cast<int64_t>(p.compressedBytes);
    return n;
}

int64_t CacheStore::totalRawBytes() const noexcept
{
    int64_t n = 0;
    for (const auto& p : m_packs) n += static_cast<int64_t>(p.rawBytes);
    return n;
}

bool CacheStore::addPack(const std::filesystem::path& path, const std::string& source,
                         std::string* err)
{
    if (source != kLocalDir && source != kDownloadedDir) {
        if (err) *err = "未知缓存来源";
        return false;
    }
    if (!ensureDirs(err)) return false;
    const auto abs = std::filesystem::absolute(path);
    const auto sourceRoot = std::filesystem::absolute(m_root / source);
    std::error_code ec;
    const auto rel = std::filesystem::relative(abs, m_root, ec);
    if (ec || rel.empty()) {
        if (err) *err = "缓存 pack 不在缓存目录内";
        return false;
    }
    const std::string relStr = rel.generic_string();
    if (relStr.rfind(source + "/", 0) != 0 || relStr.find("..") != std::string::npos) {
        if (err) *err = "缓存 pack 来源目录不匹配";
        return false;
    }
    PackInfo info;
    if (!peekPack(path, info, err)) return false;
    if (info.blockCount == 0 || !info.rect.valid()) {
        if (err) *err = "缓存 pack 元数据非法";
        return false;
    }
    const auto same = std::find_if(m_packs.begin(), m_packs.end(), [&](const CachePackEntry& e) {
        return e.file == relStr;
    });
    if (same != m_packs.end()) {
        if (err) *err = "缓存 pack 已在索引中";
        return false;
    }
    CachePackEntry e;
    e.file = relStr;
    e.source = source;
    e.rect = info.rect;
    e.rawBytes = info.rawBytes;
    e.compressedBytes = info.compressedBytes;
    e.createdAt = nowUnix();
    (void)sourceRoot;
    m_packs.push_back(std::move(e));
    return save(err);
}

bool CacheStore::removePack(const std::string& file, std::string* err)
{
    const auto it = std::find_if(m_packs.begin(), m_packs.end(), [&](const CachePackEntry& e) {
        return e.file == file;
    });
    if (it == m_packs.end()) {
        if (err) *err = "缓存 pack 不存在";
        return false;
    }
    if (file.rfind("local/", 0) != 0 && file.rfind("downloaded/", 0) != 0) {
        if (err) *err = "缓存 pack 路径非法";
        return false;
    }
    std::error_code ec;
    std::filesystem::remove(m_root / file, ec);
    if (ec) {
        if (err) *err = "删除缓存 pack 失败";
        return false;
    }
    m_packs.erase(it);
    return save(err);
}

bool CacheStore::writeFromCacheManager(const CacheManager& source, std::string* err)
{
    if (!ensureDirs(err)) return false;
    const auto& ranges = source.ranges();
    if (ranges.empty()) {
        if (err) *err = "没有可写入的缓存范围";
        return false;
    }
    const auto epoch = nowUnix();
    int64_t serial = 0;
    bool wrote = false;
    for (size_t ri = 0; ri < ranges.size(); ++ri) {
        const Rect& r = ranges[ri];
        int64_t wpr = 0, rows = 0;
        const uint64_t* words = source.rangeBitmap(ri, &wpr, &rows);
        if (!words || wpr <= 0 || rows <= 0) continue;
        // 向外扩到 1024 对齐：边缘块不足 1024 的部分用 Bedrock 判定补齐，
        // 这样 pack 总是完整的 1024 tile，同时不会留下未缓存边缘。
        const int64_t tileX0 = (static_cast<int64_t>(r.minX) >= 0)
            ? (static_cast<int64_t>(r.minX) / 1024) * 1024
            : -(((-static_cast<int64_t>(r.minX)) + 1023) / 1024) * 1024;
        const int64_t tileZ0 = (static_cast<int64_t>(r.minZ) >= 0)
            ? (static_cast<int64_t>(r.minZ) / 1024) * 1024
            : -(((-static_cast<int64_t>(r.minZ)) + 1023) / 1024) * 1024;
        const int64_t tileX1 = ((static_cast<int64_t>(r.maxX) + 1 + 1023) / 1024) * 1024;
        const int64_t tileZ1 = ((static_cast<int64_t>(r.maxZ) + 1 + 1023) / 1024) * 1024;
        for (int64_t tz = tileZ0; tz < tileZ1; tz += 1024) {
            for (int64_t tx = tileX0; tx < tileX1; tx += 1024) {
                const PackRect pr{ static_cast<int32_t>(tx), static_cast<int32_t>(tx + 1023),
                                   static_cast<int32_t>(tz), static_cast<int32_t>(tz + 1023) };
                std::vector<uint8_t> bits((1024 * 1024 + 7) / 8, 0);
                for (int row = 0; row < 1024; ++row) {
                    const int64_t z = tz + row;
                    const auto srcBitBase = static_cast<size_t>((z - r.minZ) * wpr * 64);
                    for (int col = 0; col < 1024; ++col) {
                        const int64_t x = tx + col;
                        const size_t srcBit = srcBitBase + static_cast<size_t>(x - r.minX);
                        bool slime = false;
                        if (x >= r.minX && x <= r.maxX && z >= r.minZ && z <= r.maxZ) {
                            slime = (words[srcBit / 64] >> (srcBit % 64)) & 1ull;
                        } else {
                            // 对齐补齐的边缘：Bedrock 判定与种子无关，可安全直接计算。
                            slime = slime::SlimeEngine::isSlimeChunkBedrockMT(static_cast<int>(x), static_cast<int>(z));
                        }
                        if (slime) {
                            const size_t dst = static_cast<size_t>(row) * 1024 + col;
                            bits[dst / 8] |= static_cast<uint8_t>(1u << (dst % 8));
                        }
                    }
                }
                std::string name = "local/pack-" + std::to_string(epoch) + "-" + std::to_string(serial++) + ".slmz";
                while (std::filesystem::exists(m_root / name)) {
                    name = "local/pack-" + std::to_string(epoch) + "-" + std::to_string(serial++) + ".slmz";
                }
                const auto path = m_root / name;
                if (!writePack(path, pr, bits.data(), bits.size(), err)) return false;
                PackInfo info;
                if (!peekPack(path, info, err)) return false;
                CachePackEntry e;
                e.file = name;
                e.source = kLocalDir;
                e.rect = info.rect;
                e.rawBytes = info.rawBytes;
                e.compressedBytes = info.compressedBytes;
                e.createdAt = nowUnix();
                m_packs.push_back(std::move(e));
                wrote = true;
            }
        }
    }
    if (!wrote) {
        if (err) *err = "缓存范围没有完整的 1024 对齐区块可写入";
        return false;
    }
    return save(err);
}

bool CacheStore::replaceLocalFromCacheManager(const CacheManager& source, std::string* err)
{
    // 只清 local；downloaded 是只读外部缓存，不能因构建保存被删除。
    for (auto it = m_packs.begin(); it != m_packs.end();) {
        if (it->source == kLocalDir) {
            std::error_code ec;
            std::filesystem::remove(m_root / it->file, ec);
            it = m_packs.erase(it);
        } else {
            ++it;
        }
    }
    if (!save(err)) return false;
    if (source.ranges().empty()) return true;
    return writeFromCacheManager(source, err);
}

const CachePackEntry* CacheStore::findContaining(int32_t x, int32_t z) const noexcept
{
    for (const auto& e : m_packs) {
        if (x >= e.rect.minX && x <= e.rect.maxX && z >= e.rect.minZ && z <= e.rect.maxZ) {
            return &e;
        }
    }
    return nullptr;
}

bool CacheStore::readPackBitmap(const CachePackEntry& entry, std::vector<uint8_t>& out,
                                std::string* err) const
{
    PackInfo info;
    if (!readPack(m_root / entry.file, info, out, err)) return false;
    if (info.rect.minX != entry.rect.minX || info.rect.maxX != entry.rect.maxX ||
        info.rect.minZ != entry.rect.minZ || info.rect.maxZ != entry.rect.maxZ) {
        if (err) *err = "pack 索引范围与文件头不一致";
        return false;
    }
    return true;
}

bool CacheStore::fillCacheManager(CacheManager& target, std::string* err) const
{
    target.clear();
    std::vector<Rect> ranges;
    std::vector<std::pair<int, int>> slimeChunks;
    ranges.reserve(m_packs.size());
    for (const auto& e : m_packs) {
        std::vector<uint8_t> bits;
        if (!readPackBitmap(e, bits, err)) return false;
        ranges.push_back(Rect{ e.rect.minX, e.rect.maxX, e.rect.minZ, e.rect.maxZ });
        const int64_t w = e.rect.width();
        const int64_t h = e.rect.height();
        for (int64_t z = 0; z < h; ++z) {
            for (int64_t x = 0; x < w; ++x) {
                const int64_t bit = z * w + x;
                if ((bits[static_cast<size_t>(bit / 8)] >> (bit % 8)) & 1u) {
                    slimeChunks.emplace_back(static_cast<int>(e.rect.minX + x),
                                             static_cast<int>(e.rect.minZ + z));
                }
            }
        }
    }
    if (!ranges.empty()) {
        // 若所有 pack 恰好拼成一个无重叠矩形，则合并为单个 range：
        // 这样旧版 CacheManager::covers 的单 range 语义也能直接命中整个缓存区。
        Rect merged = ranges.front();
        for (const Rect& r : ranges) {
            merged.minX = std::min(merged.minX, r.minX);
            merged.maxX = std::max(merged.maxX, r.maxX);
            merged.minZ = std::min(merged.minZ, r.minZ);
            merged.maxZ = std::max(merged.maxZ, r.maxZ);
        }
        int64_t sumArea = 0;
        for (const Rect& r : ranges) sumArea += (static_cast<int64_t>(r.maxX) - r.minX + 1) *
                                                  (static_cast<int64_t>(r.maxZ) - r.minZ + 1);
        if (sumArea == (static_cast<int64_t>(merged.maxX) - merged.minX + 1) *
                           (static_cast<int64_t>(merged.maxZ) - merged.minZ + 1)) {
            target.mergeRaw({ merged }, slimeChunks);
        } else {
            target.mergeRaw(ranges, slimeChunks);
        }
    }
    return true;
}

} // namespace slime::cache
