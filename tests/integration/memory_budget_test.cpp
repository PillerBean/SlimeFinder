#include "core/MemoryBudget.h"

#include <cstdio>

namespace {
int g_fail = 0;
#define CHECK(x) do { if (!(x)) { std::printf("FAIL %d: %s\n", __LINE__, #x); ++g_fail; } } while (0)
constexpr int64_t MiB = 1024LL * 1024LL;
} // namespace

int main()
{
    {
        const auto b = slime::MemoryBudget::compute(0, 8LL * 1024 * MiB);
        CHECK(b.automatic);
        CHECK(b.totalBytes == 4LL * 1024 * MiB);
        CHECK(b.chunkBitmapBytes == b.totalBytes / 2);
        CHECK(b.packCacheBytes == b.totalBytes / 5);
        CHECK(b.chunkBitmapBytes + b.packCacheBytes + b.buildWorkingBytes + b.gpuHostBytes +
                  b.reserveBytes ==
              b.totalBytes);
    }
    {
        const auto b = slime::MemoryBudget::compute(0, 1024LL * MiB);
        CHECK(b.totalBytes == 512LL * MiB);
        CHECK(b.packCacheBytes >= 32LL * MiB);
    }
    {
        const auto b = slime::MemoryBudget::compute(1024, 8LL * 1024 * MiB);
        CHECK(!b.automatic);
        CHECK(b.totalBytes == 1024LL * MiB);
        CHECK(!b.clamped);
    }
    {
        const auto b = slime::MemoryBudget::compute(8192, 4096LL * MiB);
        CHECK(b.clamped);
        CHECK(b.totalBytes == 3584LL * MiB);
    }
    {
        const auto b = slime::MemoryBudget::compute(64, 8LL * 1024 * MiB);
        CHECK(b.totalBytes == 64LL * MiB);
        CHECK(b.packCacheBytes == 0);
    }
    if (g_fail == 0) {
        std::printf("RAM-BUDGET ALL PASS\n");
        return 0;
    }
    std::printf("RAM-BUDGET FAIL %d\n", g_fail);
    return 1;
}
