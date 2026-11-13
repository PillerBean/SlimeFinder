// CacheManager.cpp - 位图化缓存读写实现（v3.3.8 任务 3+5 合流）
//
// 设计要点（与 CacheManager.h 顶部注释一致）：
//   * 内存 = m_ranges + 每 range 一张位图（行优先，(z-minZ)*W + (x-minX)）+ 不在任何 range 内的 outlairs；
//   * 每个坐标只存一次：置位在**第一个包含它的 range**（= owned 语义，lookup 也是查这个 range）；
//   * pruneToFit 丢 range 时，先把「仍被其它 range 覆盖」的位改挂过去，再释放（保留旧的过滤语义）；
//   * v1 文件仍可读（读入即转位图）；v2 文件走**原子替换**（.new → 回读校验 → ReplaceFileW/MoveFileExW）。
#include "core/CacheManager.h"

#include <algorithm>
#include <bit>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>

#ifdef _WIN32
#    ifndef WIN32_LEAN_AND_MEAN
#        define WIN32_LEAN_AND_MEAN
#    endif
#    ifndef NOMINMAX
#        define NOMINMAX
#    endif
#    include <windows.h>
#endif

#include "utils/IncidentLog.h"

namespace slime {

namespace {
constexpr uint32_t kMagic = 0x534C4D45U; // "SLME"
constexpr uint32_t kVersion1 = 1;        // 历史：坐标集合
constexpr uint32_t kVersion2 = 2;        // v3.3.8：按 range 位图 + outlier 坐标段
constexpr uint32_t kMaxRanges = 1 << 20;
constexpr uint64_t kMaxEntries = (1ULL << 40);
constexpr double kSlimeDensityV2 = 0.10; // 基准密度（支撑度判据用）
constexpr double kSupportRatioV2 = 0.50; // 支撑度阈值（用户拍板）
constexpr double kMinExpectedV2 = 100.0; // 期望条目数低于此值 ⇒ 不做判断

inline uint64_t packCoord(int x, int z) noexcept
{
    return (static_cast<uint64_t>(static_cast<uint32_t>(x)) << 32) | static_cast<uint32_t>(z);
}
inline int unpackX(uint64_t v) noexcept { return static_cast<int>(static_cast<int32_t>(v >> 32)); }
inline int unpackZ(uint64_t v) noexcept { return static_cast<int>(static_cast<int32_t>(v & 0xFFFFFFFFULL)); }
inline int64_t rectW(const Rect& r) noexcept { return static_cast<int64_t>(r.maxX) - r.minX + 1; }
inline int64_t rectH(const Rect& r) noexcept { return static_cast<int64_t>(r.maxZ) - r.minZ + 1; }
inline int popcount64(uint64_t v) noexcept { return std::popcount(v); }
inline int ctz64(uint64_t v) noexcept { return std::countr_zero(v); }
// 单个 range 的规模上限（防御损坏文件导致的超大分配：26 位 ≈ 6700 万 > 缩减后的基岩边界 131072）
constexpr int64_t kMaxRectSide = 1LL << 26;
// 原子替换：新文件已写好且校验通过，用它替换旧文件（Windows 优先 ReplaceFileW）
bool atomicReplace(const std::filesystem::path& target, const std::filesystem::path& fresh) noexcept
{
#ifdef _WIN32
    if (ReplaceFileW(target.wstring().c_str(), fresh.wstring().c_str(), nullptr,
                     REPLACEFILE_IGNORE_MERGE_ERRORS, nullptr, nullptr)) {
        return true;
    }
    if (MoveFileExW(fresh.wstring().c_str(), target.wstring().c_str(), MOVEFILE_REPLACE_EXISTING)) {
        return true;
    }
    return false;
#else
    std::error_code ec;
    std::filesystem::rename(fresh, target, ec);
    return !ec;
#endif
}
} // namespace

void CacheManager::clear() noexcept
{
    m_ranges.clear();
    m_bitmaps.clear();
    m_uniqueEntries = 0;
    m_outliers.clear();
}

int64_t CacheManager::bitmapBytes() const noexcept
{
    int64_t total = 0;
    for (const Bitmap& b : m_bitmaps) total += static_cast<int64_t>(b.bits.size()) * 8;
    return total;
}

int64_t CacheManager::sizeBytes() const noexcept
{
    // v2 口径：24 B 头 + 每 range（16 B 矩形 + 4 B 字数 + 位图）+ outlier 坐标 8 B/条
    int64_t total = 24;
    for (const Bitmap& b : m_bitmaps) total += 20 + static_cast<int64_t>(b.bits.size()) * 8;
    total += static_cast<int64_t>(m_outliers.size()) * 8;
    return total;
}

bool CacheManager::covers(const Rect& region) const noexcept
{
    if (!region.valid()) return false;
    for (const Rect& r : m_ranges) {
        if (r.contains(region)) return true;
    }
    return false;
}
size_t CacheManager::findRange(int x, int z) const noexcept
{
    // v3.3.8.1（任务书 二）：先用「上次命中的 range」做线程局部快查（顺序遍历时几乎总是命中），
    // 未命中再线性扫描。惰性校验（contains）保证任何竞态/失效下标都只会退化为全扫描，结果不变。
    static thread_local size_t lastIdx = static_cast<size_t>(-1);
    if (lastIdx < m_ranges.size() && m_ranges[lastIdx].contains(x, z)) return lastIdx;
    for (size_t i = 0; i < m_ranges.size(); ++i) {
        if (m_ranges[i].contains(x, z)) {
            lastIdx = i;
            return i;
        }
    }
    return static_cast<size_t>(-1);
}

bool CacheManager::testBit(size_t idx, int x, int z) const noexcept
{
    const Bitmap& b = m_bitmaps[idx];
    const Rect& r = m_ranges[idx];
    const int64_t col = static_cast<int64_t>(x) - r.minX;
    const int64_t row = static_cast<int64_t>(z) - r.minZ;
    const int64_t bitIndex = row * (b.wordsPerRow * 64) + col;
    const uint64_t word = b.bits[static_cast<size_t>(bitIndex >> 6)];
    return (word >> (bitIndex & 63)) & 1ULL;
}

bool CacheManager::setBit(size_t idx, int x, int z) noexcept
{
    Bitmap& b = m_bitmaps[idx];
    const Rect& r = m_ranges[idx];
    const int64_t col = static_cast<int64_t>(x) - r.minX;
    const int64_t row = static_cast<int64_t>(z) - r.minZ;
    const int64_t bitIndex = row * (b.wordsPerRow * 64) + col;
    uint64_t& word = b.bits[static_cast<size_t>(bitIndex >> 6)];
    const uint64_t mask = 1ULL << (bitIndex & 63);
    if (word & mask) return false;
    word |= mask;
    return true;
}

size_t CacheManager::ensureRange(const Rect& r)
{
    for (size_t i = 0; i < m_ranges.size(); ++i) {
        const Rect& e = m_ranges[i];
        if (e.minX == r.minX && e.maxX == r.maxX && e.minZ == r.minZ && e.maxZ == r.maxZ) return i;
    }
    Bitmap b;
    b.wordsPerRow = (rectW(r) + 63) / 64;
    b.rows = rectH(r);
    b.bits.assign(static_cast<size_t>(b.wordsPerRow * b.rows), 0ULL);
    m_ranges.push_back(r);
    m_bitmaps.push_back(std::move(b));
    return m_ranges.size() - 1;
}

bool CacheManager::isSlime(int x, int z) const noexcept
{
    const size_t i = findRange(x, z);
    if (i != static_cast<size_t>(-1)) return testBit(i, x, z);
    const uint64_t key = packCoord(x, z);
    return std::binary_search(m_outliers.begin(), m_outliers.end(), key);
}

int CacheManager::lookup(int x, int z) const noexcept
{
    const size_t i = findRange(x, z);
    if (i == static_cast<size_t>(-1)) return -1;
    return testBit(i, x, z) ? 1 : 0;
}

int64_t CacheManager::addOutlier(int x, int z)
{
    const uint64_t key = packCoord(x, z);
    const auto it = std::lower_bound(m_outliers.begin(), m_outliers.end(), key);
    if (it != m_outliers.end() && *it == key) return 0;
    m_outliers.insert(it, key);
    return 1;
}

void CacheManager::mergeRegion(const Rect& region, const std::vector<std::pair<int, int>>& slimeChunks)
{
    if (!region.valid()) return;
    ensureRange(region);
    for (const auto& p : slimeChunks) {
        const int x = p.first, z = p.second;
        const size_t i = findRange(x, z); // 第一个包含它的 range（可能是更早的声明）
        if (i != static_cast<size_t>(-1)) {
            if (setBit(i, x, z)) ++m_uniqueEntries;
        } else {
            addOutlier(x, z);
        }
    }
}

bool CacheManager::addPackedBitmap(const Rect& region, const uint8_t* bits, size_t bytes)
{
    if (!region.valid() || !bits) return false;
    const int64_t w = static_cast<int64_t>(region.maxX) - region.minX + 1;
    const int64_t h = static_cast<int64_t>(region.maxZ) - region.minZ + 1;
    const size_t needed = static_cast<size_t>((w * h + 7) / 8);
    if (bytes < needed) return false;
    const size_t idx = ensureRange(region);
    for (int64_t z = 0; z < h; ++z) {
        for (int64_t x = 0; x < w; ++x) {
            const size_t bit = static_cast<size_t>(z * w + x);
            if ((bits[bit / 8] >> (bit % 8)) & 1u) {
                if (setBit(idx, static_cast<int>(region.minX + x), static_cast<int>(region.minZ + z))) {
                    ++m_uniqueEntries;
                }
            }
        }
    }
    return true;
}

void CacheManager::mergeRaw(const std::vector<Rect>& ranges, const std::vector<std::pair<int, int>>& slimeChunks)
{
    for (const Rect& r : ranges) {
        if (r.valid()) ensureRange(r);
    }
    std::vector<uint64_t> newOutliers;
    for (const auto& p : slimeChunks) {
        const int x = p.first, z = p.second;
        const size_t i = findRange(x, z);
        if (i != static_cast<size_t>(-1)) {
            if (setBit(i, x, z)) ++m_uniqueEntries;
        } else {
            newOutliers.push_back(packCoord(x, z));
        }
    }
    if (!newOutliers.empty()) {
        std::sort(newOutliers.begin(), newOutliers.end());
        newOutliers.erase(std::unique(newOutliers.begin(), newOutliers.end()), newOutliers.end());
        std::vector<uint64_t> merged;
        merged.reserve(m_outliers.size() + newOutliers.size());
        std::merge(m_outliers.begin(), m_outliers.end(), newOutliers.begin(), newOutliers.end(),
                   std::back_inserter(merged));
        merged.erase(std::unique(merged.begin(), merged.end()), merged.end());
        m_outliers.swap(merged);
    }
}

void CacheManager::removeRange(size_t idx) noexcept
{
    // 丢声明前：把「仍被其它 range 覆盖」的位改挂到那个 range（保留旧的过滤语义，避免误丢）
    Bitmap bm = std::move(m_bitmaps[idx]); // 移出（随后要 erase）
    const Rect r = m_ranges[idx];
    const int64_t rowBits = bm.wordsPerRow * 64;
    const int64_t w = rectW(r), h = rectH(r);
    int64_t lost = 0;
    for (size_t wi = 0; wi < bm.bits.size(); ++wi) {
        uint64_t word = bm.bits[wi];
        while (word) {
            const int bit = ctz64(word);
            word &= word - 1;
            const int64_t bitIndex = static_cast<int64_t>(wi) * 64 + bit;
            const int64_t row = bitIndex / rowBits;
            const int64_t col = bitIndex % rowBits;
            if (col >= w || row >= h) continue; // 位图尾部填充位
            const int x = r.minX + static_cast<int>(col);
            const int z = r.minZ + static_cast<int>(row);
            size_t target = static_cast<size_t>(-1);
            for (size_t j = 0; j < m_ranges.size(); ++j) {
                if (j == idx) continue;
                if (m_ranges[j].contains(x, z)) {
                    target = j;
                    break;
                }
            }
            if (target == static_cast<size_t>(-1)) {
                ++lost;
            } else {
                setBit(target, x, z); // 该位原本也计数在 m_uniqueEntries 中 → 改挂不改变总数
            }
        }
    }
    m_ranges.erase(m_ranges.begin() + static_cast<std::ptrdiff_t>(idx));
    m_bitmaps.erase(m_bitmaps.begin() + static_cast<std::ptrdiff_t>(idx));
    m_uniqueEntries -= lost;
    if (m_uniqueEntries < 0) m_uniqueEntries = 0;
}

void CacheManager::pruneToFit(int64_t maxBytes) noexcept
{
    if (maxBytes <= 0) return;
    // 丢最旧 range（含位图）直到不超预算；与旧实现一致：允许清空到 0 个声明
    while (!m_ranges.empty() && sizeBytes() > maxBytes) {
        removeRange(0);
    }
}

int64_t CacheManager::dropHollowRanges() noexcept
{
    if (m_ranges.empty()) return 0;
    std::vector<size_t> drop;
    std::vector<int64_t> support;
    for (size_t i = 0; i < m_ranges.size(); ++i) {
        const int64_t area = rectW(m_ranges[i]) * rectH(m_ranges[i]);
        const double expected = static_cast<double>(area) * kSlimeDensityV2;
        if (expected < kMinExpectedV2) continue; // 小面积放行
        int64_t set = 0;
        for (uint64_t w : m_bitmaps[i].bits) set += popcount64(w);
        if (static_cast<double>(set) < expected * kSupportRatioV2) {
            drop.push_back(i);
            support.push_back(set);
        }
    }
    if (drop.empty()) return 0;
    // 从后往前删（下标稳定）
    for (size_t k = drop.size(); k-- > 0;) removeRange(drop[k]);
    slime::IncidentLog::note("cache.hollow 剔除无条目支撑的覆盖声明 " + std::to_string(drop.size()) +
                             " 个（判据：声明内条目数 ≥ 期望×50%，期望 = 面积×10%；小面积「期望<100 条」放行）；"
                             "保留声明 " +
                             std::to_string(m_ranges.size()) + " 个，条目 " + std::to_string(entryCount()) +
                             " 条（位图 " + std::to_string(bitmapBytes() / 1024) + " KiB）");
    return static_cast<int64_t>(drop.size());
}

const uint64_t* CacheManager::rangeBitmap(size_t idx, int64_t* wordsPerRow, int64_t* rows) const noexcept
{
    if (idx >= m_bitmaps.size()) return nullptr;
    if (wordsPerRow) *wordsPerRow = m_bitmaps[idx].wordsPerRow;
    if (rows) *rows = m_bitmaps[idx].rows;
    return m_bitmaps[idx].bits.data();
}

bool CacheManager::loadFile(const std::filesystem::path& path, bool replace, std::string& err,
                            const std::function<void(int64_t, int64_t)>& onProgress)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        err = "无法打开缓存文件：" + path.string();
        return false;
    }
    auto read32 = [&](uint32_t& v) { in.read(reinterpret_cast<char*>(&v), 4); return in.gcount() == 4; };
    auto read64 = [&](uint64_t& v) { in.read(reinterpret_cast<char*>(&v), 8); return in.gcount() == 8; };

    uint32_t magic = 0, ver = 0, rangeCount = 0, reserved = 0;
    uint64_t count = 0; // v1: entryCount；v2: outlierCount
    if (!read32(magic) || magic != kMagic) { err = "不是有效的史莱姆缓存文件（魔数错误）。"; return false; }
    if (!read32(ver) || (ver != kVersion1 && ver != kVersion2)) {
        err = "缓存版本不受支持（版本 " + std::to_string(ver) + "）。";
        return false;
    }
    if (!read32(rangeCount) || rangeCount > kMaxRanges) { err = "缓存文件头损坏。"; return false; }
    if (ver == kVersion2) {
        if (!read32(reserved)) { err = "缓存文件头损坏。"; return false; }
    }
    if (!read64(count) || count > kMaxEntries) { err = "缓存文件头损坏。"; return false; }

    // v1：头部之后是「全部 range 矩形 + 全部坐标」；v2：头部之后是「每 range（矩形+位图）」+ outlier 坐标段
    CacheManager loaded;
    auto readRect = [&](Rect& r) -> bool {
        int32_t v[4];
        in.read(reinterpret_cast<char*>(v), 16);
        if (in.gcount() != 16) return false;
        r = Rect{ v[0], v[1], v[2], v[3] };
        if (!r.valid()) return false;
        // 防御损坏文件：单边超过 2^26 或面积过大（> 2^33 个位置 ≈ 1 GB 位图）直接拒绝
        const int64_t w = static_cast<int64_t>(r.maxX) - r.minX + 1;
        const int64_t h = static_cast<int64_t>(r.maxZ) - r.minZ + 1;
        if (w > kMaxRectSide || h > kMaxRectSide || w * h > (1LL << 33)) return false;
        return true;
    };

    // v3.3.8.1（任务书 1.3）：进度上报
    //   v2：按「已读字节 / 文件字节」上报，且位图按 1 MiB 分块读取，进度条因此是**连续**的
    //       （按 range 上报时，少 range 的大缓存会 0→100 跳变，用户看不到「实时进度」）。
    //   v1：按「已读条目 / 总条目」（每 0x3FFFF 条节流一次），与旧行为一致。
    const auto reportProgress = [&onProgress](int64_t done, int64_t total) {
        if (onProgress) onProgress(done, total > 0 ? total : 1);
    };
    std::error_code sizeEc;
    const uintmax_t sizeRaw = std::filesystem::file_size(path, sizeEc);
    const int64_t fileBytes = sizeEc ? 0 : static_cast<int64_t>(sizeRaw);
    const auto reportBytes = [&]() {
        if (!onProgress || fileBytes <= 0) return;
        const std::streampos pos = in.tellg();
        if (pos < 0) return;
        onProgress(static_cast<int64_t>(pos), fileBytes);
    };
    if (ver == kVersion2) {
        for (uint32_t i = 0; i < rangeCount; ++i) {
            Rect r{};
            if (!readRect(r)) { err = "缓存文件包含无效/过大的范围，已中止加载。"; return false; }
            const size_t idx = loaded.ensureRange(r);
            uint32_t words = 0;
            in.read(reinterpret_cast<char*>(&words), 4);
            if (in.gcount() != 4) { err = "缓存文件数据不完整（位图头）。"; return false; }
            // 行对齐布局：每行 ceil(W/64) 字 × H 行（与引擎 grid 的 wpr/rows 同构）
            const int64_t expect = ((static_cast<int64_t>(r.maxX) - r.minX + 64) / 64) *
                                   (static_cast<int64_t>(r.maxZ) - r.minZ + 1);
            if (static_cast<int64_t>(words) != expect) { err = "缓存文件位图尺寸与范围不符（已中止）。"; return false; }
            Bitmap& b = loaded.m_bitmaps[idx];
            if (!b.bits.empty()) {
                // 1 MiB 分块读取（顺带把进度推到「连续」）
                constexpr size_t kBlockBytes = 1u << 20;
                uint8_t* dst = reinterpret_cast<uint8_t*>(b.bits.data());
                const size_t totalBytes = b.bits.size() * sizeof(uint64_t);
                size_t done = 0;
                while (done < totalBytes) {
                    const size_t n = std::min(kBlockBytes, totalBytes - done);
                    in.read(reinterpret_cast<char*>(dst + done), static_cast<std::streamsize>(n));
                    if (in.gcount() != static_cast<std::streamsize>(n)) {
                        err = "缓存文件数据不完整（位图）。";
                        return false;
                    }
                    done += n;
                    reportBytes();
                }
            } else {
                reportBytes();
            }
            int64_t set = 0;
            for (uint64_t w : b.bits) set += popcount64(w);
            loaded.m_uniqueEntries += set;
            if (fileBytes <= 0) reportProgress(static_cast<int64_t>(i) + 1, static_cast<int64_t>(rangeCount));
        }
        // outlier 坐标段
        std::vector<uint64_t> outlierList;
        outlierList.reserve(static_cast<size_t>(std::min<uint64_t>(count, kMaxEntries)));
        for (uint64_t i = 0; i < count; ++i) {
            uint64_t packed = 0;
            if (!read64(packed)) { err = "缓存文件数据不完整（条目）。"; return false; }
            outlierList.push_back(packed);
        }
        std::sort(outlierList.begin(), outlierList.end());
        outlierList.erase(std::unique(outlierList.begin(), outlierList.end()), outlierList.end());
        loaded.m_outliers = std::move(outlierList);
        reportBytes(); // 走到文件末尾 → 进度到 100%
    } else {
        // v1：全部矩形先读入建图，再把坐标逐条置位到「第一个包含它的 range」；不在任何 range 内的进 outlier
        for (uint32_t i = 0; i < rangeCount; ++i) {
            Rect r{};
            if (!readRect(r)) { err = "缓存文件包含无效范围，已中止加载。"; return false; }
            loaded.ensureRange(r);
        }
        std::vector<uint64_t> outlierList;
        for (uint64_t i = 0; i < count; ++i) {
            uint64_t packed = 0;
            if (!read64(packed)) { err = "缓存文件数据不完整（条目）。"; return false; }
            if ((i & 0x3FFFF) == 0) reportProgress(static_cast<int64_t>(i), static_cast<int64_t>(count));
            const int x = unpackX(packed), z = unpackZ(packed);
            const size_t idx = loaded.findRange(x, z);
            if (idx != static_cast<size_t>(-1)) {
                if (loaded.setBit(idx, x, z)) ++loaded.m_uniqueEntries;
            } else {
                outlierList.push_back(packed);
            }
        }
        std::sort(outlierList.begin(), outlierList.end());
        outlierList.erase(std::unique(outlierList.begin(), outlierList.end()), outlierList.end());
        loaded.m_outliers = std::move(outlierList);
        reportProgress(static_cast<int64_t>(count), static_cast<int64_t>(count));
    }

    if (replace) {
        m_ranges = std::move(loaded.m_ranges);
        m_bitmaps = std::move(loaded.m_bitmaps);
        m_uniqueEntries = loaded.m_uniqueEntries;
        m_outliers = std::move(loaded.m_outliers);
    } else {
        // 合并导入（replace=false）：把载入的 range/位图/outlier 并入当前缓存
        mergeFrom(loaded);
    }
    // v3.3.8（任务 1 / ④′ 修复方案 B）：加载末尾清扫无支撑声明（跨运行兜底）
    dropHollowRanges();
    return true;
}

// v3.3.8.1（任务书 1.3）：把另一个缓存对象并入本对象（每个位置仍按「第一个包含它的 range」归属）
void CacheManager::mergeFrom(const CacheManager& other)
{
    for (size_t i = 0; i < other.m_ranges.size(); ++i) {
        ensureRange(other.m_ranges[i]);
    }
    for (size_t i = 0; i < other.m_ranges.size(); ++i) {
        const Bitmap& src = other.m_bitmaps[i];
        const Rect& r = other.m_ranges[i];
        const int64_t rowBits = src.wordsPerRow * 64;
        const int64_t w = static_cast<int64_t>(r.maxX) - r.minX + 1;
        const int64_t h = static_cast<int64_t>(r.maxZ) - r.minZ + 1;
        for (size_t wi = 0; wi < src.bits.size(); ++wi) {
            uint64_t word = src.bits[wi];
            while (word) {
                const int bit = ctz64(word);
                word &= word - 1;
                const int64_t bitIndex = static_cast<int64_t>(wi) * 64 + bit;
                const int64_t row = bitIndex / rowBits;
                const int64_t col = bitIndex % rowBits;
                if (col >= w || row >= h) continue;
                const int x = r.minX + static_cast<int>(col);
                const int z = r.minZ + static_cast<int>(row);
                const size_t target = findRange(x, z);
                if (target == static_cast<size_t>(-1)) {
                    addOutlier(x, z);
                } else if (setBit(target, x, z)) {
                    ++m_uniqueEntries;
                }
            }
        }
    }
    for (uint64_t o : other.m_outliers) addOutlier(unpackX(o), unpackZ(o));
}

bool CacheManager::saveFile(const std::filesystem::path& path, std::string& err)
{
    // v3.3.8（任务 1 / ④′ 修复方案 B）：落盘前清扫无支撑声明（声明与条目同步）
    dropHollowRanges();

    const std::filesystem::path fresh = path.string() + ".new";
    std::error_code ec;
    std::filesystem::remove(fresh, ec);
    {
        std::ofstream out(fresh, std::ios::binary | std::ios::trunc);
        if (!out) {
            err = "无法写入缓存文件：" + fresh.string();
            return false;
        }
        const uint32_t magic = kMagic, ver = kVersion2;
        const uint32_t rangeCount = static_cast<uint32_t>(m_ranges.size());
        const uint32_t reserved = 0;
        const uint64_t outlierCount = static_cast<uint64_t>(m_outliers.size());
        out.write(reinterpret_cast<const char*>(&magic), 4);
        out.write(reinterpret_cast<const char*>(&ver), 4);
        out.write(reinterpret_cast<const char*>(&rangeCount), 4);
        out.write(reinterpret_cast<const char*>(&reserved), 4);
        out.write(reinterpret_cast<const char*>(&outlierCount), 8);
        for (size_t i = 0; i < m_ranges.size(); ++i) {
            const int32_t v[4] = { m_ranges[i].minX, m_ranges[i].maxX, m_ranges[i].minZ, m_ranges[i].maxZ };
            out.write(reinterpret_cast<const char*>(v), 16);
            const uint32_t words = static_cast<uint32_t>(m_bitmaps[i].bits.size());
            out.write(reinterpret_cast<const char*>(&words), 4);
            if (!m_bitmaps[i].bits.empty()) {
                out.write(reinterpret_cast<const char*>(m_bitmaps[i].bits.data()),
                          static_cast<std::streamsize>(m_bitmaps[i].bits.size() * sizeof(uint64_t)));
            }
        }
        if (outlierCount > 0) {
            out.write(reinterpret_cast<const char*>(m_outliers.data()),
                      static_cast<std::streamsize>(m_outliers.size() * sizeof(uint64_t)));
        }
        out.flush();
        if (!out) {
            err = "写入缓存文件失败（磁盘空间不足？）：" + fresh.string();
            std::filesystem::remove(fresh, ec);
            return false;
        }
    }

    // ---- 回读校验（失败即中止，旧文件不动）----
    {
        CacheManager verify;
        std::string verr;
        if (!verify.loadFile(fresh, true, verr)) {
            err = "缓存写入后回读校验失败（已保留旧文件）：" + verr;
            std::filesystem::remove(fresh, ec);
            return false;
        }
        if (verify.ranges().size() != m_ranges.size() || verify.entryCount() != entryCount()) {
            err = "缓存写入后校验不一致（范围/条目数，已保留旧文件）。";
            std::filesystem::remove(fresh, ec);
            return false;
        }
        // 抽样比对若干坐标（每个 range 最多 16 个点）
        for (size_t i = 0; i < m_ranges.size(); ++i) {
            const Rect& r = m_ranges[i];
            const int64_t w = rectW(r), h = rectH(r);
            for (int k = 0; k < 16; ++k) {
                const int64_t x = r.minX + (w > 1 ? (static_cast<int64_t>(k) * 7919) % w : 0);
                const int64_t z = r.minZ + (h > 1 ? (static_cast<int64_t>(k) * 104729) % h : 0);
                if (verify.lookup(static_cast<int>(x), static_cast<int>(z)) !=
                    lookup(static_cast<int>(x), static_cast<int>(z))) {
                    err = "缓存写入后抽样比对不一致（已保留旧文件）。";
                    std::filesystem::remove(fresh, ec);
                    return false;
                }
            }
        }
    }

    // ---- 原子替换 ----
    if (!atomicReplace(path, fresh)) {
        err = "缓存文件替换失败（已保留旧文件）：" + path.string();
        std::filesystem::remove(fresh, ec);
        return false;
    }
    return true;
}

} // namespace slime
