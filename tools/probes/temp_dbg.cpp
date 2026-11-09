// temp_dbg.cpp -- 复现单元用例 [20]-D 的跨运行场景，打印每轮后的 ranges/entries
#include "core/CacheManager.h"
#include "core/SlimeEngine.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>

using namespace slime;

static SearchOutcome runOnce(const SearchOptions& opt)
{
    SearchOutcome out;
    std::atomic<bool> cancel{ false };
    SlimeEngine e;
    e.run(opt, cancel, nullptr, out);
    return out;
}

int main()
{
#ifdef _WIN32
    _putenv_s("SLIME_FORCE_CHUNKED", "1");
    _putenv_s("SLIME_CHUNK_WORDS", "16384");
#endif
    const int64_t budgets[] = { 0, 100'000'000, 50'000'000, 20'000'000, 10'000'000, 5'000'000, 2'000'000, 1'000'000 };
    for (int64_t budget : budgets) {
        CacheManager c;
        SearchOptions o;
        o.version = Version::Bedrock;
        o.mode = Mode::Density;
        o.gridSize = 5;
        o.minDensity = 0.75;
        o.minX = -160; o.maxX = 159;
        o.minZ = -20000; o.maxZ = 19999;
        o.worldSeed = 0;
        o.targetCount = 1;
        o.stopAtTarget = false;
        o.threads = 0;
        o.useCache = true;
        o.cache = &c;
        o.cacheWrite = &c;
        o.cacheBudgetBytes = budget;
        const SearchOutcome r = runOnce(o);
        std::printf("budget=%12lld run1 incr=%d | ranges=%2zu entries=%9lld sizeBytes=%lld", static_cast<long long>(budget),
                    r.cacheHandledIncrementally ? 1 : 0, c.ranges().size(),
                    static_cast<long long>(c.entryCount()), static_cast<long long>(c.sizeBytes()));
        const int64_t d = c.dropHollowRanges();
        std::printf(" | sweep=%lld -> ranges=%zu entries=%lld\n", static_cast<long long>(d), c.ranges().size(),
                    static_cast<long long>(c.entryCount()));
    }
    return 0;
}
