// k3_prescan_parity_probe.cpp - k3/环形预筛 GPU 与缓存位图两条路径的对拍探针
//
// 目的：在同一份真实 Bedrock 位图上比较
//   GpuScan::prescanDensitySeeds（GPU 全图预筛）与
//   slime::cachePrescanSeeds（缓存位图 CPU 预筛）
// 的候选种子是否逐条一致，并记录两者耗时/加速比。
//
// 复现：
//   build-k3\k3_prescan_parity_probe.exe
//   （无可用 OpenCL 设备时打印“GPU 不可用”并返回 2；数据写入 docs/planning/k3_cache_prescan.md）
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "core/CacheManager.h"
#include "core/CachePrescan.h"
#include "core/SlimeEngine.h"
#include "gpu/GpuScanAdapter.h"

using namespace slime;

namespace {

struct RectI {
    int minX = 0;
    int maxX = 0;
    int minZ = 0;
    int maxZ = 0;
};

struct Case {
    const char* tag;
    double minDensity;
    int bandRows;
    int topPerBand;
    int maxSeeds;
    int minSeparation;
};

bool sameSeeds(const std::vector<DensitySeed>& a, const std::vector<DensitySeed>& b)
{
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (a[i].cx != b[i].cx || a[i].cz != b[i].cz || a[i].count != b[i].count) return false;
    }
    return true;
}

void reportDiff(const std::vector<DensitySeed>& a, const std::vector<DensitySeed>& b, const char* tag)
{
    std::printf("  [%s] 不一致：gpu=%zu cache=%zu\n", tag, a.size(), b.size());
    const size_t n = (std::min)(a.size(), b.size());
    size_t diff = 0;
    for (size_t i = 0; i < n; ++i) {
        if (a[i].cx != b[i].cx || a[i].cz != b[i].cz || a[i].count != b[i].count) {
            if (diff < 5) {
                std::printf("    首个差异 #%zu：gpu=(%d,%d,c=%d) cache=(%d,%d,c=%d)\n", i, a[i].cx,
                            a[i].cz, a[i].count, b[i].cx, b[i].cz, b[i].count);
            }
            ++diff;
        }
    }
    std::printf("    逐条差异数=%zu\n", diff);
}

int runCase(slime::GpuScan& gpu, const CacheManager& cache, const RectI& r, int64_t seed, const Case& c)
{
    DensityPrescanParams pp;
    pp.version = Version::Bedrock;
    pp.worldSeed = seed;
    pp.minX = r.minX;
    pp.maxX = r.maxX;
    pp.minZ = r.minZ;
    pp.maxZ = r.maxZ;
    pp.gridSize = 15;
    pp.minDensity = c.minDensity;
    pp.bandRows = c.bandRows;
    pp.topPerBand = c.topPerBand;
    pp.maxSeeds = c.maxSeeds;
    pp.minSeparation = c.minSeparation;
    std::atomic<bool> cancel{ false };
    std::vector<DensitySeed> gpuSeeds, cacheSeeds;
    std::string gerr;
    std::string cerr;

    // GPU 先跑一次做热身/编译内核，再计时 3 次取最短（小区域下内核/上下文开销占主导）。
    gpu.prescanDensitySeeds(pp, cancel, gpuSeeds, &gerr);
    double gpuMs = 1e18;
    bool okGpu = false;
    for (int rep = 0; rep < 3; ++rep) {
        gpuSeeds.clear();
        const auto t0 = std::chrono::steady_clock::now();
        okGpu = gpu.prescanDensitySeeds(pp, cancel, gpuSeeds, &gerr);
        gpuMs = (std::min)(gpuMs, std::chrono::duration<double, std::milli>(
                                       std::chrono::steady_clock::now() - t0)
                                       .count());
    }
    // 缓存版同样热身 1 次 + 计时 3 次取最短。
    cachePrescanSeeds(cache, pp, nullptr, cacheSeeds, &cerr);
    double cacheMs = 1e18;
    bool okCache = false;
    for (int rep = 0; rep < 3; ++rep) {
        cacheSeeds.clear();
        const auto t1 = std::chrono::steady_clock::now();
        okCache = cachePrescanSeeds(cache, pp, nullptr, cacheSeeds, &cerr);
        cacheMs = (std::min)(cacheMs, std::chrono::duration<double, std::milli>(
                                          std::chrono::steady_clock::now() - t1)
                                          .count());
    }

    std::printf("\n[%s] 区域 %dx%d 阈值=%.2f bandRows=%d topPerBand=%d sep=%d maxSeeds=%d\n", c.tag,
                r.maxX - r.minX + 1, r.maxZ - r.minZ + 1, c.minDensity, c.bandRows, c.topPerBand,
                c.minSeparation, c.maxSeeds);
    std::printf("  GPU  : ok=%d 种子=%zu 耗时=%.1f ms（%s）\n", static_cast<int>(okGpu),
                gpuSeeds.size(), gpuMs, gerr.c_str());
    std::printf("  缓存 : ok=%d 种子=%zu 耗时=%.1f ms（%s）\n", static_cast<int>(okCache),
                cacheSeeds.size(), cacheMs, cerr.c_str());
    std::printf("  加速比：%.1fx\n", cacheMs > 0.0 ? gpuMs / cacheMs : 0.0);
    if (!okGpu || !okCache) return 1;
    const bool same = sameSeeds(gpuSeeds, cacheSeeds);
    std::printf("  逐种子一致：%s\n", same ? "是" : "否");
    if (!same) {
        reportDiff(gpuSeeds, cacheSeeds, c.tag);
        return 1;
    }
    return 0;
}

} // namespace

int main()
{
    std::string err;
    auto gpu = slimegpu::createGpuScanAdapter(&err);
    if (!gpu || !gpu->available()) {
        std::printf("GPU 不可用: %s\n", err.empty() ? "-" : err.c_str());
        return 2;
    }
    std::printf("GPU: %s\n", gpu->deviceName().c_str());

    const int64_t seed = 2229010591266423951LL;
    int failures = 0;
    const Case cases[2] = {
        // A：topPerBand 足够大 ⇒ 无每带截断，候选集/顺序/去重应逐条一致。
        { "A 无截断", 0.15, 64, 1000000, 4000, 16 },
        // B：GUI 生产参数 ⇒ 若跨 topPerBand 边界出现 count 并列，取舍由实现决定（本机数据无差异）。
        { "B 生产参数", 0.18, 4096, 512, 4000, 16 },
    };

    const RectI regions[2] = { { -1024, 1023, -1024, 1023 }, { 0, 8191, 0, 8191 } };
    for (const RectI& r : regions) {
        const int w = r.maxX - r.minX + 1;
        const int h = r.maxZ - r.minZ + 1;
        const int wpr = (w + 63) / 64;
        // 同一事实源：GPU 扫描位图（行主序打包字）→ CacheManager
        std::vector<uint64_t> words;
        const auto tScan0 = std::chrono::steady_clock::now();
        if (!gpu->scanRectWords(Version::Bedrock, seed, r.minX, r.minZ, w, h, words, &err)) {
            std::printf("GPU 位图扫描失败: %s\n", err.c_str());
            return 3;
        }
        const double scanMs = std::chrono::duration<double, std::milli>(
                                  std::chrono::steady_clock::now() - tScan0)
                                  .count();
        if (words.size() != static_cast<size_t>(wpr) * static_cast<size_t>(h)) {
            std::printf("位图尺寸不符：words=%zu 期望=%lld\n", words.size(),
                        static_cast<long long>(wpr) * h);
            return 3;
        }
        CacheManager cache;
        if (!cache.addPackedWords(Rect{ r.minX, r.maxX, r.minZ, r.maxZ }, words.data(), words.size())) {
            std::printf("CacheManager.addPackedWords 失败\n");
            return 3;
        }
        long long slime = 0;
        for (uint64_t v : words) slime += __popcnt64(v);
        std::printf("\n=== 区域 %dx%d：GPU 扫描位图 %.1f ms，史莱姆 %lld 个（%.2f%%）===\n", w, h, scanMs,
                    slime, 100.0 * static_cast<double>(slime) / (static_cast<double>(w) * h));
        for (const Case& c : cases) failures += runCase(*gpu, cache, r, seed, c);
    }
    std::printf("\n对拍结果：%s\n", failures == 0 ? "全部一致 ✔" : "存在差异 ✘");
    return failures == 0 ? 0 : 1;
}
