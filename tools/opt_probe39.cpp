// opt_probe39.cpp - 线程数一致性 + 非法参数健壮性
#include "core/SlimeEngine.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <random>
#include <string>
#include <vector>

namespace {
struct Out {
    bool ok = false;
    std::string err;
    std::vector<std::pair<int, int>> pts;
};

Out run(const slime::SearchOptions& o)
{
    slime::SlimeEngine eng;
    slime::SearchOutcome out;
    std::atomic<bool> cancel{ false };
    slime::ProgressFn noop;
    eng.run(o, cancel, noop, out);
    Out r;
    r.ok = out.ok;
    r.err = out.error;
    for (const auto& m : out.results) r.pts.emplace_back(m.minX, m.minZ);
    return r;
}

slime::SearchOptions mk(int w, int h, int g, double thr, int N, int threads, bool strict)
{
    slime::SearchOptions o;
    o.version = slime::Version::Java;
    o.worldSeed = 12345;
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
    o.threads = threads;
    o.startX = 0;
    o.startZ = 0;
    return o;
}
} // namespace

int main()
{
    int bad = 0;

    // A) 线程数 1 vs 12：结果必须完全一致
    std::mt19937 rng(99u);
    int threadCases = 0;
    for (int i = 0; i < 12; ++i) {
        const bool strict = (i % 4) == 3;
        const int w = 300 + static_cast<int>(rng() % 2500);
        const int h = 300 + static_cast<int>(rng() % 2500);
        const int g = 2 + static_cast<int>(rng() % 6);
        const double thr = 0.2 + 0.1 * static_cast<double>(rng() % 6);
        const int N = 1 + static_cast<int>(rng() % 15);
        const Out a = run(mk(w, h, g, thr, N, 1, strict));
        const Out b = run(mk(w, h, g, thr, N, 12, strict));
        ++threadCases;
        const bool same = (a.ok == b.ok) && (a.pts == b.pts);
        if (!same) {
            ++bad;
            std::printf("[DIFF threads] %dx%d g=%d thr=%.1f N=%d%s：1线程 %zu / 12线程 %zu\n", w, h, g, thr, N,
                        strict ? " strict" : "", a.pts.size(), b.pts.size());
        }
    }
    std::printf("线程数一致性：%d 例，%s\n", threadCases, bad == 0 ? "全部一致" : "存在差异");

    // B) 非法/极端参数：不得崩溃（允许 ok=false 或空结果）
    struct Case {
        const char* name;
        slime::SearchOptions o;
    };
    std::vector<Case> cases;
    {
        slime::SearchOptions o = mk(500, 500, 3, 0.5, 5, 12, false);
        o.minDensity = 1.5;   // 非法密度
        cases.push_back({ "minDensity=1.5", o });
    }
    {
        slime::SearchOptions o = mk(500, 500, 3, 0.5, 5, 12, false);
        o.minX = 100;
        o.maxX = -100;   // minX > maxX
        cases.push_back({ "minX > maxX", o });
    }
    {
        slime::SearchOptions o = mk(500, 500, 3, 0.5, 5, 12, false);
        o.targetCount = 0;
        cases.push_back({ "targetCount=0", o });
    }
    {
        slime::SearchOptions o = mk(500, 500, 3, 0.5, 5, 12, true);
        o.pattern = decltype(o.pattern)(2, 2);   // 全白（没有任何生效格）
        cases.push_back({ "图案全白", o });
    }
    {
        slime::SearchOptions o = mk(500, 500, 3, 0.5, 5, 12, false);
        o.gridSize = 0;
        cases.push_back({ "gridSize=0", o });
    }
    {
        slime::SearchOptions o = mk(500, 500, 3, 0.5, 5, 12, false);
        o.threads = 0;
        cases.push_back({ "threads=0", o });
    }
    {
        slime::SearchOptions o = mk(500, 500, 3, 0.5, 5, 12, false);
        o.minDensity = -0.5;
        cases.push_back({ "minDensity<0", o });
    }
    for (const auto& c : cases) {
        bool crashed = false;
        Out r;
        try {
            r = run(c.o);
        } catch (...) {
            crashed = true;
        }
        const bool pass = !crashed;
        if (!pass) ++bad;
        std::printf("%-16s ok=%d 结果 %zu 条 err='%s' -> %s\n", c.name, r.ok ? 1 : 0, r.pts.size(), r.err.c_str(),
                    pass ? "OK" : "**CRASH**");
    }

    std::printf("\n异常 %d 项 -> %s\n", bad, bad == 0 ? "全部通过" : "存在问题");
    return bad == 0 ? 0 : 1;
}
