// GPU-only 基准：设备时间、回读、命中量、工作组占用代理。
#include "gpu/GpuSession.h"
#include "core/SlimeEngine.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace {

double nowMs(std::chrono::steady_clock::time_point a, std::chrono::steady_clock::time_point b)
{
    return std::chrono::duration<double, std::milli>(b - a).count();
}

void printStats(const char* name, const slimegpu::GpuSession::LastMatchStats& st,
                const slimegpu::GpuSession& s)
{
    const auto& d = s.device();
    const double wgRatio = d.maxWorkGroupSize ? static_cast<double>(st.localSize) / d.maxWorkGroupSize : 0.0;
    const double waves = d.maxComputeUnits ? static_cast<double>(st.workGroups) / d.maxComputeUnits : 0.0;
    const double busy = st.totalMs > 0 ? st.kernelMs / st.totalMs : 0.0;
    std::printf("%-22s total=%8.2fms kernel=%8.2fms counter=%6.2f hits=%6.2f select=%6.2f "
                "hits=%u/%u disp=%u wg=%zu local=%zu wgSize=%.2f waves=%.1f busy=%.1f%%\n",
                name, st.totalMs, st.kernelMs, st.counterReadMs, st.hitsReadMs, st.selectMs, st.returnedHits,
                st.totalHits, st.dispatches, st.workGroups, st.localSize, wgRatio, waves, busy * 100.0);
}

} // namespace

int main()
{
    setvbuf(stdout, nullptr, _IONBF, 0);
    _putenv_s("SLIME_REUSE_BITMAP", "0");
    slimegpu::GpuSession session;
    std::string err;
    if (!session.init(&err)) {
        std::printf("GPU init failed: %s\n", err.c_str());
        return 1;
    }
    const auto& d = session.device();
    std::printf("GPU-only bench\n");
    std::printf("device=%s CU=%llu clock=%uMHz global=%lluMiB local=%lluKiB maxWG=%llu\n", d.name.c_str(),
                static_cast<unsigned long long>(d.maxComputeUnits), d.maxClockMhz,
                static_cast<unsigned long long>(d.globalMemBytes >> 20),
                static_cast<unsigned long long>(d.localMemBytes >> 10),
                static_cast<unsigned long long>(d.maxWorkGroupSize));
    std::printf("legend: kernel=dispatch+wait, counter/hits=readback, select=CPU select, "
                "busy=kernel/total, waves=WG/CU\n");

    std::atomic<bool> cancel{ false };
    for (auto version : { slime::Version::Java, slime::Version::Bedrock }) {
        for (int size : { 512, 1024, 2048 }) {
            const int g = 5;
            const int minX = -size / 2, minZ = -size / 2;
            slime::DensityMatchParams p;
            p.version = version;
            p.worldSeed = 12345;
            p.axMin = minX;
            p.axMax = minX + size - g;
            p.azMin = minZ;
            p.azMax = minZ + size - g;
            p.gridSize = g;
            p.minDensity = 0.3;
            p.maxResults = 4000000;
            p.startX = 0;
            p.startZ = 0;
            p.keepCandidates = 256;
            uint32_t total = 0;
            std::vector<slimegpu::GpuSession::StreamDensityHit> hits;
            // warmup
            (void)session.matchDensityStream(p.worldSeed, version == slime::Version::Bedrock, g, 8, size, size,
                                             p.axMin, p.axMax, p.azMin, p.azMax, p.maxResults, 0, 0, cancel,
                                             hits, &err, &total, false, 0, p.keepCandidates);
            const auto a = std::chrono::steady_clock::now();
            const bool ok = session.matchDensityStream(
                p.worldSeed, version == slime::Version::Bedrock, g, 8, size, size, p.axMin, p.axMax, p.azMin,
                p.azMax, p.maxResults, 0, 0, cancel, hits, &err, &total, false, 0, p.keepCandidates);
            const auto b = std::chrono::steady_clock::now();
            if (!ok) {
                std::printf("density %s %d failed: %s\n",
                            version == slime::Version::Java ? "Java" : "Bedrock", size, err.c_str());
                continue;
            }
            char label[80];
            std::snprintf(label, sizeof(label), "density %s %d",
                          version == slime::Version::Java ? "Java" : "Bedrock", size);
            auto st = session.lastMatchStats();
            st.totalMs = nowMs(a, b); // host wall includes everything
            printStats(label, st, session);
            std::printf("  raw total=%lld hits=%zu\n", static_cast<long long>(total), hits.size());
        }
    }

    for (auto version : { slime::Version::Java, slime::Version::Bedrock }) {
        for (int size : { 1024, 2048 }) {
            const int rows = 2, cols = 2;
            const int minX = -size / 2, minZ = -size / 2;
            const int axMin = minX, axMax = minX + size - cols;
            const int azMin = minZ, azMax = minZ + size - rows;
            const int w = axMax - axMin + cols;
            const int h = azMax - azMin + rows;
            uint32_t en[2] = { 3u, 3u };
            uint32_t gr[2] = { 3u, 3u };
            std::vector<slimegpu::GpuSession::StreamDensityHit> hits;
            (void)session.patternStream(12345, version == slime::Version::Bedrock, rows, cols, w, h, axMin,
                                       axMax, azMin, azMax, en, gr, 4000000, cancel, hits, &err);
            const auto a = std::chrono::steady_clock::now();
            const bool ok = session.patternStream(12345, version == slime::Version::Bedrock, rows, cols, w, h,
                                                  axMin, axMax, azMin, azMax, en, gr, 4000000, cancel, hits,
                                                  &err);
            const auto b = std::chrono::steady_clock::now();
            if (!ok) {
                std::printf("pattern %s %d failed: %s\n",
                            version == slime::Version::Java ? "Java" : "Bedrock", size, err.c_str());
                continue;
            }
            char label[80];
            std::snprintf(label, sizeof(label), "pattern2x2 %s %d",
                          version == slime::Version::Java ? "Java" : "Bedrock", size);
            auto st = session.lastMatchStats();
            st.totalMs = nowMs(a, b);
            printStats(label, st, session);
        }
    }
    return 0;
}
