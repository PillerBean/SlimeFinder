// Force a real GPU density run into the hit cap and verify it continues without CPU fallback.
#include "core/SlimeEngine.h"
#include "gpu/GpuScanAdapter.h"

#include <atomic>
#include <cstdio>
#include <memory>
#include <string>

int main()
{
    std::string gerr;
    auto gpu = slimegpu::createGpuScanAdapter(&gerr);
    if (!gpu) {
        std::printf("GPU unavailable: %s\n", gerr.c_str());
        return 0; // optional hardware test
    }
    slime::SearchOptions opt;
    opt.version = slime::Version::Bedrock;
    opt.minX = -2048;
    opt.maxX = 2047;
    opt.minZ = -2048;
    opt.maxZ = 2047;
    opt.mode = slime::Mode::Density;
    opt.algorithm = slime::Algorithm::Exact;
    opt.gridSize = 5;
    opt.minDensity = 0.3;
    opt.targetCount = 50;
    opt.gpuScan = gpu.get();
    opt.gpuHostBudgetBytes = 8192; // engine caps result staging to >=256 hits
    slime::SlimeEngine engine;
    slime::SearchOutcome out;
    engine.run(opt, std::atomic<bool>{}, slime::ProgressFn(), out);
    std::printf("ok=%d gpuMatch=%d truncated=%d fallback='%s' results=%zu total=%lld\n",
                out.ok ? 1 : 0, out.gpuMatchUsed ? 1 : 0, out.truncated ? 1 : 0,
                out.gpuFallbackReason.c_str(), out.results.size(),
                static_cast<long long>(out.totalFound));
    if (!out.ok || !out.gpuMatchUsed || !out.truncated || !out.gpuFallbackReason.empty()) return 1;
    if (out.results.size() != 50) return 1;
    if (out.totalFound <= 50) return 1;
    std::printf("GPU-CAP-REAL OK\n");
    return 0;
}
