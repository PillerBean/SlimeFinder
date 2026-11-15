#include "core/next/Pipeline.h"

#include "core/CacheManager.h"

#include <algorithm>
#include <chrono>
#include <mutex>

namespace slime::next {

namespace {

bool fillBitmapFromCache(const slime::CacheManager& cache, const slime::Rect& rect, Bitmap& bmp,
                         std::string* err)
{
    if (!cache.covers(rect)) return false;
    if (!bmp.init(rect.minX, rect.minZ, rect.maxX - rect.minX + 1, rect.maxZ - rect.minZ + 1)) {
        if (err) *err = "缓存位图分配失败";
        return false;
    }
    for (int z = rect.minZ; z <= rect.maxZ; ++z) {
        for (int x = rect.minX; x <= rect.maxX; ++x) {
            if (cache.isSlime(x, z)) bmp.setSlime(x, z);
        }
    }
    return true;
}

} // namespace

bool MaterializePipeline::run(const slime::SearchOptions& opt, const std::atomic<bool>& cancel,
                              MaterializeResult& out, std::string* err, const slime::ProgressFn& progress) const
{
    if (opt.useCache || opt.buildMode || opt.ignoreThreshold || opt.stopAtTarget || opt.smartRange) {
        if (err) *err = "Phase2 MaterializePipeline 只支持无缓存/无停止的经典搜索";
        return false;
    }
    if (opt.mode == slime::Mode::Density && opt.algorithm != slime::Algorithm::Exact) {
        if (err) *err = "Phase2 MaterializePipeline 只支持精确密度";
        return false;
    }
    if (opt.mode == slime::Mode::Strict && !opt.directionLocked) {
        if (err) *err = "Phase2 MaterializePipeline 暂不支持旋转图案";
        return false;
    }

    std::mutex progressMu;
    double progressLast = -1.0;
    const auto emitProgress = [&](double f, bool scanning) {
        if (!progress) return;
        std::lock_guard<std::mutex> lk(progressMu);
        if (f <= progressLast) return;
        progressLast = f;
        progress(f, scanning, 0.0, 0.0);
    };

    emitProgress(0.0, true);
    Bitmap bmp;
    ScanStats scan;
    CpuDeterminer determiner;
    slime::ProgressFn scanCb;
    if (progress) {
        scanCb = [&](double f, bool, double, double) { emitProgress(f * 0.75, true); };
    }
    if (!determiner.run(opt, cancel, bmp, scan, err, scanCb)) {
        out.cancelled = cancel.load(std::memory_order_relaxed);
        return false;
    }
    emitProgress(0.75, true);

    ResultCollector collector(CollectorKind::DistanceTopN, opt.targetCount);
    CpuExactMatcher matcher;
    MatchStats match;
    slime::ProgressFn matchCb;
    if (progress) {
        matchCb = [&](double f, bool, double, double) { emitProgress(0.75 + f * 0.25, false); };
    }
    if (!matcher.run(bmp, opt, cancel, collector, match, err, matchCb)) {
        out.cancelled = cancel.load(std::memory_order_relaxed);
        return false;
    }

    out.results = collector.results();
    out.totalFound = collector.totalFound();
    out.scan = scan;
    out.match = match;
    out.cancelled = cancel.load(std::memory_order_relaxed);
    emitProgress(1.0, false);
    return true;
}

bool MaterializePipeline::runChunked(const slime::SearchOptions& opt, int64_t rowsPerChunk,
                                     const std::atomic<bool>& cancel, MaterializeResult& out,
                                     std::string* err, const slime::ProgressFn& progress) const
{
    if (opt.useCache || opt.buildMode || opt.ignoreThreshold || opt.stopAtTarget || opt.smartRange) {
        if (err) *err = "Phase3 runChunked 只支持无缓存/无停止的经典搜索";
        return false;
    }
    if (opt.mode == slime::Mode::Density && opt.algorithm != slime::Algorithm::Exact) {
        if (err) *err = "Phase3 runChunked 只支持精确密度";
        return false;
    }
    if (opt.mode == slime::Mode::Strict && !opt.directionLocked) {
        if (err) *err = "Phase3 runChunked 暂不支持旋转图案";
        return false;
    }
    const int height = (opt.mode == slime::Mode::Strict) ? opt.pattern.rows() : opt.gridSize;
    if (height <= 0) {
        if (err) *err = "匹配窗口高度非法";
        return false;
    }
    const int64_t azMin = opt.minZ;
    const int64_t azMax = static_cast<int64_t>(opt.maxZ) - height + 1;
    ResultCollector all(CollectorKind::DistanceTopN, opt.targetCount);
    if (azMax < azMin) {
        out.totalFound = 0;
        out.scan.chunks = 0;
        if (progress) progress(1.0, false, 0.0, 0.0);
        return true;
    }
    rowsPerChunk = (std::max)(int64_t{ 1 }, rowsPerChunk);
    const int64_t totalRows = azMax - azMin + 1;
    MatchStats matchTotal;
    ScanStats scanTotal;
    if (progress) progress(0.0, true, 0.0, 0.0);

    for (int64_t a0 = azMin; a0 <= azMax; a0 += rowsPerChunk) {
        if (cancel.load(std::memory_order_relaxed)) {
            out.cancelled = true;
            return false;
        }
        const int64_t a1 = (std::min)(azMax, a0 + rowsPerChunk - 1);
        slime::SearchOptions local = opt;
        local.minZ = a0;
        local.maxZ = a1 + height - 1;

        Bitmap bmp;
        ScanStats scan;
        CpuDeterminer determiner;
        if (!determiner.run(local, cancel, bmp, scan, err)) {
            out.cancelled = cancel.load(std::memory_order_relaxed);
            return false;
        }
        ResultCollector chunk(CollectorKind::DistanceTopN, opt.targetCount);
        CpuExactMatcher matcher;
        MatchStats match;
        MatchLimits limits;
        limits.zLimited = true;
        limits.azMin = a0;
        limits.azMax = a1;
        limits.rankXMin = opt.minX;
        limits.rankZMin = opt.minZ;
        limits.rankAzCount = totalRows;
        if (!matcher.run(bmp, opt, cancel, chunk, match, err, slime::ProgressFn(), limits)) {
            out.cancelled = cancel.load(std::memory_order_relaxed);
            return false;
        }
        all.merge(chunk);
        scanTotal.ms += scan.ms;
        scanTotal.chunks += scan.chunks;
        scanTotal.gpuUsed = false;
        scanTotal.backend = "cpu-chunked";
        matchTotal.ms += match.ms;
        matchTotal.windows += match.windows;
        const int64_t doneRows = a1 - azMin + 1;
        if (progress) {
            progress(static_cast<double>(doneRows) / static_cast<double>(totalRows), true, 0.0, 0.0);
        }
    }

    out.results = all.results();
    out.totalFound = all.totalFound();
    out.scan = scanTotal;
    out.match = matchTotal;
    out.cancelled = false;
    if (progress) progress(1.0, false, 0.0, 0.0);
    return true;
}

bool fillBitmapPartial(const slime::CacheManager& cache, const slime::Rect& rect,
                         slime::Version version, int64_t seed, const std::atomic<bool>& cancel,
                         Bitmap& bmp, std::string* err)
{
    if (!cache.hasData()) return false;
    if (!bmp.init(rect.minX, rect.minZ, rect.maxX - rect.minX + 1, rect.maxZ - rect.minZ + 1)) {
        if (err) *err = "缓存位图分配失败";
        return false;
    }
    for (int z = rect.minZ; z <= rect.maxZ; ++z) {
        if (cancel.load(std::memory_order_relaxed)) return false;
        for (int x = rect.minX; x <= rect.maxX; ++x) {
            const int cached = cache.lookup(x, z);
            const bool slime = cached >= 0 ? cached != 0
                                           : slime::SlimeEngine::isSlimeChunk(version, seed, x, z);
            if (slime) bmp.setSlime(x, z);
        }
    }
    return true;
}

bool MaterializePipeline::runBanded(const slime::SearchOptions& opt, const std::atomic<bool>& cancel,
                                    MaterializeResult& out, std::string* err,
                                    const slime::ProgressFn& progress) const
{
    if (!opt.cacheBandLoader) return run(opt, cancel, out, err, progress);
    if (opt.buildMode || opt.ignoreThreshold || opt.stopAtTarget || opt.smartRange) {
        if (err) *err = "Phase3 runBanded 只支持无构建/无停止的搜索";
        return false;
    }
    if (opt.mode == slime::Mode::Density && opt.algorithm != slime::Algorithm::Exact) {
        if (err) *err = "Phase3 runBanded 只支持精确密度";
        return false;
    }
    if (opt.mode == slime::Mode::Strict && !opt.directionLocked) {
        if (err) *err = "Phase3 runBanded 暂不支持旋转图案";
        return false;
    }
    const int width = (opt.mode == slime::Mode::Strict) ? opt.pattern.cols() : opt.gridSize;
    const int height = (opt.mode == slime::Mode::Strict) ? opt.pattern.rows() : opt.gridSize;
    if (width <= 0 || height <= 0) {
        if (err) *err = "匹配窗口尺寸非法";
        return false;
    }
    const int64_t axMin = opt.minX;
    const int64_t axMax = static_cast<int64_t>(opt.maxX) - width + 1;
    const int64_t azMin = opt.minZ;
    const int64_t azMax = static_cast<int64_t>(opt.maxZ) - height + 1;
    ResultCollector all(CollectorKind::DistanceTopN, opt.targetCount);
    if (axMax < axMin || azMax < azMin) {
        out.totalFound = 0;
        if (progress) progress(1.0, false, 0.0, 0.0);
        return true;
    }
    const int64_t band = (std::max)(int64_t{ 16 }, static_cast<int64_t>(opt.cacheBandSize));
    const int64_t totalRows = azMax - azMin + 1;
    const int64_t tilesX = (axMax - axMin + band) / band;
    const int64_t tilesZ = (azMax - azMin + band) / band;
    const int64_t totalTiles = tilesX * tilesZ;
    int64_t doneTiles = 0;
    ScanStats scanTotal;
    MatchStats matchTotal;
    if (progress) progress(0.0, true, 0.0, 0.0);

    for (int64_t ax0 = axMin; ax0 <= axMax; ax0 += band) {
        const int64_t ax1 = (std::min)(axMax, ax0 + band - 1);
        for (int64_t az0 = azMin; az0 <= azMax; az0 += band) {
            if (cancel.load(std::memory_order_relaxed)) {
                out.cancelled = true;
                return false;
            }
            const int64_t az1 = (std::min)(azMax, az0 + band - 1);
            const slime::Rect dataRect{ static_cast<int>(ax0), static_cast<int>(ax1 + width - 1),
                                        static_cast<int>(az0), static_cast<int>(az1 + height - 1) };
            slime::SearchOptions local = opt;
            local.minX = ax0;
            local.maxX = ax1 + width - 1;
            local.minZ = az0;
            local.maxZ = az1 + height - 1;
            local.cacheBandLoader = nullptr;
            local.cacheBandMayCover = nullptr;
            local.cache = nullptr;
            local.cacheReadMu = nullptr;

            Bitmap bmp;
            ScanStats scan;
            const auto t0 = std::chrono::steady_clock::now();
            std::shared_ptr<slime::CacheManager> cache = opt.cacheBandLoader(dataRect);
            bool fromCache = false;
            bool partial = false;
            if (cache) {
                fromCache = fillBitmapFromCache(*cache, dataRect, bmp, err);
                if (!fromCache && cache->hasData()) {
                    partial = fillBitmapPartial(*cache, dataRect, opt.version, opt.worldSeed, cancel, bmp, err);
                    if (!partial && cancel.load(std::memory_order_relaxed)) {
                        out.cancelled = true;
                        return false;
                    }
                }
            }
            if (!fromCache && !partial) {
                CpuDeterminer determiner;
                if (!determiner.run(local, cancel, bmp, scan, err)) {
                    out.cancelled = cancel.load(std::memory_order_relaxed);
                    return false;
                }
            } else {
                scan.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
                scan.chunks = static_cast<int64_t>(dataRect.area());
                scan.gpuUsed = false;
            }

            ResultCollector tile(CollectorKind::DistanceTopN, opt.targetCount);
            CpuExactMatcher matcher;
            MatchStats match;
            MatchLimits limits;
            limits.xLimited = true;
            limits.axMin = ax0;
            limits.axMax = ax1;
            limits.zLimited = true;
            limits.azMin = az0;
            limits.azMax = az1;
            limits.rankXMin = opt.minX;
            limits.rankZMin = opt.minZ;
            limits.rankAzCount = totalRows;
            if (!matcher.run(bmp, opt, cancel, tile, match, err, slime::ProgressFn(), limits)) {
                out.cancelled = cancel.load(std::memory_order_relaxed);
                return false;
            }
            all.merge(tile);
            scanTotal.ms += scan.ms;
            scanTotal.chunks += scan.chunks;
            scanTotal.backend = fromCache ? "cache+banded"
                                  : partial ? "cache-partial+banded"
                                            : "cpu-banded";
            scanTotal.gpuUsed = false;
            matchTotal.ms += match.ms;
            matchTotal.windows += match.windows;
            ++doneTiles;
            if (progress) {
                progress(static_cast<double>(doneTiles) / static_cast<double>(totalTiles), true, 0.0, 0.0);
            }
        }
    }

    out.results = all.results();
    out.totalFound = all.totalFound();
    out.scan = scanTotal;
    out.match = matchTotal;
    out.cancelled = false;
    if (progress) progress(1.0, false, 0.0, 0.0);
    return true;
}

} // namespace slime::next

