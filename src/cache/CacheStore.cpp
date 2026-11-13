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

bool CacheStore::load(std::string* err, bool scanOrphans)
{
    m_packs.clear();
    if (!ensureDirs(err)) return false;
    std::ifstream in(indexPath(), std::ios::binary);
    // 无索引时仍继续：手动导入/直接放入 downloaded 的 pack 需要在启动时扫描入索引。
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
    in.close(); // Windows：后续去重保存前必须先释放 index.json 句柄
    m_packs = std::move(loaded);

    // v3.12.3：本地去重 + 手动导入扫描。
    // 独立范围包会重复包含边缘 pack；用户把多个 zip 解压到 local 后，不能
    // 在 index 里产生同范围的重复记录。这里按 rect 去重，并扫描未被索引的
    // .slmz，确保网盘人工下载的包也能直接使用。
    bool changed = false;
    std::set<std::tuple<int32_t, int32_t, int32_t, int32_t>> seen;
    std::vector<CachePackEntry> deduped;
    deduped.reserve(m_packs.size());
    for (auto& e : m_packs) {
        const auto key = std::make_tuple(e.rect.minX, e.rect.maxX, e.rect.minZ, e.rect.maxZ);
        if (!seen.insert(key).second) {
            changed = true;
            continue;
        }
        deduped.push_back(std::move(e));
    }
    m_packs = std::move(deduped);

    if (scanOrphans) for (const char* source : { kLocalDir, kDownloadedDir }) {
        const auto dir = m_root / source;
        std::error_code ec;
        if (!std::filesystem::is_directory(dir, ec)) continue;
        for (std::filesystem::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
            if (!it->is_regular_file()) continue;
            if (it->path().extension() != ".slmz") continue;
            std::error_code relEc;
            const std::string relStr = std::filesystem::relative(it->path(), m_root, relEc).generic_string();
            if (relEc || relStr.rfind(std::string(source) + "/", 0) != 0) continue;
            if (std::any_of(m_packs.begin(), m_packs.end(), [&](const CachePackEntry& e) {
                    return e.file == relStr;
                })) {
                continue;
            }
            PackInfo info;
            std::string packErr;
            if (!peekPack(it->path(), info, &packErr)) continue;
            const auto key = std::make_tuple(info.rect.minX, info.rect.maxX, info.rect.minZ, info.rect.maxZ);
            if (!seen.insert(key).second) continue; // 同范围副本不重复入索引
            CachePackEntry e;
            e.file = relStr;
            e.source = source;
            e.rect = info.rect;
            e.rawBytes = info.rawBytes;
            e.compressedBytes = info.compressedBytes;
            e.createdAt = 0;
            m_packs.push_back(std::move(e));
            changed = true;
        }
    }
    if (changed && !save(err)) return false;
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
                         std::string* err, bool persist)
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
    return persist ? save(err) : true;
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
        // 统一向外扩到 8192 对齐：边缘不足 8192 的部分用 Bedrock 判定补齐。
        // pack 覆盖范围固定 8192×8192（内部仍是 64 个 1024×1024 zstd block）。
        constexpr int64_t kTile = kPackTileChunks;
        const int64_t tileX0 = (static_cast<int64_t>(r.minX) >= 0)
            ? (static_cast<int64_t>(r.minX) / kTile) * kTile
            : -(((-static_cast<int64_t>(r.minX)) + kTile - 1) / kTile) * kTile;
        const int64_t tileZ0 = (static_cast<int64_t>(r.minZ) >= 0)
            ? (static_cast<int64_t>(r.minZ) / kTile) * kTile
            : -(((-static_cast<int64_t>(r.minZ)) + kTile - 1) / kTile) * kTile;
        const int64_t tileX1 = ((static_cast<int64_t>(r.maxX) + 1 + kTile - 1) / kTile) * kTile;
        const int64_t tileZ1 = ((static_cast<int64_t>(r.maxZ) + 1 + kTile - 1) / kTile) * kTile;
        for (int64_t tz = tileZ0; tz < tileZ1; tz += kTile) {
            for (int64_t tx = tileX0; tx < tileX1; tx += kTile) {
                const PackRect pr{ static_cast<int32_t>(tx), static_cast<int32_t>(tx + kTile - 1),
                                   static_cast<int32_t>(tz), static_cast<int32_t>(tz + kTile - 1) };
                std::vector<uint8_t> bits(static_cast<size_t>((kTile * kTile + 7) / 8), 0);
                for (int64_t row = 0; row < kTile; ++row) {
                    const int64_t z = tz + row;
                    const auto srcBitBase = static_cast<size_t>((z - r.minZ) * wpr * 64);
                    for (int64_t col = 0; col < kTile; ++col) {
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
                            const size_t dst = static_cast<size_t>(row * kTile + col);
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
        if (err) *err = "缓存范围没有完整的 8192 对齐区块可写入";
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

bool CacheStore::coversRange(const slime::Rect& range) const
{
    if (!range.valid()) return false;
    std::vector<std::pair<int, int>> intervals;
    for (int64_t z = range.minZ; z <= range.maxZ; ++z) {
        intervals.clear();
        for (const auto& e : m_packs) {
            if (z < e.rect.minZ || z > e.rect.maxZ) continue;
            const int a = (std::max)(static_cast<int>(range.minX), e.rect.minX);
            const int b = (std::min)(static_cast<int>(range.maxX), e.rect.maxX);
            if (a <= b) intervals.emplace_back(a - range.minX, b - range.minX);
        }
        if (intervals.empty()) return false;
        std::sort(intervals.begin(), intervals.end());
        int64_t end = -1;
        for (const auto& iv : intervals) {
            if (iv.first > end + 1) return false;
            end = (std::max)(end, static_cast<int64_t>(iv.second));
        }
        if (end < static_cast<int64_t>(range.maxX) - range.minX) return false;
    }
    return true;
}

bool CacheStore::fillCacheManagerRange(CacheManager& target, const slime::Rect& range, std::string* err) const
{
    target.clear();
    if (!range.valid()) return true;

    const int64_t W = static_cast<int64_t>(range.maxX) - range.minX + 1;
    const int64_t H = static_cast<int64_t>(range.maxZ) - range.minZ + 1;
    const int64_t wpr = (W + 63) / 64;

    // 先判断每一行是否被 pack 完整覆盖；完整覆盖才走整带连续位图快路径。
    bool fullyCovered = true;
    std::vector<std::pair<int, int>> intervals;
    for (int64_t z = range.minZ; z <= range.maxZ && fullyCovered; ++z) {
        intervals.clear();
        for (const auto& e : m_packs) {
            if (z < e.rect.minZ || z > e.rect.maxZ) continue;
            const int a = (std::max)(static_cast<int>(range.minX), e.rect.minX);
            const int b = (std::min)(static_cast<int>(range.maxX), e.rect.maxX);
            if (a <= b) intervals.emplace_back(a - range.minX, b - range.minX);
        }
        if (intervals.empty()) { fullyCovered = false; break; }
        std::sort(intervals.begin(), intervals.end());
        int64_t end = -1;
        for (const auto& iv : intervals) {
            if (iv.first > end + 1) { fullyCovered = false; break; }
            end = (std::max)(end, static_cast<int64_t>(iv.second));
        }
        if (end < W - 1) fullyCovered = false;
    }

    if (!fullyCovered) {
        // 部分覆盖：逐个 pack 写入范围位图（引擎按区块读缓存，不做整行快路径）。
        for (const auto& e : m_packs) {
            const Rect pr{ e.rect.minX, e.rect.maxX, e.rect.minZ, e.rect.maxZ };
            if (pr.maxX < range.minX || pr.minX > range.maxX ||
                pr.maxZ < range.minZ || pr.minZ > range.maxZ) continue;
            std::vector<uint8_t> bits;
            if (!readPackBitmap(e, bits, err)) return false;
            if (!target.addPackedBitmap(pr, bits.data(), bits.size())) {
                if (err) *err = "缓存位图合并失败";
                return false;
            }
        }
        return true;
    }

    // 完整覆盖：按行把 pack 位图合并成整带连续位图。使用 64 位字拷贝，
    // 不再先构造 fullBits 再逐 bit 写入 CacheManager。
    std::vector<uint64_t> fullWords(static_cast<size_t>(wpr * H), 0ULL);
    std::vector<uint8_t> rowBuf;
    for (const auto& e : m_packs) {
        const Rect pr{ e.rect.minX, e.rect.maxX, e.rect.minZ, e.rect.maxZ };
        if (pr.maxX < range.minX || pr.minX > range.maxX ||
            pr.maxZ < range.minZ || pr.minZ > range.maxZ) continue;
        std::vector<uint8_t> bits;
        if (!readPackBitmap(e, bits, err)) return false;
        const int64_t pw = static_cast<int64_t>(pr.maxX) - pr.minX + 1;
        const int64_t rowBytes = (pw + 7) / 8;
        if (rowBuf.size() < static_cast<size_t>(rowBytes + 16)) rowBuf.resize(static_cast<size_t>(rowBytes + 16));
        const int x0 = (std::max)(range.minX, pr.minX);
        const int x1 = (std::min)(range.maxX, pr.maxX);
        const int dz0 = (std::max)(range.minZ, pr.minZ);
        const int dz1 = (std::min)(range.maxZ, pr.maxZ);
        for (int z = dz0; z <= dz1; ++z) {
            std::fill(rowBuf.begin(), rowBuf.end(), 0);
            const int64_t localZ = static_cast<int64_t>(z) - pr.minZ;
            const size_t srcOffset = static_cast<size_t>(localZ * rowBytes);
            if (srcOffset + static_cast<size_t>(rowBytes) > bits.size()) {
                if (err) *err = "缓存 pack 位图长度不足";
                return false;
            }
            std::memcpy(rowBuf.data(), bits.data() + srcOffset, static_cast<size_t>(rowBytes));
            int srcBit = x0 - pr.minX;
            int dstBit = static_cast<int>((static_cast<int64_t>(z) - range.minZ) * (wpr * 64) +
                                          (x0 - range.minX));
            int remain = x1 - x0 + 1;
            while (remain > 0) {
                const int chunk = (std::min)(64, remain);
                const size_t byte = static_cast<size_t>(srcBit >> 3);
                const int shift = srcBit & 7;
                uint64_t lo = 0, hi = 0;
                std::memcpy(&lo, rowBuf.data() + byte, 8);
                std::memcpy(&hi, rowBuf.data() + byte + 8, 8);
                uint64_t v = (lo >> shift);
                if (shift != 0) v |= (hi << (64 - shift));
                if (chunk < 64) v &= ((1ULL << chunk) - 1ULL);
                const int dw = dstBit >> 6;
                const int ds = dstBit & 63;
                fullWords[static_cast<size_t>(dw)] |= (v << ds);
                if (ds != 0 && ds + chunk > 64 &&
                    static_cast<size_t>(dw + 1) < fullWords.size()) {
                    fullWords[static_cast<size_t>(dw + 1)] |= (v >> (64 - ds));
                }
                srcBit += chunk;
                dstBit += chunk;
                remain -= chunk;
            }
        }
    }
    if (!target.addPackedWords(range, fullWords.data(), fullWords.size())) {
        if (err) *err = "缓存整带位图合并失败";
        return false;
    }
    return true;
}

} // namespace slime::cache
