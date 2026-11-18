// Direct CacheStore pack-decode / band-fill benchmark.
#include "cache/CacheStore.h"
#include "core/CacheManager.h"
#include "core/SlimeEngine.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>

int main(int argc, char** argv)
{
    if (argc < 2) {
        std::printf("usage: cache_lru_bench <cache-root> [iterations] [half]\n");
        return 2;
    }
    const int iterations = argc > 2 ? std::atoi(argv[2]) : 50;
    const int half = argc > 3 ? std::atoi(argv[3]) : 1024;
    slime::cache::CacheStore store{ std::filesystem::path(argv[1]) };
    std::string err;
    if (!store.load(&err, false)) {
        std::printf("load failed: %s\n", err.c_str());
        return 1;
    }
    slime::Rect band{ -half, half - 1, -half, half - 1 };
    slime::CacheManager cm;
    // warm the index and the first band
    if (!store.fillCacheManagerRange(cm, band, &err)) {
        std::printf("fill failed: %s\n", err.c_str());
        return 1;
    }
    const auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < iterations; ++i) {
        if (!store.fillCacheManagerRange(cm, band, &err)) {
            std::printf("fill failed at %d: %s\n", i, err.c_str());
            return 1;
        }
    }
    const auto t1 = std::chrono::steady_clock::now();
    const double total = std::chrono::duration<double, std::milli>(t1 - t0).count();
    std::printf("packs=%zu band=%d iterations=%d total=%.2fms avg=%.3fms cacheMB=%s\n",
                store.packs().size(), half * 2, iterations, total, total / iterations,
                std::getenv("SLIME_PACK_CACHE_MB") ? std::getenv("SLIME_PACK_CACHE_MB") : "default");
    return 0;
}
