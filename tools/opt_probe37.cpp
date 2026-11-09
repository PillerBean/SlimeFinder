// opt_probe37.cpp - 搜索算法一致性 fuzz：保守(Brute) 与 分层(Layered, ratio<=1) 必须与精确(Exact) 一致
#include "core/SlimeEngine.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <random>
#include <string>
#include <vector>

namespace {
struct Res {
    bool ok = false;
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
    r.ok = out.ok;
    r.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    for (const auto& m : out.results) r.pts.emplace_back(m.minX, m.minZ);
    return r;
}

slime::SearchOptions mk(int w, int h, int g, double thr, int N, slime::Algorithm alg, int layerBlock,
                        double layerRatio, bool strict, unsigned patSeed)
{
    slime::SearchOptions o;
    o.version = (patSeed % 5 == 0) ? slime::Version::Bedrock : slime::Version::Java;
    o.worldSeed = (o.version == slime::Version::Java) ? 12345 : 0;
    o.minX = -w / 2;
    o.maxX = o.minX + w - 1;
    o.minZ = -h / 2;
    o.maxZ = o.minZ + h - 1;
    o.mode = strict ? slime::Mode::Strict : slime::Mode::Density;
    if (strict) {
        const int pr = 1 + static_cast<int>(patSeed % 3);
        const int pc = 1 + static_cast<int>((patSeed / 5) % 3);
        o.pattern = decltype(o.pattern)(pr, pc);
        for (int r = 0; r < pr; ++r)
            for (int c = 0; c < pc; ++c) {
                const bool green = ((patSeed >> ((r * 4 + c) % 16)) & 1u) != 0;
                o.pattern.set(r, c, green ? slime::CellState::Green : slime::CellState::Ignore);
            }
        o.pattern.set(0, 0, slime::CellState::Green);
    } else {
        o.gridSize = g;
        o.minDensity = thr;
    }
    o.targetCount = N;
    o.threads = 12;
    o.algorithm = alg;
    o.layerBlock = layerBlock;
    o.layerRatio = layerRatio;
    return o;
}
} // namespace

int main()
{
    std::mt19937 rng(424242u);
    int bad = 0, cases = 0, estDiff = 0, estTotal = 0;
    for (int iter = 0; iter < 40; ++iter) {
        const bool strict = (iter % 4) == 3;
        const int w = 300 + static_cast<int>(rng() % 2000);
        const int h = 300 + static_cast<int>(rng() % 2000);
        const int g = 2 + static_cast<int>(rng() % 6);
        const double thr = 0.2 + 0.1 * static_cast<double>(rng() % 7);
        const int N = 2 + static_cast<int>(rng() % 20);
        const unsigned patSeed = static_cast<unsigned>(rng());
        const int lb = 8 << static_cast<int>(rng() % 4);   // 8/16/32/64
        const Res exact = run(mk(w, h, g, thr, N, slime::Algorithm::Exact, lb, 1.0, strict, patSeed));
        const Res brute = run(mk(w, h, g, thr, N, slime::Algorithm::Brute, lb, 1.0, strict, patSeed));
        const Res layered = run(mk(w, h, g, thr, N, slime::Algorithm::Layered, lb, 1.0, strict, patSeed));
        const Res est = run(mk(w, h, g, thr, N, slime::Algorithm::Sample, lb, 0.5, strict, patSeed));
        ++cases;
        bool ok = true;
        if (!(brute.ok == exact.ok && brute.pts == exact.pts)) {
            ok = false;
            std::printf("[DIFF brute] %dx%d g=%d thr=%.1f N=%d%s：exact %zu / brute %zu\n", w, h, g, thr, N,
                        strict ? " strict" : "", exact.pts.size(), brute.pts.size());
        }
        if (!(layered.ok == exact.ok && layered.pts == exact.pts)) {
            ok = false;
            std::printf("[DIFF layered] %dx%d g=%d thr=%.1f N=%d lb=%d%s：exact %zu / layered %zu\n", w, h, g, thr, N,
                        lb, strict ? " strict" : "", exact.pts.size(), layered.pts.size());
        }
        if (!strict && exact.pts != est.pts) ++estDiff;
        if (!strict) ++estTotal;
        if (!ok) ++bad;
        if (!ok || iter % 10 == 0) {
            std::printf("case %2d %4dx%-4d g=%d thr=%.1f N=%-2d lb=%-2d | exact %6.2f brute %6.2f layered %6.2f est %6.2f | %s\n",
                        iter, w, h, g, thr, N, lb, exact.ms, brute.ms, layered.ms, est.ms, ok ? "OK" : "**DIFF**");
        }
    }
    std::printf("\n共 %d 例：不一致 %d 例；估算模式与精确不同 %d/%d（设计如此）-> %s\n", cases, bad, estDiff,
                estTotal, bad == 0 ? "全部一致" : "存在差异");
    return bad == 0 ? 0 : 1;
}
