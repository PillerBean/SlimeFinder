// 新框架 Phase 0 对拍：CpuDeterminer vs GpuDeterminer vs 单区块判定。
#include "core/next/Bitmap.h"
#include "gpu/GpuScanAdapter.h"

#include <atomic>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

int main()
{
    std::string gpuErr;
    std::unique_ptr<slime::GpuScan> gpu = slimegpu::createGpuScanAdapter(&gpuErr);
    if (gpu && !gpu->available()) gpu.reset();

    for (slime::Version version : { slime::Version::Java, slime::Version::Bedrock }) {
        slime::SearchOptions opt;
        opt.version = version;
        opt.worldSeed = 12345;
        opt.minX = -1024;
        opt.maxX = 1023;
        opt.minZ = -1024;
        opt.maxZ = 1023;

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
        std::printf("%s CPU=%s %.2fms GPU=%s %.2fms\n", version == slime::Version::Java ? "Java" : "Bedrock",
                    cOk ? "OK" : "FAIL", cs.ms, gOk ? "OK" : "FAIL", gs.ms);
        if (!cOk) {
            std::printf("  CPU error: %s\n", ce.c_str());
            continue;
        }
        if (!gOk) {
            std::printf("  GPU skipped: %s\n", ge.c_str());
            continue;
        }
        size_t mismatch = 0;
        for (size_t i = 0; i < cpu.words().size(); ++i) {
            if (cpu.words()[i] != gpuBmp.words()[i]) ++mismatch;
        }
        std::printf("  words=%zu mismatch=%zu\n", cpu.words().size(), mismatch);

        size_t sampleBad = 0;
        for (int z = opt.minZ; z <= opt.maxZ; z += 37) {
            for (int x = opt.minX; x <= opt.maxX; x += 41) {
                const bool want = slime::SlimeEngine::isSlimeChunk(version, opt.worldSeed, x, z);
                if (want != cpu.isSlime(x, z) || want != gpuBmp.isSlime(x, z)) ++sampleBad;
            }
        }
        std::printf("  sample mismatch=%zu\n", sampleBad);
    }
    return 0;
}
