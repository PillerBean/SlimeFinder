// opt_probe32.cpp - 方案 A 的验证：早停（stopAtTarget）与智能范围（smartRange）是否真的生效
//   V1: 早停开启后扫描区块数下降？
//   V2: 早停结果 == 不早停的前 N 名？
//   V3: 智能范围开启后扫描区块数下降？
//   V4: 两者组合是否一致且更省？
#include "core/SlimeEngine.h"

#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

namespace {
using Clock = std::chrono::steady_clock;

struct R {
    double ms = 0;
    int64_t scanned = 0;
    int64_t total = 0;
    size_t n = 0;
    int64_t found = 0;
    double scanMs = 0, matchMs = 0;
    std::vector<std::pair<int, int>> pts;
};

R run(bool stop, bool smart, slime::Version v, int w, int h, int target)
{
    slime::SearchOptions o;
    o.version = v;
    o.worldSeed = 12345;
    o.minX = -w / 2;
    o.maxX = o.minX + w - 1;
    o.minZ = -h / 2;
    o.maxZ = o.minZ + h - 1;
    o.mode = slime::Mode::Density;
    o.gridSize = 5;
    o.minDensity = 0.5;
    o.targetCount = target;
    o.stopAtTarget = stop;
    o.smartRange = smart;
    o.threads = 12;
    o.startX = 0;
    o.startZ = 0;
    slime::SlimeEngine eng;
    slime::SearchOutcome out;
    std::atomic<bool> cancel{ false };
    slime::ProgressFn noop;
    const auto t0 = Clock::now();
    eng.run(o, cancel, noop, out);
    R r;
    r.ms = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
    r.scanned = out.stats.scannedChunks;
    r.total = static_cast<int64_t>(w) * h;
    r.n = out.results.size();
    r.found = out.totalFound;
    r.scanMs = out.stats.scanMs;
    r.matchMs = out.stats.matchMs;
    for (const auto& m : out.results) r.pts.emplace_back(m.minX, m.minZ);
    return r;
}

void report(const char* tag, const R& r, const R* baseline)
{
    const double pct = r.total > 0 ? 100.0 * static_cast<double>(r.scanned) / static_cast<double>(r.total) : 0.0;
    std::printf("%-26s %8.2f ms | 扫描 %lld / %lld（%.1f%%）| 命中 %lld 显示 %zu", tag, r.ms,
                static_cast<long long>(r.scanned), static_cast<long long>(r.total), pct,
                static_cast<long long>(r.found), r.n);
    if (baseline) {
        const bool same = (r.pts == baseline->pts);  // 只比前 N 名（早停的 totalFound 天然更少）
        std::printf(" | 扫描段 %.1f ms / 匹配段 %.1f ms | 与全量一致=%s", r.scanMs, r.matchMs, same ? "是" : "否");
    }
    std::printf("\n");
}
} // namespace

int main()
{
    const int W = 8192, H = 8192;   // 6710 万区块，足够大，早停才有空间
    const int target = 8;
    for (auto ver : { slime::Version::Java, slime::Version::Bedrock }) {
        const char* name = (ver == slime::Version::Java) ? "Java" : "基岩";
        std::printf("=== %s %d² 密度 g=5 thr=0.5 target=%d ===\n", name, W, target);
        const R full = run(false, false, ver, W, H, target);
        report("全量（无早停）", full, nullptr);
        // 环形扫描潜力：第 N 名结果到起点的距离 ⇒ 只需扫 pi*r^2 的面积
        if (!full.pts.empty()) {
            const auto& last = full.pts.back();
            const double r = std::hypot(static_cast<double>(last.first), static_cast<double>(last.second));
            const double ringArea = 3.14159265 * r * r;
            std::printf("  第 N 名距起点 %.0f 区块 => 环形扫描只需 %.1f%% 的面积\n", r,
                        100.0 * ringArea / static_cast<double>(full.total));
        }
        const R stop = run(true, false, ver, W, H, target);
        report("早停 stopAtTarget", stop, &full);
        const R smart = run(false, true, ver, W, H, target);
        report("智能范围 smartRange", smart, &full);
        const R both = run(true, true, ver, W, H, target);
        report("早停+智能范围", both, &full);
        std::printf("\n");
    }
    return 0;
}
