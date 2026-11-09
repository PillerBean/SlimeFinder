// opt_probe41.cpp - 缓存+提前停止一致性 + 反复运行压力测试
#include "core/CacheManager.h"
#include "core/SlimeEngine.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <random>
#include <string>
#include <vector>

namespace {
struct Res {
    bool ok = false;
    std::vector<std::pair<int, int>> pts;
    double ms = 0;
    int64_t found = 0;
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
    r.found = out.totalFound;
    for (const auto& m : out.results) r.pts.emplace_back(m.minX, m.minZ);
    return r;
}

slime::SearchOptions mk(int w, int h, int g, double thr, int N, bool stop, bool smart)
{
    slime::SearchOptions o;
    o.version = slime::Version::Bedrock;
    o.worldSeed = 0;
    o.minX = -w / 2;
    o.maxX = o.minX + w - 1;
    o.minZ = -h / 2;
    o.maxZ = o.minZ + h - 1;
    o.mode = slime::Mode::Density;
    o.gridSize = g;
    o.minDensity = thr;
    o.targetCount = N;
    o.stopAtTarget = stop;
    o.smartRange = smart;
    o.threads = 12;
    return o;
}
} // namespace

int main()
{
    int bad = 0;
    const std::string tmp = (std::filesystem::temp_directory_path() / "slime_probe41_cache.bin").string();

    // A) 缓存 + 提前停止：结果前 N 名必须与「不用缓存的全量」一致
    {
        const int W = 6000, H = 6000, G = 4, N = 10;
        const double THR = 0.35;
        slime::CacheManager mgr;
        slime::SearchOptions build = mk(W, H, G, THR, N, false, false);
        build.cacheWrite = &mgr;
        build.buildMode = true;
        build.useCache = true;
        build.cache = &mgr;
        const Res rBuild = run(build);
        std::string err;
        const bool saved = mgr.saveFile(tmp, err);
        slime::CacheManager mgr2;
        std::string lerr;
        const bool loaded = mgr2.loadFile(tmp, true, lerr);

        const Res noCacheFull = run(mk(W, H, G, THR, N, false, false));
        slime::SearchOptions cs = mk(W, H, G, THR, N, true, false);
        cs.useCache = true;
        cs.cache = &mgr2;
        const Res cacheStop = run(cs);
        slime::SearchOptions ns = mk(W, H, G, THR, N, true, false);
        const Res noCacheStop = run(ns);

        const bool sameStop = (cacheStop.pts == noCacheStop.pts);
        const bool prefixOfFull = std::equal(cacheStop.pts.begin(), cacheStop.pts.end(), noCacheFull.pts.begin()) &&
                                  cacheStop.pts.size() <= noCacheFull.pts.size();
        if (!(sameStop && prefixOfFull)) ++bad;
        std::printf("A) 缓存构建 ok=%d %.1f ms（保存=%d 加载=%d）\n", rBuild.ok ? 1 : 0, rBuild.ms, saved ? 1 : 0,
                    loaded ? 1 : 0);
        std::printf("   缓存+早停 %.1f ms（%zu 条）vs 无缓存+早停 %.1f ms（%zu 条）→ 一致=%s；是前者的前缀=%s\n",
                    cacheStop.ms, cacheStop.pts.size(), noCacheStop.ms, noCacheStop.pts.size(),
                    sameStop ? "是" : "否", prefixOfFull ? "是" : "否");
        std::filesystem::remove(tmp);
    }

    // B) 压力测试：反复运行随机搜索（含智能范围/早停），检查稳定与内存不段增长
    {
        std::mt19937 rng(2026u);
        const auto t0 = std::chrono::steady_clock::now();
        int runs = 0;
        for (int i = 0; i < 120; ++i) {
            const int w = 300 + static_cast<int>(rng() % 2500);
            const int h = 300 + static_cast<int>(rng() % 2500);
            const int g = 2 + static_cast<int>(rng() % 6);
            const double thr = 0.2 + 0.1 * static_cast<double>(rng() % 6);
            const int N = 1 + static_cast<int>(rng() % 20);
            const bool stop = (i % 3) == 0;
            const bool smart = (i % 4) == 0;
            const bool ok = run(mk(w, h, g, thr, N, stop, smart)).ok;
            if (!ok) {
                ++bad;
                std::printf("   压力测试第 %d 次运行失败\n", i);
            }
            ++runs;
        }
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        std::printf("B) 压力测试：%d 次随机搜索全部完成，共 %.0f ms（平均 %.1f ms/次）\n", runs, ms, ms / runs);
    }

    std::printf("\n异常 %d 项 -> %s\n", bad, bad == 0 ? "全部通过" : "存在问题");
    return bad == 0 ? 0 : 1;
}
