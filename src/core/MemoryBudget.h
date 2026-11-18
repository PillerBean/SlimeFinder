// MemoryBudget.h - v3.14.3: one RAM budget for memory-sensitive flows.
#pragma once

#include <cstdint>

namespace slime {

// Host RAM only. Disk cache capacity (cacheMaxMB) and VRAM are deliberately excluded.
struct MemoryBudget {
    int64_t totalBytes = 0;
    int64_t chunkBitmapBytes = 0;
    int64_t packCacheBytes = 0;
    int64_t buildWorkingBytes = 0;
    int64_t gpuHostBytes = 0;
    int64_t reserveBytes = 0;
    bool automatic = true;
    bool clamped = false;

    // configuredMB == 0 -> auto: clamp(availablePhysical * 50%, 512 MiB, 4096 MiB)
    // configuredMB > 0 -> clamp(configured, 64 MiB, availablePhysical - 512 MiB), floor 64 MiB.
    static MemoryBudget fromSettingMB(int configuredMB);
    // Deterministic helper for tests / callers that already queried the OS.
    static MemoryBudget compute(int configuredMB, int64_t availablePhysicalBytes);
    static int64_t availablePhysicalBytes() noexcept;
    void distribute();
};

} // namespace slime
