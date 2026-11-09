// opt_probe25.cpp - 「全 GPU 流式匹配」可行性（不动产品代码）
//   内核在 GPU 内完成「区块判定 + 密度窗口统计 + 只回传命中」，不回传位图、不在 CPU 上匹配。
//   用于验证：能否把引擎级效率从 96 亿/秒 推近内核实测的 1200 亿/秒。
//
//   结构：work-group 处理 TILE_W x TILE_H 区块，
//     ① 每个线程算 32 个连续区块（一个 32 位字）→ 写共享位图（无原子、无重复）
//     ② 屏障后，每个线程按锚点从共享位图求 g×g 窗口计数，达阈值就原子占位写回命中
#include "gpu/ClApi.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cmath>
#include <utility>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace {

struct IPair {
    int x = 0, y = 0;
};

using Clock = std::chrono::steady_clock;
double msSince(Clock::time_point t0)
{
    return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

const char* kSrc = R"CL(
static inline long sext32(int v) { return (long)v; }

static inline ulong java_row_base(long worldSeed, int cz)
{
    uint czu = (uint)cz;
    uint t3u = czu * czu;
    long t3s = sext32((int)t3u);
    uint t4u = czu * 389711u;
    long t4s = sext32((int)t4u);
    ulong b = (ulong)worldSeed;
    b += (ulong)((ulong)t3s * 4392871UL);
    b += (ulong)t4s;
    return b;
}

static inline ulong java_x_tail(int x)
{
    uint xu = (uint)x;
    uint t1u = (xu * xu) * 4987142u;
    uint t2u = xu * 5947611u;
    return (ulong)(long)(int)t1u + (ulong)(long)(int)t2u;
}

static inline int java_hit_word(ulong rowBase, int x0)
{
    uint bits = 0;
    for (int b = 0; b < 32; ++b) {
        ulong s = rowBase + java_x_tail(x0 + b);
        s ^= 987234911UL;
        ulong lcg = (s ^ 0x5DEECE66DUL) & 0xFFFFFFFFFFFFUL;
        lcg = (lcg * 0x5DEECE66DUL + 0xBUL) & 0xFFFFFFFFFFFFUL;
        uint q = (uint)(lcg >> 17u);
        bits |= (uint)((q % 10u) == 0u ? 1u : 0u) << b;
    }
    return (int)bits;
}

#define TILE_W 256
#define TILE_H 32
#define WORDS (TILE_W / 32)          // 8 个字/行
#define SHARED_WORDS (WORDS * TILE_H) // 256 个字

__kernel void density_stream(long seed, int minX, int minZ, int w, int h,
                             int g, int thr, uint maxHits,
                             __global int2* hits, __global uint* counter)
{
    __local uint bits[SHARED_WORDS];
    const int lid = (int)get_local_id(0);
    const int strideX = TILE_W - g + 1;
    const int strideZ = TILE_H - g + 1;
    const int tilesX = (w - g + strideX) / strideX;   // ceil((w-g+1)/strideX)
    const int tileIdx = (int)get_group_id(0);
    const int tx = tileIdx % tilesX;
    const int tz = tileIdx / tilesX;
    const int ox = tx * strideX;
    const int oz = tz * strideZ;

    // ① 每个线程算一个字（32 个连续区块）
    {
        const int wordIdx = lid;             // 0..255
        const int lz = wordIdx / WORDS;
        const int wx = (wordIdx % WORDS) * 32;
        const int cz = oz + lz;
        const int cx0 = minX + ox + wx;
        if (cz < h && ox + wx < w) {
            bits[wordIdx] = (uint)java_hit_word(java_row_base(seed, minZ + cz), cx0);
        } else {
            bits[wordIdx] = 0u;
        }
    }
    barrier(CLK_LOCAL_MEM_FENCE);

    // ② v3.8.2 试验 A1：可分离卷积 —— 先算「每行的 g 连加」(hsum)，锚点只做 g 次加法
    const int axMax = TILE_W - g;
    const int azMax = TILE_H - g;
    __local uchar hsum[TILE_H * TILE_W];
    const uint mask = (g >= 32) ? 0xFFFFFFFFu : ((1u << g) - 1u);
    for (int i = lid; i < TILE_H * (axMax + 1); i += 256) {
        const int ly = i / (axMax + 1);
        const int ax = i - ly * (axMax + 1);
        const int rowBase = ly * WORDS;
        const int word = ax >> 5;
        const int shift = ax & 31;
        uint v;
        if (shift + g <= 32) {
            v = (bits[rowBase + word] >> shift) & mask;
        } else {
            v = ((bits[rowBase + word] >> shift) | (bits[rowBase + word + 1] << (32 - shift))) & mask;
        }
        hsum[i] = (uchar)popcount(v);
    }
    barrier(CLK_LOCAL_MEM_FENCE);

    for (int az = 0; az <= azMax; ++az) {
        for (int ax = lid; ax <= axMax; ax += 256) {
            const int cx = ox + ax;
            const int cz = oz + az;
            if (cx + g > w || cz + g > h) continue;
            int cnt = 0;
            for (int dz = 0; dz < g; ++dz) cnt += (int)hsum[(az + dz) * (axMax + 1) + ax];
            if (cnt >= thr) {
                const uint slot = atomic_inc(counter);
                if (slot < maxHits) {
                    hits[slot] = (int2)(minX + cx, minZ + cz);
                }
            }
        }
    }
}
)CL";

// CPU 参照：与内核同构的窗口计数（用于校验）
int cpuCount(const std::vector<uint8_t>& map, int w, int h, int x, int z, int g)
{
    int c = 0;
    for (int dz = 0; dz < g; ++dz)
        for (int dx = 0; dx < g; ++dx) c += map[static_cast<size_t>(z + dz) * w + (x + dx)];
    return c;
}

} // namespace

int main(int argc, char** argv)
{
    slimegpu::ClApi api;
    std::vector<std::string> mods;
    slimegpu::collectCandidateModules(mods);
    for (const std::string& m : mods) {
        if (api.openModule(m)) break;
    }
    if (!api.ok) {
        std::printf("OpenCL 不可用\n");
        return 0;
    }
    cl_platform_id plat = nullptr;
    cl_device_id dev = nullptr;
    cl_uint np = 0;
    if (api.pfnGetPlatformIDs(1, &plat, &np) != CL_SUCCESS || np == 0 ||
        api.pfnGetDeviceIDs(plat, CL_DEVICE_TYPE_ALL, 1, &dev, nullptr) != CL_SUCCESS) {
        std::printf("找不到设备\n");
        return 0;
    }
    cl_int rc = CL_SUCCESS;
    cl_context ctx = api.pfnCreateContext(nullptr, 1, &dev, nullptr, nullptr, &rc);
    cl_command_queue q = api.pfnCreateCommandQueue(ctx, dev, 0, &rc);
    cl_program prog = api.pfnCreateProgramWithSource(ctx, 1, &kSrc, nullptr, &rc);
    if (!prog || api.pfnBuildProgram(prog, 1, &dev, nullptr, nullptr, nullptr) != CL_SUCCESS) {
        size_t n = 0;
        api.pfnGetProgramBuildInfo(prog, dev, CL_PROGRAM_BUILD_LOG, 0, nullptr, &n);
        std::string log(n, '\0');
        api.pfnGetProgramBuildInfo(prog, dev, CL_PROGRAM_BUILD_LOG, n, log.data(), &n);
        std::printf("内核编译失败:\n%s\n", log.c_str());
        return 0;
    }
    cl_kernel k = api.pfnCreateKernel(prog, "density_stream", &rc);

    // ---- 正确性：2048x1024，g=5，阈值 8（= 0.32 密度）----
    {
        const int W = 2048, H = 1024, g = 5, thr = 8;
        const long long seed = 777;
        const int minX = 0, minZ = 0;
        std::vector<uint8_t> cpuMap(static_cast<size_t>(W) * H, 0);
        // CPU 端用项目内核同款判定（这里用简化复制：调用引擎静态函数不方便，直接复刻公式）
        for (int z = 0; z < H; ++z) {
            for (int x = 0; x < W; ++x) {
                // 与内核同构的 64 位公式
                const uint32_t czu = static_cast<uint32_t>(z);
                const uint32_t t3u = czu * czu;
                const int32_t t3s = static_cast<int32_t>(t3u);
                const int32_t t4s = static_cast<int32_t>(czu * 389711u);
                uint64_t b = static_cast<uint64_t>(seed);
                b += static_cast<uint64_t>(static_cast<int64_t>(t3s) * 4392871LL);
                b += static_cast<uint64_t>(static_cast<int64_t>(t4s));
                const uint32_t xu = static_cast<uint32_t>(x);
                const uint32_t t1u = (xu * xu) * 4987142u;
                const uint32_t t2u = xu * 5947611u;
                uint64_t s = b + static_cast<uint64_t>(static_cast<int64_t>(static_cast<int32_t>(t1u))) +
                             static_cast<uint64_t>(static_cast<int64_t>(static_cast<int32_t>(t2u)));
                s ^= 987234911ULL;
                uint64_t lcg = (s ^ 0x5DEECE66DULL) & 0xFFFFFFFFFFFFULL;
                lcg = (lcg * 0x5DEECE66DULL + 0xBULL) & 0xFFFFFFFFFFFFULL;
                const uint32_t qv = static_cast<uint32_t>(lcg >> 17u);
                cpuMap[static_cast<size_t>(z) * W + x] = (qv % 10u == 0u) ? 1 : 0;
            }
        }
        std::vector<IPair> cpuHits;
        for (int z = 0; z + g <= H; ++z)
            for (int x = 0; x + g <= W; ++x)
                if (cpuCount(cpuMap, W, H, x, z, g) >= thr) cpuHits.push_back(IPair{ x, z });

        const uint32_t maxHits = 1u << 20;
        cl_mem dHits = api.pfnCreateBuffer(ctx, CL_MEM_WRITE_ONLY, maxHits * sizeof(IPair), nullptr, &rc);
        cl_mem dCnt = api.pfnCreateBuffer(ctx, CL_MEM_READ_WRITE, sizeof(cl_uint), nullptr, &rc);
        uint32_t zero = 0;
        api.pfnEnqueueWriteBuffer(q, dCnt, CL_TRUE, 0, sizeof(zero), &zero, 0, nullptr, nullptr);
        api.pfnSetKernelArg(k, 0, sizeof(cl_long), &seed);
        api.pfnSetKernelArg(k, 1, sizeof(int), &minX);
        api.pfnSetKernelArg(k, 2, sizeof(int), &minZ);
        api.pfnSetKernelArg(k, 3, sizeof(int), &W);
        api.pfnSetKernelArg(k, 4, sizeof(int), &H);
        api.pfnSetKernelArg(k, 5, sizeof(int), &g);
        api.pfnSetKernelArg(k, 6, sizeof(int), &thr);
        api.pfnSetKernelArg(k, 7, sizeof(cl_uint), &maxHits);
        api.pfnSetKernelArg(k, 8, sizeof(cl_mem), &dHits);
        api.pfnSetKernelArg(k, 9, sizeof(cl_mem), &dCnt);
        const int strideX = 256 - g + 1, strideZ = 32 - g + 1;
        const int tilesX = (W - g + strideX) / strideX, tilesZ = (H - g + strideZ) / strideZ;
        const size_t global = static_cast<size_t>(tilesX) * tilesZ * 256;
        const size_t local = 256;
        api.pfnEnqueueNDRangeKernel(q, k, 1, nullptr, &global, &local, 0, nullptr, nullptr);
        uint32_t gpuCnt = 0;
        api.pfnEnqueueReadBuffer(q, dCnt, CL_TRUE, 0, sizeof(gpuCnt), &gpuCnt, 0, nullptr, nullptr);
        std::vector<IPair> gpuHits(gpuCnt);
        if (gpuCnt) {
            api.pfnEnqueueReadBuffer(q, dHits, CL_TRUE, 0, gpuCnt * sizeof(IPair), gpuHits.data(), 0, nullptr,
                                     nullptr);
        }
        // 比较（顺序可能不同，按集合比）
        std::vector<std::pair<int, int>> a, b;
        for (const auto& h2 : cpuHits) a.emplace_back(h2.x, h2.y);
        for (const auto& h2 : gpuHits) b.emplace_back(h2.x, h2.y);
        std::sort(a.begin(), a.end());
        std::sort(b.begin(), b.end());
        const bool eq = (a == b);
        std::printf("正确性：CPU 命中 %zu，GPU 命中 %u，完全一致=%s\n", cpuHits.size(), gpuCnt,
                    eq ? "是" : "否");
        api.pfnReleaseMemObject(dHits);
        api.pfnReleaseMemObject(dCnt);
    }

    // ---- 吞吐：8192x8192 = 6710 万区块（g/thr 由命令行给出，默认 5/8）----
    {
        int g = 5, thr = 8;
        if (argc >= 3) { g = std::atoi(argv[1]); thr = std::atoi(argv[2]); }
        const int W = 8192, H = 8192;
        const long long seed = 777;
        const int minX = -4096, minZ = -4096;
        const uint32_t maxHits = 4u << 20;
        cl_mem dHits = api.pfnCreateBuffer(ctx, CL_MEM_WRITE_ONLY, maxHits * sizeof(IPair), nullptr, &rc);
        cl_mem dCnt = api.pfnCreateBuffer(ctx, CL_MEM_READ_WRITE, sizeof(cl_uint), nullptr, &rc);
        uint32_t zero = 0;
        api.pfnEnqueueWriteBuffer(q, dCnt, CL_TRUE, 0, sizeof(zero), &zero, 0, nullptr, nullptr);
        api.pfnSetKernelArg(k, 0, sizeof(cl_long), &seed);
        api.pfnSetKernelArg(k, 1, sizeof(int), &minX);
        api.pfnSetKernelArg(k, 2, sizeof(int), &minZ);
        api.pfnSetKernelArg(k, 3, sizeof(int), &W);
        api.pfnSetKernelArg(k, 4, sizeof(int), &H);
        api.pfnSetKernelArg(k, 5, sizeof(int), &g);
        api.pfnSetKernelArg(k, 6, sizeof(int), &thr);
        api.pfnSetKernelArg(k, 7, sizeof(cl_uint), &maxHits);
        api.pfnSetKernelArg(k, 8, sizeof(cl_mem), &dHits);
        api.pfnSetKernelArg(k, 9, sizeof(cl_mem), &dCnt);
        const int strideX = 256 - g + 1, strideZ = 32 - g + 1;
        const int tilesX = (W - g + strideX) / strideX, tilesZ = (H - g + strideZ) / strideZ;
        const size_t global = static_cast<size_t>(tilesX) * tilesZ * 256;
        const size_t local = 256;
        double best = 1e9;
        for (int rep = 0; rep < 5; ++rep) {
            api.pfnEnqueueWriteBuffer(q, dCnt, CL_TRUE, 0, sizeof(zero), &zero, 0, nullptr, nullptr);
            const auto t0 = Clock::now();
            api.pfnEnqueueNDRangeKernel(q, k, 1, nullptr, &global, &local, 0, nullptr, nullptr);
            api.pfnFinish(q);
            const double ms = msSince(t0);
            if (ms < best) best = ms;
        }
        const double chunks = static_cast<double>(W) * H;
        std::printf("流式匹配吞吐：8192² = %.1f 万区块，%.2f ms → %.1f 亿区块/秒（含判定+窗口统计+命中回写）\n",
                    chunks / 1e4, best, chunks / (best / 1000.0) / 1e8);
        api.pfnReleaseMemObject(dHits);
        api.pfnReleaseMemObject(dCnt);
    }
    return 0;
}
