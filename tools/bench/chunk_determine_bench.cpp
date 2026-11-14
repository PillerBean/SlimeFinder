// Raw slime-chunk determination benchmark (no pattern/density matching).
//
// Measures only per-chunk determination:
//   CPU: SlimeEngine::isSlimeChunkJava / isSlimeChunkBedrockMT over a rectangle
//   GPU: GpuScan::scanRectWords (kernel + readback; the same path the engine uses)
//
// Build: cmake target slime_chunk_bench (links slime_core + slime_gpu).
#include "core/SlimeEngine.h"
#include "gpu/GpuScanAdapter.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#ifdef _OPENMP
#include <omp.h>
#endif

namespace {

using Clock = std::chrono::steady_clock;

double elapsedMs(Clock::time_point a, Clock::time_point b)
{
    return std::chrono::duration<double, std::milli>(b - a).count();
}

double cpuScanMs(slime::Version version, int threads, int w, int h, int reps)
{
    std::vector<uint8_t> out(static_cast<size_t>(w) * h, 0);
    double best = 1e100;
    for (int rep = 0; rep < reps; ++rep) {
        const auto t0 = Clock::now();
#pragma omp parallel for num_threads(threads) schedule(static)
        for (int z = 0; z < h; ++z) {
            for (int x = 0; x < w; ++x) {
                const bool slime = version == slime::Version::Java
                                       ? slime::SlimeEngine::isSlimeChunkJava(123456789, x, z)
                                       : slime::SlimeEngine::isSlimeChunkBedrockMT(x, z);
                out[static_cast<size_t>(z) * w + x] = slime ? 1 : 0;
            }
        }
        const double ms = elapsedMs(t0, Clock::now());
        if (rep > 0) best = std::min(best, ms);
    }
    return best;
}

double cpuBatchScanMs(slime::Version version, int threads, int w, int h, int reps)
{
    std::vector<uint8_t> out(static_cast<size_t>(w) * h, 0);
    double best = 1e100;
    for (int rep = 0; rep < reps; ++rep) {
        const auto t0 = Clock::now();
#pragma omp parallel for num_threads(threads) schedule(static)
        for (int z = 0; z < h; ++z) {
            uint8_t* row = out.data() + static_cast<size_t>(z) * w;
            if (version == slime::Version::Java) {
                slime::SlimeEngine::fillSlimeRowJava(123456789, z, 0, w, row);
            } else {
                slime::SlimeEngine::fillSlimeRowBedrock(z, 0, w, row);
            }
        }
        const double ms = elapsedMs(t0, Clock::now());
        if (rep > 0) best = std::min(best, ms);
    }
    return best;
}

struct GpuResult {
    bool ok = false;
    bool words = false;
    double bestMs = 1e100;
    std::string error;
};

GpuResult gpuScanMs(const slime::GpuScan& gpu, slime::Version version, int w, int h, int reps)
{
    GpuResult r;
    std::vector<uint64_t> words;
    std::vector<uint8_t> bytes;
    std::string err;
    for (int rep = 0; rep < reps; ++rep) {
        const auto t0 = Clock::now();
        bool ok = false;
        if ((w % 64) == 0) {
            ok = gpu.scanRectWords(version, 123456789, 0, 0, w, h, words, &err);
            if (ok) r.words = true;
        }
        if (!ok) {
            ok = gpu.scanRect(version, 123456789, 0, 0, w, h, bytes, &err);
            r.words = false;
        }
        if (!ok) {
            r.error = err.empty() ? "GPU scan failed" : err;
            return r;
        }
        const double ms = elapsedMs(t0, Clock::now());
        if (rep > 0) r.bestMs = std::min(r.bestMs, ms);
    }
    r.ok = true;
    return r;
}

bool validateGpu(const slime::GpuScan& gpu, slime::Version version, int w, int h, std::string* err)
{
    std::vector<uint64_t> words;
    std::string e;
    if (!gpu.scanRectWords(version, 123456789, 0, 0, w, h, words, &e)) {
        if (err) *err = e.empty() ? "GPU words scan failed" : e;
        return false;
    }
    const int wpr = (w + 63) / 64;
    if (words.size() < static_cast<size_t>(wpr) * h) {
        if (err) *err = "GPU words output too small";
        return false;
    }
    for (int z = 0; z < h; ++z) {
        for (int x = 0; x < w; ++x) {
            const bool a = (words[static_cast<size_t>(z) * wpr + x / 64] >> (x % 64)) & 1ULL;
            const bool b = version == slime::Version::Java
                               ? slime::SlimeEngine::isSlimeChunkJava(123456789, x, z)
                               : slime::SlimeEngine::isSlimeChunkBedrockMT(x, z);
            if (a != b) {
                if (err) *err = "GPU/CPU mismatch at " + std::to_string(x) + "," + std::to_string(z);
                return false;
            }
        }
    }
    return true;
}

const char* versionName(slime::Version v)
{
    return v == slime::Version::Java ? "Java" : "Bedrock";
}

void printRow(const char* version, const char* backend, int w, int h, double ms, double scalarMs = 0.0)
{
    const double chunks = static_cast<double>(w) * h;
    const double rate = ms > 0.0 ? chunks / (ms / 1000.0) : 0.0;
    const double scalarRate = scalarMs > 0.0 ? chunks / (scalarMs / 1000.0) : 0.0;
    if (scalarMs > 0.0) {
        std::printf("%-7s %-9s %6d x %-6d %10.0f  %10.3f  %10.2f  %10.2f\n", version, backend, w, h,
                    chunks, ms, rate / 1e8, scalarRate / 1e8);
    } else {
        std::printf("%-7s %-9s %6d x %-6d %10.0f  %10.3f  %10.2f  %10s\n", version, backend, w, h,
                    chunks, ms, rate / 1e8, "-");
    }
}

} // namespace

int main()
{
    int threads = static_cast<int>(std::thread::hardware_concurrency());
    if (threads < 1) threads = 1;
#ifdef _OPENMP
    threads = std::min(threads, omp_get_max_threads());
#endif

    std::string gpuErr;
    std::unique_ptr<slime::GpuScan> gpu = slimegpu::createGpuScanAdapter(&gpuErr);
    if (!gpu || !gpu->available()) {
        std::fprintf(stderr, "GPU unavailable: %s\n", gpuErr.c_str());
        gpu.reset();
    }

    std::printf("SlimeFinder v3.12.3 raw chunk determination benchmark\n");
    std::printf("threads=%d\n", threads);
    if (gpu) {
        std::printf("gpu=%s\n", gpu->deviceName().c_str());
        std::printf("compute_units=%u vram_mb=%.0f\n", gpu->computeUnits(),
                    static_cast<double>(gpu->globalMemBytes()) / (1024.0 * 1024.0));
    }

    if (gpu) {
        std::string verr;
        if (!validateGpu(*gpu, slime::Version::Java, 1024, 1024, &verr)) {
            std::fprintf(stderr, "GPU validation failed: %s\n", verr.c_str());
        } else {
            std::printf("gpu_validation=OK (Java 1024x1024)\n");
        }
        if (!validateGpu(*gpu, slime::Version::Bedrock, 1024, 1024, &verr)) {
            std::fprintf(stderr, "GPU Bedrock validation failed: %s\n", verr.c_str());
        } else {
            std::printf("gpu_validation=OK (Bedrock 1024x1024)\n");
        }
    }

    std::printf("\n%-7s %-9s %-15s %10s  %10s  %10s  %10s\n", "version", "backend", "size", "chunks",
                "ms", "亿/s", "逐块/s");
    std::printf("-------------------------------------------------------------------------------\n");

    const int sizes[][2] = {
        { 2048, 2048 },   //  4.19M
        { 4096, 4096 },   // 16.78M
        { 8192, 4096 },   // 33.55M
    };
    for (slime::Version version : { slime::Version::Java, slime::Version::Bedrock }) {
        for (const auto& size : sizes) {
            const int w = size[0];
            const int h = size[1];
            const double cpuBatch = cpuBatchScanMs(version, threads, w, h, 4);
            const double cpuScalar = cpuScanMs(version, threads, w, h, 2);
            printRow(versionName(version), "CPU-row", w, h, cpuBatch, cpuScalar);
        }
    }
    if (gpu) {
        for (slime::Version version : { slime::Version::Java, slime::Version::Bedrock }) {
            for (const auto& size : sizes) {
                const int w = size[0];
                const int h = size[1];
                GpuResult r = gpuScanMs(*gpu, version, w, h, 4);
                if (!r.ok) {
                    std::fprintf(stderr, "GPU %s %dx%d failed: %s\n", versionName(version), w, h,
                                 r.error.c_str());
                    continue;
                }
                printRow(versionName(version), r.words ? "GPUw" : "GPU", w, h, r.bestMs);
            }
        }
    }
    return 0;
}
