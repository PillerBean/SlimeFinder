// temp_range_rate.cpp -- 逐 range 条目率（只读现有缓存，不改产品代码）
// 判读：双峰（部分 ~10%、部分 ~0%）⇒ 候选 ④′ 坐实；均匀 ~6.7% ⇒ 转第 5 候选
#include "core/CacheManager.h"

#include <chrono>
#include <cstdio>
#include <random>

using namespace slime;
using Clock = std::chrono::steady_clock;

int main(int argc, char** argv)
{
    if (argc < 2) { std::printf("usage: temp_range_rate <cache> [samplesPerRange]\n"); return 1; }
    CacheManager c;
    std::string err;
    const auto t0 = Clock::now();
    if (!c.loadFile(argv[1], true, err)) { std::printf("load fail: %s\n", err.c_str()); return 2; }
    const auto t1 = Clock::now();
    const double loadMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
    const int64_t totalEntries = c.entryCount();
    const size_t R = c.ranges().size();
    const long long perRange = (argc > 2) ? std::atoll(argv[2]) : 200000;

    std::printf("LOAD cacheLoadMs=%.0f(实测 loadFile) ranges=%zu(文件头 rangeCount) entries=%lld(文件头 entryCount) sizeBytes=%lld\n",
                loadMs, R, static_cast<long long>(totalEntries), static_cast<long long>(c.sizeBytes()));
    std::printf("%-3s %-30s %14s %12s %10s %9s\n", "#", "rect(minX..maxX, minZ..maxZ)", "area(区块)",
                "samples", "slime", "rate%");

    std::mt19937_64 rng(20260921);
    int64_t sumArea = 0, sumSlime = 0, sumSamples = 0;
    for (size_t i = 0; i < R; ++i) {
        const Rect& r = c.ranges()[i];
        const int64_t w = static_cast<int64_t>(r.maxX - r.minX + 1);
        const int64_t h = static_cast<int64_t>(r.maxZ - r.minZ + 1);
        const int64_t area = w * h;
        sumArea += area;
        long long slime = 0;
        for (long long k = 0; k < perRange; ++k) {
            const int x = r.minX + static_cast<int>(rng() % static_cast<uint64_t>(w));
            const int z = r.minZ + static_cast<int>(rng() % static_cast<uint64_t>(h));
            if (c.lookup(x, z) == 1) ++slime;
        }
        sumSlime += slime;
        sumSamples += perRange;
        std::printf("%-3zu %-30s %14lld %12lld %10lld %8.2f%%\n", i,
                    (std::to_string(r.minX) + ".." + std::to_string(r.maxX) + ", " + std::to_string(r.minZ) +
                     ".." + std::to_string(r.maxZ))
                        .c_str(),
                    static_cast<long long>(area), perRange, slime,
                    100.0 * static_cast<double>(slime) / static_cast<double>(perRange));
    }
    std::printf("合计: 面积=%lld  抽样=%lld  命中史莱姆=%lld  抽样总率=%.2f%%  参考=小范围真值 10.56%%，基岩理论≈10%%\n",
                static_cast<long long>(sumArea), sumSamples, sumSlime,
                100.0 * static_cast<double>(sumSlime) / static_cast<double>(sumSamples));
    return 0;
}
