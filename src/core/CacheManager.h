// CacheManager.h - 基岩版史莱姆区块缓存管理（Qt 无关，二进制格式）
//
// 缓存语义：
//   * 缓存 = 一组“已完整扫描”的矩形区域(ranges) + 每个区域内的史莱姆区块**位图**；
//   * 对任意落在某 range 内的区块：是史莱姆 ⟺ 该 range 位图对应位 = 1；
//   * 查询区域被某个 range 完全覆盖时可跳过计算直接出结果。
//
// v3.3.8（任务 3+5 合流：位图化）—— 内存与磁盘都由「坐标集合 + 二分」改为「按 range 位图 + 位测试」：
//   * 判定成本 523 ns/点（二分 ≈27 次随机访存）→ 2~5 ns/点（1 次位测试）；
//   * 体积 ≈ Σ(面积/8) 字节（1.02 GB 缓存 → 229 MiB，4.3× 更小）、加载更快；
//   * 位图与引擎分块几何同形（209715×762）⇒ 后续可直接当 grid 用（任务 3+5 第 3 步，见设计文档）。
//
// 磁盘格式：
//   v1（历史，仍可读；读入后立即转位图）：
//     u32 魔数 0x534C4D45("SLME") | u32 版本=1 | u32 rangeCount | u64 entryCount
//     i32×4 × rangeCount（minX maxX minZ maxZ） | u64 × entryCount（(X<<32)|(Z&0xFFFFFFFF)）
//   v2（v3.3.8 起写入）：
//     u32 魔数 0x534C4D45("SLME") | u32 版本=2 | u32 rangeCount | u32 保留=0 | u64 outlierCount
//     rangeCount × { i32×4 rect | u32 bitmapWords | u64 × bitmapWords }   （行优先，位序 = (z-minZ)*W + (x-minX)）
//     u64 × outlierCount                                                  （不在任何 range 内的条目，升序去重）
//   落盘采用**原子替换**：先写 <path>.new → 回读校验 → ReplaceFileW（失败退 MoveFileExW）；
//   任一步失败都保留旧文件并返回错误（不静默丢缓存）。
#pragma once

#include <cstdint>
#include <functional>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

#include "SlimeEngine.h" // Rect 完整定义

namespace slime {

class CacheManager {
public:
    CacheManager() = default;
    ~CacheManager() = default;

    // ---- 内存状态 ----
    void clear() noexcept;
    bool hasData() const noexcept { return !m_ranges.empty(); }
    const std::vector<Rect>& ranges() const noexcept { return m_ranges; }
    // 去重后的史莱姆区块总数（= 各 range 位图中「归属本 range」的置位数 + 不在任何 range 内的条目数）
    int64_t entryCount() const noexcept { return m_uniqueEntries + static_cast<int64_t>(m_outliers.size()); }
    // 位图总字节数（v3.3.8：v2 体积的实际构成，供日志/验收对比）
    int64_t bitmapBytes() const noexcept;

    // 是否已被任何 range 完整覆盖（查询区域整体命中缓存）
    bool covers(const Rect& region) const noexcept;

    // 区块判定：仅当坐标落在某个 range 内时有效（调用前请确认命中）
    bool isSlime(int x, int z) const noexcept;
    // 三角判定：-1=不在缓存覆盖内，0=已知非史莱姆，1=已知史莱姆
    int lookup(int x, int z) const noexcept;

    // 合并一次完整扫描的结果：新 range + 该 range 内扫描出的史莱姆坐标
    // （该 range 内被缓存覆盖的区块不重复加入；调用方保证 range 内每个区块都已有确定值）
    void mergeRegion(const Rect& region, const std::vector<std::pair<int, int>>& slimeChunks);

    // 合并任意 range/条目（导入用；自动去重）
    void mergeRaw(const std::vector<Rect>& ranges, const std::vector<std::pair<int, int>>& slimeChunks);

    // v3.12.3：直接合并按行主序打包的位图，避免展开成 pair<int,int>。
    bool addPackedBitmap(const Rect& region, const uint8_t* bits, size_t bytes);
    // 直接写入已按 CacheManager 行宽打包的位图（每行 wordsPerRow 个字）。
    bool addPackedWords(const Rect& region, const uint64_t* words, size_t wordCount);

    // ---- 磁盘 IO ----
    // replace=true：整文件替换内存内容（启动加载）；false：与内存合并（导入）
    // v1 / v2 都能读；本版本保存时写 v2（原子替换）。
    // v3.3.8.1（任务书 1.3）：可选进度回调（done/total；**可能在工作线程内调用**，回调须线程安全）
    //   v2 文件按「已读字节 / 文件字节」上报（位图 1 MiB 分块读 ⇒ 进度连续）；
    //   v1 文件按「已读条目 / 总条目」上报。
    bool loadFile(const std::filesystem::path& path, bool replace, std::string& err,
                  const std::function<void(int64_t, int64_t)>& onProgress = {});
    // v3.3.8.1（任务书 1.3）：把另一个缓存对象的内容并入本对象（后台线程读完后在 UI 线程合并）
    void mergeFrom(const CacheManager& other);
    // v3.3.8（任务 1 / ④′ 修复方案 B）：saveFile 不再是 const —— 落盘前会先清扫空壳声明
    //   （dropHollowRanges），保证「声明与条目」在文件里也严格同步。
    bool saveFile(const std::filesystem::path& path, std::string& err);

    // ---- v3.1.0：容量控制 ----
    // 估算当前缓存的二进制体积（字节，v2 口径）
    int64_t sizeBytes() const noexcept;
    // 丢弃最旧 range 及其专属条目，直至体积不超过 maxBytes（幂等；至少保留 1 个 range）
    void pruneToFit(int64_t maxBytes) noexcept;

    // ---- v3.3.8（任务 1 / ④′ 修复方案 B）：无支撑声明的清扫 ----
    // 删除「条目支撑不足」的 range 声明（声明与条目同步），返回剔除的 range 数。
    // 判据（用户 2026-09-21 拍板）：声明内条目数 ≥ 期望条目数 × 50%，期望 = 面积 × 基准密度 10%；
    //   期望 < 100 条的小面积声明不做判断（一律保留）。
    // 位图化后判据变成**一次 popcount**（精确、无 O(N×R) 遍历）。
    // 语义：只删声明（位图随声明一起释放）；被剔除的区域下次搜索需重扫（正确性优先）。
    // 调用点：loadFile() 末尾 + saveFile() 落盘前各一次。
    int64_t dropHollowRanges() noexcept;

    // ---- v3.3.8（任务 3+5 第 3 步预留）：位图只读访问（引擎可直接当 grid 用）----
    // 返回 range idx 的位图字数组与每行字数；idx 越界/未建图返回 nullptr / 0
    const uint64_t* rangeBitmap(size_t idx, int64_t* wordsPerRow = nullptr, int64_t* rows = nullptr) const noexcept;

private:
    struct Bitmap {
        int64_t wordsPerRow = 0; // = ceil(W/64)
        int64_t rows = 0;        // = H
        std::vector<uint64_t> bits;
    };

    // 找到第一个包含 (x,z) 的 range 下标；无则返回 -1
    size_t findRange(int x, int z) const noexcept;
    // 在 idx 的位图上置位；返回是否为「新置位」（首次置位）
    bool setBit(size_t idx, int x, int z) noexcept;
    bool testBit(size_t idx, int x, int z) const noexcept;
    // 新建/查找 range（相同矩形视为同一个）；返回下标
    size_t ensureRange(const Rect& r);
    // 把「不被任何 range 覆盖」的条目并入 m_outliers（升序去重；返回新增数）
    int64_t addOutlier(int x, int z);
    // 删除 range idx：其位图中仍被其它 range 覆盖的位先「改挂」过去，再释放
    void removeRange(size_t idx) noexcept;

    std::vector<Rect> m_ranges;
    std::vector<Bitmap> m_bitmaps;      // 与 m_ranges 一一对应
    int64_t m_uniqueEntries = 0;        // 各 range 位图中「归属本 range」的置位总数
    std::vector<uint64_t> m_outliers;   // 升序去重；不在任何 range 内的条目
};

} // namespace slime
