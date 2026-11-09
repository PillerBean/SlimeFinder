// opt_probe34.cpp - 大区域（分块路径）一致性 + 早停有效性
//   1) 分块路径下 full / smart / stop / both 结果必须一致；
//   2) 早停在大区域是否真的省时间（若 stop 与 full 同速，说明分块路径没有早停）。
#include "core/SlimeEngine.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

namespace {
struct Res {
    std::vector<std::pair<int, int>> pts;
    double ms = 0;
    int64_t scanned = 0;
};

Res run(bool stop, bool smart, slime::Version ver, int w, int h, int N, int g, double thr, bool strict)
{
    slime::SearchOptions o;
    o.version = ver;
    o.worldSeed = (ver == slime::Version::Java) ? 12345 : 0;
    o.minX = -w / 2;
    o.maxX = o.minX + w - 1;
    o.minZ = -h / 2;
    o.maxZ = o.minZ + h - 1;
    o.mode = strict ? slime::Mode::Strict : slime::Mode::Density;
    if (strict) {
        o.pattern = decltype(o.pattern)(g, g);
        for (int r = 0; r < g; ++r)
            for (int c = 0; c < g; ++c) o.pattern.set(r, c, slime::CellState::Green);
    } else {
        o.gridSize = g;
        o.minDensity = thr;
    }
    o.targetCount = N;
    o.stopAtTarget = stop;
    o.smartRange = smart;
    o.threads = 12;
    o.startX = 0;
    o.startZ = 0;
    slime::SlimeEngine eng;
    slime::SearchOutcome out;
    std::atomic<bool> cancel{ false };
    slime::ProgressFn noop;
    const auto t0 = std::chrono::steady_clock::now();
    eng.run(o, cancel, noop, out);
    Res r;
    r.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    r.scanned = out.stats.scannedChunks;
    for (const auto& m : out.results) r.pts.emplace_back(m.minX, m.minZ);
    return r;
}
} // namespace

int main(int argc, char** argv)
{
    const int N = argc > 1 ? std::atoi(argv[1]) : 5;
    struct Case {
        int w, h;
        int g;
        double thr;
        bool strict;
        bool bedrock;
    };
    const Case cases[] = {
        { 8192, 8192, 5, 0.3, false, false },
        { 12000, 12000, 5, 0.3, false, false },
        { 16384, 16384, 5, 0.3, false, false },
        { 8192, 8192, 2, 0.5, true, false },
        { 12000, 12000, 2, 0.5, true, false },
        { 8192, 8192, 4, 0.4, false, true },
        { 12000, 12000, 4, 0.4, false, true },
    };
    int bad = 0;
    for (const Case& c : cases) {
        const auto ver = c.bedrock ? slime::Version::Bedrock : slime::Version::Java;
        const Res full = run(false, false, ver, c.w, c.h, N, c.g, c.thr, c.strict);
        const Res smart = run(false, true, ver, c.w, c.h, N, c.g, c.thr, c.strict);
        const Res stop = run(true, false, ver, c.w, c.h, N, c.g, c.thr, c.strict);
        const Res both = run(true, true, ver, c.w, c.h, N, c.g, c.thr, c.strict);
        bool ok = true;
        if (smart.pts != full.pts) ok = false;
        if (stop.pts.size() > full.pts.size() || !std::equal(stop.pts.begin(), stop.pts.end(), full.pts.begin()))
            ok = false;
        if (both.pts.size() > full.pts.size() || !std::equal(both.pts.begin(), both.pts.end(), full.pts.begin()))
            ok = false;
        if (!ok) ++bad;
        std::printf("%s %-7s %5dx%-5d g=%d thr=%.1f N=%d | full %7.1f smart %7.1f stop %7.1f both %7.1f | 扫描 %lld | %s\n",
                    c.bedrock ? "bedrock" : "java", c.strict ? "strict" : "density", c.w, c.h, c.g, c.thr, N,
                    full.ms, smart.ms, stop.ms, both.ms, static_cast<long long>(full.scanned), ok ? "OK" : "**DIFF**");
        std::fflush(stdout);
    }
    std::printf("\n不一致 %d 例 -> %s\n", bad, bad == 0 ? "全部一致" : "存在差异");
    return bad == 0 ? 0 : 1;
}
