// opt_probe38.cpp - 基岩缓存一致性：构建缓存 → 导入 → 搜索，结果必须与「不用缓存」完全一致
#include "core/CacheManager.h"
#include "core/SlimeEngine.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace {
struct Res {
    bool ok = false;
    std::vector<std::pair<int, int>> pts;
    double ms = 0;
    int64_t hitPoints = 0;
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
    r.hitPoints = out.cacheHitPoints;
    for (const auto& m : out.results) r.pts.emplace_back(m.minX, m.minZ);
    return r;
}

slime::SearchOptions mk(int w, int h, int g, double thr, int N)
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
    o.threads = 12;
    return o;
}
} // namespace

int main()
{
    const int W = 1500, H = 1200, G = 4, N = 10;
    const double THR = 0.4;
    const std::string tmp = (std::filesystem::temp_directory_path() / "slime_probe38_cache.bin").string();
    int bad = 0;

    // 1) 构建缓存
    slime::CacheManager mgr;
    slime::SearchOptions build = mk(W, H, G, THR, N);
    build.useCache = true;
    build.buildMode = true;
    build.cache = &mgr;
    build.cacheWrite = &mgr;
    const Res rBuild = run(build);
    std::string err;
    const bool saved = mgr.saveFile(tmp, err);
    std::printf("构建缓存：ok=%d %.1f ms，条目 %lld，保存=%d%s\n", rBuild.ok ? 1 : 0, rBuild.ms,
                static_cast<long long>(mgr.entryCount()), saved ? 1 : 0, saved ? "" : err.c_str());
    if (!rBuild.ok || !saved) ++bad;

    // 2) 不用缓存
    const Res noCache = run(mk(W, H, G, THR, N));
    std::printf("不用缓存：ok=%d %.1f ms，结果 %zu 条\n", noCache.ok ? 1 : 0, noCache.ms, noCache.pts.size());

    // 3) 导入缓存后再搜索
    slime::CacheManager mgr2;
    std::string lerr;
    const bool loaded = mgr2.loadFile(tmp, true, lerr);
    slime::SearchOptions cached = mk(W, H, G, THR, N);
    cached.useCache = true;
    cached.cache = &mgr2;
    const Res withCache = run(cached);
    std::printf("用缓存：  ok=%d %.1f ms，结果 %zu 条，缓存命中点位 %lld\n", withCache.ok ? 1 : 0, withCache.ms,
                withCache.pts.size(), static_cast<long long>(withCache.hitPoints));

    const bool same = (noCache.ok == withCache.ok) && (noCache.pts == withCache.pts);
    if (!same) ++bad;
    std::printf("一致性：%s（加载=%d）\n", same ? "一致" : "**不一致**", loaded ? 1 : 0);

    // 4) 范围不完整覆盖时的缓存也应给出与无缓存一致的结果（只覆盖一半区域）
    slime::CacheManager partial;
    slime::SearchOptions half = mk(W / 2, H, G, THR, N);
    half.minX = -W / 2;
    half.maxX = half.minX + W / 2 - 1;
    half.useCache = true;
    half.buildMode = true;
    half.cache = &partial;
    half.cacheWrite = &partial;
    const Res rHalf = run(half);
    slime::SearchOptions fullPartial = mk(W, H, G, THR, N);
    fullPartial.useCache = true;
    fullPartial.cache = &partial;
    const Res withPartial = run(fullPartial);
    const Res noPartial = run(mk(W, H, G, THR, N));
    const bool same2 = (withPartial.ok == noPartial.ok) && (withPartial.pts == noPartial.pts);
    if (!same2) ++bad;
    std::printf("半覆盖缓存（构建 ok=%d %.1f ms）：%s\n", rHalf.ok ? 1 : 0, rHalf.ms,
                same2 ? "一致" : "**不一致**");

    std::filesystem::remove(tmp);
    std::printf("\n异常 %d 项 -> %s\n", bad, bad == 0 ? "全部通过" : "存在问题");
    return bad == 0 ? 0 : 1;
}
