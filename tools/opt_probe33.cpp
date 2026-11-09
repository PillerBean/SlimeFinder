// opt_probe33.cpp - 随机 A/B fuzz（升级版）：全量 / 智能范围 / 提前停止 / 两者组合 必须一致
//   覆盖：Java+基岩、密度+严格图案（随机图案，含 White/Ignore）、边界阈值 0.0/1.0、
//         目标数 1~25、随机区域大小（含触发分块路径的大区域）。
#include "core/SlimeEngine.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <random>
#include <string>
#include <vector>

namespace {
struct Res {
    std::vector<std::pair<int, int>> pts;
    double ms = 0;
};

Res run(const slime::SearchOptions& o)
{
    slime::SlimeEngine eng;
    slime::SearchOutcome out;
    std::atomic<bool> cancel{ false };
    slime::ProgressFn noop;
    const auto t0 = std::chrono::steady_clock::now();
    eng.run(o, cancel, noop, out);
    Res r;
    r.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    for (const auto& m : out.results) r.pts.emplace_back(m.minX, m.minZ);
    return r;
}

slime::SearchOptions makeOpt(int w, int h, bool stop, bool smart, bool strict, int g, double thr, int N,
                             slime::Version ver, unsigned patSeed, int patKind)
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
        const int pr = 1 + static_cast<int>(patSeed % 4);
        const int pc = 1 + static_cast<int>((patSeed / 7) % 4);
        o.pattern = decltype(o.pattern)(pr, pc);
        unsigned x = patSeed * 2654435761u;
        for (int r = 0; r < pr; ++r) {
            for (int c = 0; c < pc; ++c) {
                x = x * 1103515245u + 12345u;
                const int kind = static_cast<int>((x >> 16) % 100);
                slime::CellState st = slime::CellState::Green;
                if (patKind == 1) st = (kind < 55) ? slime::CellState::Green : slime::CellState::Ignore;
                else if (patKind == 2)
                    st = (kind < 45) ? slime::CellState::Green
                                    : (kind < 80 ? slime::CellState::White : slime::CellState::Ignore);
                o.pattern.set(r, c, st);
            }
        }
        // 全灰图案非法（引擎会拒绝），保证至少一个生效格
        o.pattern.set(0, 0, slime::CellState::Green);
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
    return o;
}
} // namespace

int main()
{
    std::mt19937 rng(20261108u);
    int cases = 0, bad = 0;
    for (int iter = 0; iter < 120; ++iter) {
        const bool strict = (iter % 3) == 2;
        const slime::Version ver = (iter % 5 == 0) ? slime::Version::Bedrock : slime::Version::Java;
        const int w = 200 + static_cast<int>(rng() % 4000);
        const int h = 200 + static_cast<int>(rng() % 4000);
        const int g = 1 + static_cast<int>(rng() % 8);
        const int thrPick = static_cast<int>(rng() % 4);
        const double thr =
            (thrPick == 0) ? 0.0 : (thrPick == 1 ? 1.0 : 0.2 + 0.1 * static_cast<double>(rng() % 8));
        const int N = 1 + static_cast<int>(rng() % 25);
        const unsigned patSeed = static_cast<unsigned>(rng());
        const int patKind = static_cast<int>(rng() % 3);
        const Res full = run(makeOpt(w, h, false, false, strict, g, thr, N, ver, patSeed, patKind));
        const Res smart = run(makeOpt(w, h, false, true, strict, g, thr, N, ver, patSeed, patKind));
        const Res stop = run(makeOpt(w, h, true, false, strict, g, thr, N, ver, patSeed, patKind));
        const Res both = run(makeOpt(w, h, true, true, strict, g, thr, N, ver, patSeed, patKind));
        ++cases;
        bool ok = true;
        const char* vn = (ver == slime::Version::Java) ? "java" : "bedrock";
        const char* mn = strict ? "strict" : "density";
        if (smart.pts != full.pts) {
            ok = false;
            std::printf("[DIFF smart] %s %s %dx%d g=%d thr=%.1f N=%d: full %zu / smart %zu\n", vn, mn, w, h, g,
                        thr, N, full.pts.size(), smart.pts.size());
        }
        if (stop.pts.size() > full.pts.size() ||
            !std::equal(stop.pts.begin(), stop.pts.end(), full.pts.begin())) {
            ok = false;
            std::printf("[DIFF stop] %s %s %dx%d g=%d thr=%.1f N=%d: full %zu / stop %zu\n", vn, mn, w, h, g,
                        thr, N, full.pts.size(), stop.pts.size());
        }
        if (both.pts.size() > full.pts.size() ||
            !std::equal(both.pts.begin(), both.pts.end(), full.pts.begin())) {
            ok = false;
            std::printf("[DIFF both] %s %s %dx%d g=%d thr=%.1f N=%d: full %zu / both %zu\n", vn, mn, w, h, g,
                        thr, N, full.pts.size(), both.pts.size());
        }
        if (!ok) ++bad;
        if (!ok || iter % 20 == 0) {
            std::printf("case %3d %-7s %-7s %4dx%-4d g=%d thr=%.1f N=%-2d | full %7.2f smart %7.2f stop %7.2f both %7.2f | %s\n",
                        iter, vn, mn, w, h, g, thr, N, full.ms, smart.ms, stop.ms, both.ms, ok ? "OK" : "**DIFF**");
        }
    }
    std::printf("\n共 %d 例，不一致 %d 例 -> %s\n", cases, bad, bad == 0 ? "全部一致" : "存在差异");
    return bad == 0 ? 0 : 1;
}
