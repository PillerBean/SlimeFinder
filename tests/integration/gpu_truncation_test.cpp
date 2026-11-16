// GPU cap overflow: truncated results must be accepted without CPU fallback.
#include "core/SlimeEngine.h"

#include <atomic>
#include <cstdio>
#include <string>
#include <vector>

namespace {
class CappedGpu : public slime::GpuScan {
public:
    bool available() const noexcept override { return true; }
    std::string deviceName() const override { return "fake-gpu"; }
    uint64_t globalMemBytes() const noexcept override { return 1ull << 30; }

    bool scanRect(slime::Version, int64_t, int, int, int, int, std::vector<uint8_t>&,
                  std::string*) const override
    {
        return false;
    }

    bool matchDensity(const slime::DensityMatchParams& p, const std::atomic<bool>& cancel,
                      std::vector<slime::MatchResult>& out, std::string* err) const override
    {
        if (cancel.load(std::memory_order_relaxed)) return false;
        if (err) *err = "fake cap";
        if (p.outTruncated) *p.outTruncated = true;
        if (p.outTotalFound) *p.outTotalFound = 1000000;
        out.clear();
        const int64_t total = 500;
        out.reserve(static_cast<size_t>(total));
        for (int64_t i = 0; i < total; ++i) {
            slime::MatchResult m;
            m.minX = p.axMin + static_cast<int>(i % 200);
            m.maxX = m.minX + p.gridSize - 1;
            m.minZ = p.azMin + static_cast<int>(i / 200);
            m.maxZ = m.minZ + p.gridSize - 1;
            m.density = 0.5f;
            out.push_back(m);
        }
        return true;
    }
};
} // namespace

int main()
{
    slime::SearchOptions opt;
    opt.version = slime::Version::Bedrock;
    opt.minX = -1024;
    opt.maxX = 1023;
    opt.minZ = -1024;
    opt.maxZ = 1023;
    opt.mode = slime::Mode::Density;
    opt.algorithm = slime::Algorithm::Exact;
    opt.gridSize = 5;
    opt.minDensity = 0.3;
    opt.targetCount = 50;
    opt.threads = 2;
    CappedGpu gpu;
    opt.gpuScan = &gpu;

    slime::SlimeEngine engine;
    slime::SearchOutcome out;
    engine.run(opt, std::atomic<bool>{}, slime::ProgressFn(), out);
    std::printf("ok=%d gpuUsed=%d truncated=%d fallback='%s' results=%zu total=%lld\n",
                out.ok ? 1 : 0, out.gpuMatchUsed ? 1 : 0, out.truncated ? 1 : 0,
                out.gpuFallbackReason.c_str(), out.results.size(),
                static_cast<long long>(out.totalFound));
    if (!out.ok) return 1;
    if (!out.gpuMatchUsed) return 1;
    if (!out.truncated) return 1;
    if (!out.gpuFallbackReason.empty()) return 1;
    if (out.results.size() != 50) return 1;
    if (out.totalFound != 1000000) return 1;
    std::printf("GPU-TRUNCATION OK\n");
    return 0;
}
