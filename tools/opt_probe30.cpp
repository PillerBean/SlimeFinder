// opt_probe30.cpp - 定位「GPU 流式密度漏判 213 条」：逐坐标 diff
//   直接调用适配器的 matchDensity（=产品流式路径），与 CPU 全量结果比对。
#include "core/SlimeEngine.h"
#include "gpu/GpuScanAdapter.h"

#include <algorithm>
#include <iterator>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

namespace {
struct Hit {
    int x = 0, z = 0;
    bool operator<(const Hit& o) const { return x != o.x ? x < o.x : z < o.z; }
    bool operator==(const Hit& o) const { return x == o.x && z == o.z; }
};

bool javaSlime(long long seed, int cx, int cz)
{
    const uint32_t czu = static_cast<uint32_t>(cz);
    const uint32_t t3u = czu * czu;
    const int32_t t3s = static_cast<int32_t>(t3u);
    const int32_t t4s = static_cast<int32_t>(czu * 389711u);
    uint64_t b = static_cast<uint64_t>(seed);
    b += static_cast<uint64_t>(static_cast<int64_t>(t3s) * 4392871LL);
    b += static_cast<uint64_t>(static_cast<int64_t>(t4s));
    const uint32_t xu = static_cast<uint32_t>(cx);
    const uint32_t t1u = (xu * xu) * 4987142u;
    const uint32_t t2u = xu * 5947611u;
    uint64_t s = b + static_cast<uint64_t>(static_cast<int64_t>(static_cast<int32_t>(t1u))) +
                 static_cast<uint64_t>(static_cast<int64_t>(static_cast<int32_t>(t2u)));
    s ^= 987234911ULL;
    uint64_t lcg = (s ^ 0x5DEECE66DULL) & 0xFFFFFFFFFFFFULL;
    lcg = (lcg * 0x5DEECE66DULL + 0xBULL) & 0xFFFFFFFFFFFFULL;
    return (static_cast<uint32_t>(lcg >> 17u) % 10u) == 0u;
}
} // namespace

int main(int argc, char** argv)
{
    const int W = argc > 1 ? std::atoi(argv[1]) : 2048;
    const int H = argc > 2 ? std::atoi(argv[2]) : 1024;
    const int g = 2;
    const int thr = 2;
    const long long seed = 12345;
    const int x0 = argc > 3 ? std::atoi(argv[3]) : 0;
    const int z0 = argc > 4 ? std::atoi(argv[4]) : 0;

    // CPU 全量命中
    std::vector<uint8_t> map(static_cast<size_t>(W) * H, 0);
    for (int z = 0; z < H; ++z)
        for (int x = 0; x < W; ++x) map[static_cast<size_t>(z) * W + x] = javaSlime(seed, x0 + x, z0 + z) ? 1 : 0;
    std::vector<Hit> cpu;
    for (int z = 0; z + g <= H; ++z)
        for (int x = 0; x + g <= W; ++x) {
            int c = 0;
            for (int dz = 0; dz < g; ++dz)
                for (int dx = 0; dx < g; ++dx) c += map[static_cast<size_t>(z + dz) * W + (x + dx)];
            if (c >= thr) cpu.push_back(Hit{ x0 + x, z0 + z });
        }

    std::string err;
    std::unique_ptr<slime::GpuScan> gpu = slimegpu::createGpuScanAdapter(&err);
    if (!gpu) {
        std::printf("GPU 不可用：%s\n", err.c_str());
        return 0;
    }
    slime::DensityMatchParams p;
    p.version = slime::Version::Java;
    p.worldSeed = seed;
    p.axMin = x0;
    p.axMax = x0 + W - g;
    p.azMin = z0;
    p.azMax = z0 + H - g;
    p.gridSize = g;
    p.minDensity = 0.5;
    p.maxResults = 4000000;
    std::vector<slime::MatchResult> res;
    std::atomic<bool> cancel{ false };
    if (!gpu->matchDensity(p, cancel, res, &err)) {
        std::printf("GPU 流式匹配失败：%s\n", err.c_str());
        return 0;
    }
    std::vector<Hit> gh;
    gh.reserve(res.size());
    for (const auto& m : res) gh.push_back(Hit{ m.minX, m.minZ });
    std::sort(cpu.begin(), cpu.end());
    std::sort(gh.begin(), gh.end());
    cpu.erase(std::unique(cpu.begin(), cpu.end()), cpu.end());
    gh.erase(std::unique(gh.begin(), gh.end()), gh.end());

    std::printf("  CPU 前 5：");
    for (size_t i = 0; i < cpu.size() && i < 5; ++i) std::printf("(%d,%d) ", cpu[i].x, cpu[i].z);
    std::printf("  GPU 前 5：");
    for (size_t i = 0; i < gh.size() && i < 5; ++i) std::printf("(%d,%d) ", gh[i].x, gh[i].z);
    std::printf("\n");
    std::vector<Hit> missing, extra;
    std::set_difference(cpu.begin(), cpu.end(), gh.begin(), gh.end(), std::back_inserter(missing));
    std::set_difference(gh.begin(), gh.end(), cpu.begin(), cpu.end(), std::back_inserter(extra));
    std::printf("区域 %dx%d g=%d 阈值 %d：CPU %zu，GPU(最近N条) %zu，漏 %zu，多 %zu\n", W, H, g, thr, cpu.size(),
                gh.size(), missing.size(), extra.size());
    // v3.9.8：GPU 流式只回读最近 keepTarget 条（5 万），因此校验：GPU 结果必须全部出现在 CPU 结果里，
    //   且 GPU 条数应为 min(CPU, 50000) 附近；不再要求全量相等。
    std::vector<Hit> multi;
    std::set_difference(gh.begin(), gh.end(), cpu.begin(), cpu.end(), std::back_inserter(multi));
    std::printf("  GPU 结果不在 CPU 结果里的条数 = %zu（应为 0）\n", multi.size());
    // 漏判点的分布特征（看是否集中在 tile 边界：x%255、z%31）
    int nearX = 0, nearZ = 0, edgeX = 0, edgeZ = 0;
    for (size_t i = 0; i < missing.size() && i < 20; ++i) {
        std::printf("  漏 (%d, %d)  x%%255=%d  z%%31=%d\n", missing[i].x, missing[i].z,
                    ((missing[i].x - x0) % 255 + 255) % 255, ((missing[i].z - z0) % 31 + 31) % 31);
    }
    for (const Hit& h : missing) {
        const int mx = ((h.x - x0) % 255 + 255) % 255;
        const int mz = ((h.z - z0) % 31 + 31) % 31;
        if (mx >= 250) ++nearX;
        if (mz >= 26) ++nearZ;
        if (h.x - x0 >= W - g - 1) ++edgeX;
        if (h.z - z0 >= H - g - 1) ++edgeZ;
    }
    std::printf("  漏判点分布：x 接近 tile 右沿 %d，z 接近 tile 下沿 %d，x 贴区域右沿 %d，z 贴区域下沿 %d\n",
                nearX, nearZ, edgeX, edgeZ);
    return 0;
}
