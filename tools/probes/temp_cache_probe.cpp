// temp_cache_probe.cpp -- 任务 3 前置微基准（临时测量件，非产品代码）
// 测量：① loadFile 加载耗时/体积 ② isSlime 二分查表吞吐与命中率 ③ lookup 命中率
#include "core/CacheManager.h"

#include <chrono>
#include <cstdio>
#include <random>
#include <string>
#include <vector>

using namespace slime;
using Clock = std::chrono::steady_clock;
static double ms(Clock::time_point a, Clock::time_point b)
{
    return std::chrono::duration<double, std::milli>(b - a).count();
}

int main(int argc, char** argv)
{
    if (argc < 2) {
        std::printf("usage: temp_cache_probe <cache.slimecache> [samples]\n");
        return 1;
    }
    CacheManager c;
    std::string err;
    const auto t0 = Clock::now();
    const bool ok = c.loadFile(argv[1], true, err);
    const auto t1 = Clock::now();
    if (!ok) {
        std::printf("loadFile 失败: %s\n", err.c_str());
        return 2;
    }
    const int64_t entries = c.entryCount();
    const int64_t bytes = c.sizeBytes();
    std::printf("LOAD ok ms=%.0f entries=%lld ranges=%zu sizeBytes=%lld (%.2f B/entry)\n", ms(t0, t1),
                static_cast<long long>(entries), c.ranges().size(), static_cast<long long>(bytes),
                entries > 0 ? static_cast<double>(bytes) / static_cast<double>(entries) : 0.0);

    // 在缓存覆盖的范围内随机取样（用 ranges 的并集包围盒近似）
    int minX = 0, maxX = 0, minZ = 0, maxZ = 0;
    bool first = true;
    for (const Rect& r : c.ranges()) {
        if (first) { minX = r.minX; maxX = r.maxX; minZ = r.minZ; maxZ = r.maxZ; first = false; }
        else {
            if (r.minX < minX) minX = r.minX;
            if (r.maxX > maxX) maxX = r.maxX;
            if (r.minZ < minZ) minZ = r.minZ;
            if (r.maxZ > maxZ) maxZ = r.maxZ;
        }
    }
    const int64_t samples = (argc > 2) ? std::atoll(argv[2]) : 2000000;
    std::mt19937_64 rng(20260921);
    std::vector<std::pair<int, int>> pts;
    pts.reserve(static_cast<size_t>(samples));
    for (int64_t i = 0; i < samples; ++i) {
        const int x = minX + static_cast<int>(rng() % static_cast<uint64_t>(std::max(1, maxX - minX + 1)));
        const int z = minZ + static_cast<int>(rng() % static_cast<uint64_t>(std::max(1, maxZ - minZ + 1)));
        pts.emplace_back(x, z);
    }
    // ---- range 几何 + 位图测算（任务 3+5 设计文档要求）----
    int64_t areaSum = 0;
    std::printf("RANGES %zu 个：\n", c.ranges().size());
    for (size_t i = 0; i < c.ranges().size(); ++i) {
        const Rect& r = c.ranges()[i];
        const int64_t w = static_cast<int64_t>(r.maxX - r.minX + 1);
        const int64_t h = static_cast<int64_t>(r.maxZ - r.minZ + 1);
        const int64_t area = w * h;
        areaSum += area;
        std::printf("  #%zu minX=%d maxX=%d minZ=%d maxZ=%d  %lld×%lld  面积=%lld  位图=%lld KB\n", i,
                    r.minX, r.maxX, r.minZ, r.maxZ, static_cast<long long>(w), static_cast<long long>(h),
                    static_cast<long long>(area), static_cast<long long>(area / 8 / 1024));
    }
    const int64_t bboxArea = static_cast<int64_t>(maxX - minX + 1) * static_cast<int64_t>(maxZ - minZ + 1);
    std::printf("BBOX %d..%d × %d..%d 面积=%lld（位图 %lld MB）\n", minX, maxX, minZ, maxZ,
                static_cast<long long>(bboxArea), static_cast<long long>(bboxArea / 8 / 1048576));
    std::printf("SUM-RANGE 面积=%lld（位图 %lld MB）  BBOX/SUM=%.2f  条目/面积=%.4f%%\n",
                static_cast<long long>(areaSum), static_cast<long long>(areaSum / 8 / 1048576),
                areaSum > 0 ? static_cast<double>(bboxArea) / static_cast<double>(areaSum) : 0.0,
                areaSum > 0 ? 100.0 * static_cast<double>(entries) / static_cast<double>(areaSum) : 0.0);
    std::printf("对比：坐标集合=%lld B（%.0f MB） vs 位图(sum-range)=%lld B（%.0f MB） 压缩比=%.1fx\n",
                static_cast<long long>(bytes), static_cast<double>(bytes) / 1048576.0,
                static_cast<long long>(areaSum / 8), static_cast<double>(areaSum / 8) / 1048576.0,
                (areaSum > 0) ? static_cast<double>(bytes) / static_cast<double>(areaSum / 8) : 0.0);

    int64_t covered = 0, slime = 0;
    const auto t2 = Clock::now();
    for (const auto& p : pts) {
        const int lk = c.lookup(p.first, p.second);
        if (lk >= 0) ++covered;
        if (lk == 1) ++slime;
        else if (lk < 0) {
            // 未覆盖点：仍做一次 isSlime 二分以模拟「无论如何都要查表」的成本
            (void)c.isSlime(p.first, p.second);
        }
    }
    const auto t3 = Clock::now();
    const double totalMs = ms(t2, t3);
    std::printf("LOOKUP samples=%lld ms=%.0f ns/point=%.1f covered=%.1f%% slime=%.1f%% (of samples)\n",
                static_cast<long long>(samples), totalMs, totalMs * 1e6 / static_cast<double>(samples),
                100.0 * static_cast<double>(covered) / static_cast<double>(samples),
                100.0 * static_cast<double>(slime) / static_cast<double>(samples));
    if (covered > 0) {
        std::printf("HIT-RATE 命中点中史莱姆占比=%.1f%%（基岩版理论≈10%%）\n",
                    100.0 * static_cast<double>(slime) / static_cast<double>(covered));
    }
    std::printf("参考：v3.3.7.2 实测分块路径单线程扫描+判定 10.0M 点/秒（=100 ns/点，20MB 位图）、\n"
                "      小位图(64KB)同模式单线程 32.4M 点/秒（=31 ns/点）\n");
    return 0;
}
