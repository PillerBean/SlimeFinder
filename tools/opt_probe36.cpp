// opt_probe36.cpp - 退化/边界参数健壮性：不得崩溃，且 full / smart 结果一致
#include "core/SlimeEngine.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>
#include <vector>

namespace {
struct Out {
    bool ok = false;
    std::string err;
    std::vector<std::pair<int, int>> pts;
};

Out run(const slime::SearchOptions& o, std::atomic<bool>* cancelFlag = nullptr)
{
    slime::SlimeEngine eng;
    slime::SearchOutcome out;
    std::atomic<bool> local{ false };
    std::atomic<bool>& cancel = cancelFlag ? *cancelFlag : local;
    slime::ProgressFn noop;
    eng.run(o, cancel, noop, out);
    Out r;
    r.ok = out.ok;
    r.err = out.error;
    for (const auto& m : out.results) r.pts.emplace_back(m.minX, m.minZ);
    return r;
}

slime::SearchOptions mk(int w, int h, int g, double thr, bool strict, int N, bool smart, int sx, int sz)
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
    o.smartRange = smart;
    o.threads = 12;
    o.startX = sx;
    o.startZ = sz;
    return o;
}
} // namespace

int main()
{
    struct Case {
        const char* name;
        slime::SearchOptions o;
    };
    std::vector<Case> cases;
    cases.push_back({ "窗口大于区域(g=15, 10x10)", mk(10, 10, 15, 0.5, false, 5, false, 0, 0) });
    cases.push_back({ "g=1 密度 100x100", mk(100, 100, 1, 1.0, false, 5, false, 0, 0) });
    cases.push_back({ "g=1 密度 thr=0", mk(100, 100, 1, 0.0, false, 5, false, 0, 0) });
    cases.push_back({ "图案大于区域(2x2, 1x1?)", mk(1, 1, 2, 0.5, true, 5, false, 0, 0) });
    cases.push_back({ "起点远在区域外", mk(500, 500, 5, 0.5, false, 10, true, 100000, -100000) });
    cases.push_back({ "目标数 1", mk(1000, 1000, 3, 0.3, false, 1, true, 0, 0) });
    cases.push_back({ "目标数 50000（超过区域）", mk(200, 200, 2, 0.0, false, 50000, false, 0, 0) });
    cases.push_back({ "奇数尺寸 197x331", mk(197, 331, 7, 0.62, false, 3, true, 0, 0) });
    cases.push_back({ "1x1000 长条", mk(1, 1000, 3, 0.5, true, 2, false, 0, 0) });
    cases.push_back({ "1000x1 长条", mk(1000, 1, 3, 0.5, true, 2, true, 0, 0) });
    cases.push_back({ "g=15 大窗口 200x200", mk(200, 200, 15, 0.5, false, 4, false, 0, 0) });

    int bad = 0;
    for (const auto& c : cases) {
        Out a, b;
        bool crashed = false;
        try {
            a = run(c.o);
            slime::SearchOptions optSmart = c.o;
            optSmart.smartRange = !c.o.smartRange;
            b = run(optSmart);
        } catch (...) {
            crashed = true;
        }
        bool same = (a.ok == b.ok) && (a.pts == b.pts);
        if (crashed) same = false;
        if (!same) ++bad;
        std::printf("%-28s ok=%d full=%zu smart=%zu err='%s' -> %s\n", c.name, a.ok ? 1 : 0, a.pts.size(),
                    b.pts.size(), a.err.c_str(), same ? "OK" : "**DIFF/CRASH**");
        std::fflush(stdout);
    }

    // 取消测试：搜索中途 cancel，必须正常返回且之后能再跑
    {
        std::atomic<bool> cancel{ false };
        std::thread th([&]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            cancel.store(true);
        });
        const Out r = run(mk(20000, 20000, 5, 0.3, false, 10, false, 0, 0), &cancel);
        th.join();
        const Out again = run(mk(2000, 2000, 3, 0.3, false, 5, false, 0, 0));
        const bool ok = again.ok && !again.pts.empty();
        if (!ok) ++bad;
        std::printf("取消测试：cancel 后 ok=%d 结果 %zu 条；随后再跑 ok=%d 结果 %zu 条 -> %s\n", r.ok ? 1 : 0,
                    r.pts.size(), again.ok ? 1 : 0, again.pts.size(), ok ? "OK" : "**FAIL**");
    }

    std::printf("\n异常 %d 项 -> %s\n", bad, bad == 0 ? "全部通过" : "存在问题");
    return bad == 0 ? 0 : 1;
}
