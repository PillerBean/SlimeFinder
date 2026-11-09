// temp_band.cpp -- 逐 range 真值比对（定位「稀疏声明」造成的漏判范围）
// 对每个 range 均匀采样 N 点：统计「缓存判定史莱姆率」与「真值史莱姆率」，
// 得出该 range 的漏判率 = 真值为史莱姆但缓存 lookup != 1 的比例。
#include "core/CacheManager.h"
#include "core/SlimeEngine.h"

#include <cstdio>
#include <cstdlib>
#include <random>

using namespace slime;

int main(int argc, char** argv)
{
    if (argc < 2) { std::printf("usage: temp_band <cache> [samplesPerRange]\n"); return 1; }
    CacheManager c;
    std::string err;
    const auto t0 = std::chrono::steady_clock::now();
    if (!c.loadFile(argv[1], true, err)) { std::printf("load fail: %s\n", err.c_str()); return 2; }
    const double loadMs =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    const long long N = (argc > 2) ? std::atoll(argv[2]) : 100000;
    std::printf("load=%.0fms ranges=%zu(清扫后) entries=%lld  每 range 采样=%lld\n", loadMs,
                c.ranges().size(), static_cast<long long>(c.entryCount()), N);
    std::printf("%-3s %-40s %10s %10s %10s %10s %10s\n", "#", "rect", "cacheSlime%", "truthSlime%",
                "hitRate%", "missRate%", "agree%");
    std::mt19937_64 rng(20260921);
    int64_t gCache = 0, gTruth = 0, gHit = 0, gMiss = 0;
    for (size_t i = 0; i < c.ranges().size(); ++i) {
        const Rect& r = c.ranges()[i];
        const int64_t w = static_cast<int64_t>(r.maxX - r.minX + 1);
        const int64_t h = static_cast<int64_t>(r.maxZ - r.minZ + 1);
        long long cacheSlime = 0, truthSlime = 0, hit = 0, miss = 0;
        for (long long k = 0; k < N; ++k) {
            const int x = r.minX + static_cast<int>(rng() % static_cast<uint64_t>(w));
            const int z = r.minZ + static_cast<int>(rng() % static_cast<uint64_t>(h));
            const int lk = c.lookup(x, z);
            const bool truth = SlimeEngine::isSlimeChunk(Version::Bedrock, 0, x, z);
            if (lk == 1) ++cacheSlime;
            if (truth) {
                ++truthSlime;
                if (lk == 1) ++hit; else ++miss;
            }
        }
        char rect[128];
        std::snprintf(rect, sizeof(rect), "%d..%d x %d..%d", r.minX, r.maxX, r.minZ, r.maxZ);
        const double cs = 100.0 * static_cast<double>(cacheSlime) / static_cast<double>(N);
        const double ts = 100.0 * static_cast<double>(truthSlime) / static_cast<double>(N);
        const double hr = truthSlime ? 100.0 * static_cast<double>(hit) / static_cast<double>(truthSlime) : 0.0;
        const double mr = truthSlime ? 100.0 * static_cast<double>(miss) / static_cast<double>(truthSlime) : 0.0;
        const double ag = 100.0 * static_cast<double>(N - miss) / static_cast<double>(N);
        std::printf("%-3zu %-40s %9.3f%% %9.3f%% %9.2f%% %9.2f%% %9.2f%%%s\n", i, rect, cs, ts, hr, mr, ag,
                    (mr > 50.0) ? "   <== 稀疏声明：绝大多数史莱姆区块被漏判" : "");
        gCache += cacheSlime; gTruth += truthSlime; gHit += hit; gMiss += miss;
    }
    std::printf("合计: cacheSlime=%lld truthSlime=%lld 命中=%lld 漏判=%lld  漏判率=%.2f%%  一致率=%.2f%%\n",
                static_cast<long long>(gCache), static_cast<long long>(gTruth), static_cast<long long>(gHit),
                static_cast<long long>(gMiss),
                gTruth ? 100.0 * static_cast<double>(gMiss) / static_cast<double>(gTruth) : 0.0,
                100.0 * static_cast<double>(gTruth + (N * static_cast<long long>(c.ranges().size()) - gTruth) - gMiss) /
                    static_cast<double>(N * static_cast<long long>(c.ranges().size())));
    return 0;
}
