#include "core/next/Pipeline.h"

namespace slime::next {

bool MaterializePipeline::run(const slime::SearchOptions& opt, const std::atomic<bool>& cancel,
                              MaterializeResult& out, std::string* err) const
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

    Bitmap bmp;
    ScanStats scan;
    CpuDeterminer determiner;
    if (!determiner.run(opt, cancel, bmp, scan, err)) return false;

    ResultCollector collector(CollectorKind::DistanceTopN, opt.targetCount);
    CpuExactMatcher matcher;
    MatchStats match;
    if (!matcher.run(bmp, opt, cancel, collector, match, err)) return false;

    out.results = collector.results();
    out.totalFound = collector.totalFound();
    out.scan = scan;
    out.match = match;
    return true;
}

} // namespace slime::next
