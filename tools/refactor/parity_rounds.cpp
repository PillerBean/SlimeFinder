// Phase 0/1 对拍：4 轮确定性区域，CPU/GPU 位图逐字一致 + 独立标量抽样/小块全量。
#include "core/next/Bitmap.h"
#include "core/next/Plan.h"
#include "gpu/GpuScanAdapter.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

namespace {

struct Case {
    slime::Version version;
    int minX, minZ, w, h;
    int64_t seed;
    const char* name;
};

bool compareBitmap(const slime::next::Bitmap& a, const slime::next::Bitmap& b, size_t* wordsBad)
{
    *wordsBad = 0;
    if (a.words().size() != b.words().size()) return false;
    for (size_t i = 0; i < a.words().size(); ++i) {
        if (a.words()[i] != b.words()[i]) ++*wordsBad;
    }
    return *wordsBad == 0;
}

size_t sampleMismatch(const slime::next::Bitmap& bmp, const Case& c, int step)
{
    size_t bad = 0;
    for (int z = c.minZ; z < c.minZ + c.h; z += step) {
        for (int x = c.minX; x < c.minX + c.w; x += step) {
            const bool want = slime::SlimeEngine::isSlimeChunk(c.version, c.seed, x, z);
            if (want != bmp.isSlime(x, z)) ++bad;
        }
    }
    return bad;
}

size_t fullSmallMismatch(const slime::next::Bitmap& bmp, const Case& c, int sw, int sh)
{
    size_t bad = 0;
    const int x1 = c.minX + (std::min)(sw, c.w);
    const int z1 = c.minZ + (std::min)(sh, c.h);
    for (int z = c.minZ; z < z1; ++z) {
        for (int x = c.minX; x < x1; ++x) {
            const bool want = slime::SlimeEngine::isSlimeChunk(c.version, c.seed, x, z);
            if (want != bmp.isSlime(x, z)) ++bad;
        }
    }
    return bad;
}

} // namespace

int main()
{
    std::string gpuErr;
    std::unique_ptr<slime::GpuScan> gpu = slimegpu::createGpuScanAdapter(&gpuErr);
    if (gpu && !gpu->available()) gpu.reset();
    std::printf("GPU: %s\n", gpu ? gpu->deviceName().c_str() : ("unavailable: " + gpuErr).c_str());

    const Case cases[] = {
        { slime::Version::Java, -200000, 300000, 777, 333, -987654321098765LL, "Java-far-odd" },
        { slime::Version::Bedrock, -8192, -8192, 1024, 1024, 12345, "Bedrock-1M" },
        { slime::Version::Java, -1024, -1024, 2048, 512, 42, "Java-wide" },
        { slime::Version::Bedrock, -5000, 7000, 1000, 700, 0, "Bedrock-offset" },
    };

    int failures = 0;
    for (int round = 0; round < 4; ++round) {
        const Case& c = cases[round];
        slime::SearchOptions opt;
        opt.version = c.version;
        opt.worldSeed = c.seed;
        opt.minX = c.minX;
        opt.maxX = c.minX + c.w - 1;
        opt.minZ = c.minZ;
        opt.maxZ = c.minZ + c.h - 1;

        std::atomic<bool> cancel{ false };
        slime::next::Bitmap cpu, gpuBmp;
        slime::next::ScanStats cs, gs;
        std::string ce, ge;
        slime::next::CpuDeterminer cd;
        const bool cOk = cd.run(opt, cancel, cpu, cs, &ce);
        bool gOk = false;
        if (gpu) {
            slime::next::GpuDeterminer gd(gpu.get());
            gOk = gd.run(opt, cancel, gpuBmp, gs, &ge);
        }
        if (!cOk) {
            std::printf("ROUND %d %s CPU FAIL: %s\n", round + 1, c.name, ce.c_str());
            ++failures;
            continue;
        }

        size_t wordsBad = 0;
        bool wordsOk = false;
        if (gOk) wordsOk = compareBitmap(cpu, gpuBmp, &wordsBad);
        const size_t sampleBad = gOk ? sampleMismatch(cpu, c, 13) : 0;
        const size_t fullBad = fullSmallMismatch(cpu, c, 64, 64);

        std::printf("ROUND %d %-14s CPU=%s %.2fms GPU=%s %.2fms words_bad=%zu sample_bad=%zu full64_bad=%zu\n",
                    round + 1, c.name, cOk ? "OK" : "FAIL", cs.ms, gOk ? "OK" : "SKIP", gs.ms,
                    wordsBad, sampleBad, fullBad);
        if (!gOk) std::printf("  GPU skip: %s\n", ge.c_str());
        if (wordsBad != 0) {
            int shown = 0;
            const int wpr = cpu.wordsPerRow();
            for (int z = 0; z < c.h && shown < 6; ++z) {
                for (int wi = 0; wi < wpr && shown < 6; ++wi) {
                    const size_t i = static_cast<size_t>(z) * wpr + wi;
                    if (cpu.words()[i] != gpuBmp.words()[i]) {
                        std::printf("  mismatch row=%d word=%d cpu=%016llx gpu=%016llx\n", z, wi,
                                    static_cast<unsigned long long>(cpu.words()[i]),
                                    static_cast<unsigned long long>(gpuBmp.words()[i]));
                        ++shown;
                    }
                }
            }
        }
        if (!gOk || !wordsOk || sampleBad != 0 || fullBad != 0) ++failures;
    }
    std::printf("PARITY %s (%d failures)\n", failures == 0 ? "ALL PASS" : "FAIL", failures);
    return failures == 0 ? 0 : 1;
}
