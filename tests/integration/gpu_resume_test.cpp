// GPU scan failure: already-integrated tiles must not be recomputed by the CPU fallback.
#include "core/SlimeEngine.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

class FailAfterFirstTile : public slime::GpuScan {
public:
    bool available() const noexcept override { return true; }
    std::string deviceName() const override { return "fake-gpu"; }
    uint64_t globalMemBytes() const noexcept override { return 1ull << 30; }
    int calls() const { return m_calls.load(); }

    bool scanRect(slime::Version version, int64_t, int minX, int minZ, int w, int h,
                  std::vector<uint8_t>& out, std::string* err) const override
    {
        const int call = ++m_calls;
        if (call > 2) {
            if (err) *err = "injected tile failure";
            return false;
        }
        if (version != slime::Version::Bedrock) {
            if (err) *err = "test only supports bedrock";
            return false;
        }
        out.assign(static_cast<size_t>(w) * static_cast<size_t>(h), 0);
        for (int z = 0; z < h; ++z) {
            slime::SlimeEngine::fillSlimeRowBedrock(minZ + z, minX, w, out.data() + static_cast<size_t>(z) * w);
        }
        return true;
    }

private:
    mutable std::atomic<int> m_calls{ 0 };
};

bool same(const std::vector<slime::MatchResult>& a, const std::vector<slime::MatchResult>& b)
{
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (a[i].minX != b[i].minX || a[i].minZ != b[i].minZ) return false;
    }
    return true;
}

} // namespace

int main()
{
    slime::SearchOptions opt;
    opt.version = slime::Version::Bedrock;
    opt.minX = -4096;
    opt.maxX = 4095;
    opt.minZ = -4096;
    opt.maxZ = 4095;
    opt.mode = slime::Mode::Density;
    opt.algorithm = slime::Algorithm::Exact;
    opt.gridSize = 5;
    opt.minDensity = 0.3;
    opt.targetCount = 50;
    opt.threads = 4;

    slime::SearchOutcome cpu;
    const auto cpu0 = std::chrono::steady_clock::now();
    {
        slime::SlimeEngine engine;
        engine.run(opt, std::atomic<bool>{}, slime::ProgressFn(), cpu);
    }
    const double cpuMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - cpu0).count();
    if (!cpu.ok) {
        std::printf("CPU reference failed: %s\n", cpu.error.c_str());
        return 1;
    }

    FailAfterFirstTile fake;
    slime::SearchOptions gpuOpt = opt;
    gpuOpt.gpuScan = &fake;
    slime::SearchOutcome fallback;
    const auto t0 = std::chrono::steady_clock::now();
    {
        slime::SlimeEngine engine;
        engine.run(gpuOpt, std::atomic<bool>{}, slime::ProgressFn(), fallback);
    }
    const auto t1 = std::chrono::steady_clock::now();
    std::printf("cpu=%zu fallback=%zu ok=%d same=%d fallbackReason=%s resumedRows=%lld cpuMs=%.1f elapsed=%.1fms calls=%d\n",
                cpu.results.size(), fallback.results.size(), fallback.ok ? 1 : 0,
                same(cpu.results, fallback.results) ? 1 : 0, fallback.gpuFallbackReason.c_str(),
                static_cast<long long>(fallback.gpuRowsResumed), cpuMs,
                std::chrono::duration<double, std::milli>(t1 - t0).count(), fake.calls());
    if (!fallback.ok || !same(cpu.results, fallback.results)) return 1;
    if (fallback.gpuFallbackReason.empty()) return 1;
    if (fake.calls() < 2) return 1;
    if (fallback.gpuRowsResumed <= 0) {
        std::printf("GPU-RESUME FAIL: no completed rows reused\n");
        return 1;
    }
    std::printf("GPU-RESUME OK\n");
    return 0;
}
