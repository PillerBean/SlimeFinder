#include "core/MemoryBudget.h"

#include <algorithm>
#include <cstdint>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

namespace slime {
namespace {
constexpr int64_t kMiB = 1024LL * 1024LL;
constexpr int64_t kMinTotal = 64LL * kMiB;
constexpr int64_t kAutoFloor = 512LL * kMiB;
constexpr int64_t kAutoCap = 4096LL * kMiB;
} // namespace

int64_t MemoryBudget::availablePhysicalBytes() noexcept
{
#ifdef _WIN32
    MEMORYSTATUSEX st{};
    st.dwLength = sizeof(st);
    if (GlobalMemoryStatusEx(&st)) return static_cast<int64_t>(st.ullAvailPhys);
#endif
    return 0;
}

MemoryBudget MemoryBudget::fromSettingMB(int configuredMB)
{
    return compute(configuredMB, availablePhysicalBytes());
}

MemoryBudget MemoryBudget::compute(int configuredMB, int64_t availablePhysicalBytes)
{
    MemoryBudget b;
    const int64_t avail = (std::max)(int64_t{ 0 }, availablePhysicalBytes);
    if (configuredMB <= 0) {
        b.automatic = true;
        int64_t desired = avail > 0 ? avail / 2 : 1536LL * kMiB;
        desired = std::clamp(desired, kAutoFloor, kAutoCap);
        if (avail > 0) desired = (std::min)(desired, (std::max)(kMinTotal, avail - 256LL * kMiB));
        b.totalBytes = desired;
        b.clamped = (avail > 0 && desired != avail / 2);
    } else {
        b.automatic = false;
        int64_t desired = static_cast<int64_t>(configuredMB) * kMiB;
        int64_t maxAllowed = desired;
        if (avail > 0) {
            maxAllowed = (std::max)(kMinTotal, avail - 512LL * kMiB);
        }
        const int64_t effective = std::clamp(desired, kMinTotal, maxAllowed);
        b.totalBytes = effective;
        b.clamped = effective != desired;
    }
    b.distribute();
    return b;
}

void MemoryBudget::distribute()
{
    if (totalBytes < kMinTotal) totalBytes = kMinTotal;
    chunkBitmapBytes = totalBytes * 50 / 100;
    packCacheBytes = totalBytes * 20 / 100;
    buildWorkingBytes = totalBytes * 15 / 100;
    gpuHostBytes = totalBytes * 10 / 100;
    if (packCacheBytes < 32 * kMiB) packCacheBytes = 0;
    reserveBytes = totalBytes - chunkBitmapBytes - packCacheBytes - buildWorkingBytes - gpuHostBytes;
    if (reserveBytes < 0) {
        const int64_t shrink = -reserveBytes;
        const int64_t fromChunk = (std::min)(shrink, chunkBitmapBytes);
        chunkBitmapBytes -= fromChunk;
        reserveBytes += fromChunk;
        if (reserveBytes < 0) {
            gpuHostBytes = (std::max)(int64_t{ 0 }, gpuHostBytes + reserveBytes);
            reserveBytes = 0;
        }
    }
}

} // namespace slime
