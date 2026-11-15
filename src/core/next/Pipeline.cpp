#include "core/next/Pipeline.h"

#include <mutex>

namespace slime::next {

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

} // namespace slime::next
