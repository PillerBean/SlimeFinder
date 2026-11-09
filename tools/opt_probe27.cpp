// opt_probe27.cpp - 「严格图案流式化」可行性（不动产品代码）
//   在共享位图上按图案行掩码做位并行匹配，只回传命中，不回传位图。
//   本探针用「5×5 全绿」这个最典型的图案做验证与测速。
#include "gpu/ClApi.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace {

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
static inline int java_to_bits(int x)
{
    return (java_x_tail(x) != 0xFFFFFFFFFFFFFFFFUL) ? 0 : 0; // 占位（未用）
}
static inline int java_hit(ulong rowBase, int x)
{
    ulong s = rowBase + java_x_tail(x);
    s ^= 987234911UL;
    ulong lcg = (s ^ 0x5DEECE66DUL) & 0xFFFFFFFFFFFFUL;
    lcg = (lcg * 0x5DEECE66DUL + 0xBUL) & 0xFFFFFFFFFFFFUL;
    uint bits = (uint)(lcg >> 17u);
    return (bits % 10u) == 0u ? 1 : 0;
}

#define PTW 256
#define PTH 32
#define PWORDS (PTW / 32)
#define PSHARED (PWORDS * PTH)

// 图案：ROWS x COLS 全绿（enable = green = (1<<COLS)-1）
__kernel void pattern_stream(long seed, int minX, int minZ, int w, int h,
                             int patRows, int patCols, uint patMask,
                             uint maxHits, __global int2* hits, __global uint* counter)
{
    __local uint bits[PSHARED];
    const int lid = (int)get_local_id(0);
    const int strideX = 256 - patCols + 1;
    const int strideZ = 32 - patRows + 1;
    const int tilesX = (w - patCols + strideX) / strideX;
    const int tileIdx = (int)get_group_id(0);
    const int tx = tileIdx % tilesX;
    const int tz = tileIdx / tilesX;
    const int ox = tx * strideX;
    const int oz = tz * strideZ;

    // ① 判定并写共享位图（每线程一个字 = 32 个连续区块）
    {
        const int wordIdx = lid;
        const int lz = wordIdx / PWORDS;
        const int wx = (wordIdx % PWORDS) * 32;
        const int cz = oz + lz;
        const int cx0 = minX + ox + wx;
        uint v = 0;
        if (cz < h && ox + wx < w) {
            ulong rb = java_row_base(seed, minZ + cz);
            for (int b = 0; b < 32; ++b) {
                const int cx = ox + wx + b;
                if (cx >= w) break;
                v |= (uint)java_hit(rb, cx0 + b) << b;
            }
        }
        bits[wordIdx] = v;
    }
    barrier(CLK_LOCAL_MEM_FENCE);

    // ② 锚点：逐行按图案掩码做位并行 AND
    const int axMax = strideX - 1;
    const int azMax = strideZ - 1;
    for (int az = 0; az <= azMax; ++az) {
        for (int ax = lid; ax <= axMax; ax += 256) {
            const int cx = minX + ox + ax;
            const int cz = minZ + oz + az;
            if (cx + patCols > minX + w || cz + patRows > minZ + h) continue;
            int ok = 1;
            for (int dz = 0; dz < patRows; ++dz) {
                const int rowBase = (az + dz) * PWORDS;
                const int word = ax >> 5;
                const int shift = ax & 31;
                uint v;
                if (shift + patCols <= 32) {
                    v = (bits[rowBase + word] >> shift) & patMask;
                } else {
                    v = ((bits[rowBase + word] >> shift) | (bits[rowBase + word + 1] << (32 - shift))) & patMask;
                }
                if (v != patMask) {
                    ok = 0;
                    break;
                }
            }
            if (ok) {
                const uint slot = atomic_inc(counter);
                if (slot < maxHits) hits[slot] = (int2)(cx, cz);
            }
        }
    }
}
)CL";

struct IPair {
    int x = 0, y = 0;
    bool operator==(const IPair& o) const { return x == o.x && y == o.y; }
};

} // namespace

int main()
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
        std::printf("编译失败:\n%s\n", log.c_str());
        return 0;
    }
    cl_kernel k = api.pfnCreateKernel(prog, "pattern_stream", &rc);

    const int patRows = 5;
    const int patCols = 5;
    const unsigned int patMask = (1u << patCols) - 1u;
    const long long seed = 424242;

    // ---- 正确性：1024x512，5x5 全绿 ----
    {
        const int W = 1024, H = 512;
        // CPU 参照
        std::vector<uint8_t> map(static_cast<size_t>(W) * H, 0);
        for (int z = 0; z < H; ++z) {
            const uint32_t czu = static_cast<uint32_t>(z);
            const uint32_t t3u = czu * czu;
            const int32_t t3s = static_cast<int32_t>(t3u);
            const int32_t t4s = static_cast<int32_t>(czu * 389711u);
            uint64_t b = static_cast<uint64_t>(seed);
            b += static_cast<uint64_t>(static_cast<int64_t>(t3s) * 4392871LL);
            b += static_cast<uint64_t>(static_cast<int64_t>(t4s));
            for (int x = 0; x < W; ++x) {
                const uint32_t xu = static_cast<uint32_t>(x);
                const uint32_t t1u = (xu * xu) * 4987142u;
                const uint32_t t2u = xu * 5947611u;
                uint64_t s = b + static_cast<uint64_t>(static_cast<int64_t>(static_cast<int32_t>(t1u))) +
                             static_cast<uint64_t>(static_cast<int64_t>(static_cast<int32_t>(t2u)));
                s ^= 987234911ULL;
                uint64_t lcg = (s ^ 0x5DEECE66DULL) & 0xFFFFFFFFFFFFULL;
                lcg = (lcg * 0x5DEECE66DULL + 0xBULL) & 0xFFFFFFFFFFFFULL;
                map[static_cast<size_t>(z) * W + x] = ((lcg >> 17u) % 10u == 0u) ? 1 : 0;
            }
        }
        std::vector<IPair> cpuHits;
        for (int z = 0; z + patRows <= H; ++z) {
            for (int x = 0; x + patCols <= W; ++x) {
                bool ok = true;
                for (int dz = 0; dz < patRows && ok; ++dz)
                    for (int dx = 0; dx < patCols; ++dx)
                        if (!map[static_cast<size_t>(z + dz) * W + (x + dx)]) {
                            ok = false;
                            break;
                        }
                if (ok) cpuHits.push_back(IPair{ x, z });
            }
        }
        const unsigned int maxHits = 1u << 20u;
        cl_mem dHits = api.pfnCreateBuffer(ctx, CL_MEM_WRITE_ONLY, maxHits * sizeof(IPair), nullptr, &rc);
        cl_mem dCnt = api.pfnCreateBuffer(ctx, CL_MEM_READ_WRITE, sizeof(cl_uint), nullptr, &rc);
        uint32_t zero = 0;
        api.pfnEnqueueWriteBuffer(q, dCnt, CL_TRUE, 0, sizeof(zero), &zero, 0, nullptr, nullptr);
        api.pfnSetKernelArg(k, 0, sizeof(cl_long), &seed);
        int minX = 0, minZ = 0;
        api.pfnSetKernelArg(k, 1, sizeof(int), &minX);
        api.pfnSetKernelArg(k, 2, sizeof(int), &minZ);
        api.pfnSetKernelArg(k, 3, sizeof(int), &W);
        api.pfnSetKernelArg(k, 4, sizeof(int), &H);
        int pr = patRows;
        int pc = patCols;
        unsigned int pm = patMask;
        api.pfnSetKernelArg(k, 5, sizeof(int), &pr);
        api.pfnSetKernelArg(k, 6, sizeof(int), &pc);
        api.pfnSetKernelArg(k, 7, sizeof(unsigned int), &pm);
        api.pfnSetKernelArg(k, 8, sizeof(unsigned int), &maxHits);
        api.pfnSetKernelArg(k, 9, sizeof(cl_mem), &dHits);
        api.pfnSetKernelArg(k, 10, sizeof(cl_mem), &dCnt);
        const int strideX = 256 - patCols + 1, strideZ = 32 - patRows + 1;
        const int tilesX = (W - patCols + strideX) / strideX, tilesZ = (H - patRows + strideZ) / strideZ;
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
        std::vector<IPair> a = cpuHits, b = gpuHits;
        auto cmp = [](const IPair& p, const IPair& q2) { return p.x != q2.x ? p.x < q2.x : p.y < q2.y; };
        std::sort(a.begin(), a.end(), cmp);
        std::sort(b.begin(), b.end(), cmp);
        std::printf("正确性（5x5 全绿，1024x512）：CPU %zu，GPU %u，完全一致=%s\n", cpuHits.size(), gpuCnt,
                    (a == b) ? "是" : "否");
        api.pfnReleaseMemObject(dHits);
        api.pfnReleaseMemObject(dCnt);
    }

    // ---- 吞吐：8192x8192 ----
    {
        const int W = 8192, H = 8192;
        const unsigned int maxHits = 1u << 20u;
        cl_mem dHits = api.pfnCreateBuffer(ctx, CL_MEM_WRITE_ONLY, maxHits * sizeof(IPair), nullptr, &rc);
        cl_mem dCnt = api.pfnCreateBuffer(ctx, CL_MEM_READ_WRITE, sizeof(cl_uint), nullptr, &rc);
        uint32_t zero = 0;
        api.pfnSetKernelArg(k, 0, sizeof(cl_long), &seed);
        int minX = -4096, minZ = -4096;
        api.pfnSetKernelArg(k, 1, sizeof(int), &minX);
        api.pfnSetKernelArg(k, 2, sizeof(int), &minZ);
        api.pfnSetKernelArg(k, 3, sizeof(int), &W);
        api.pfnSetKernelArg(k, 4, sizeof(int), &H);
        int pr = patRows;
        int pc = patCols;
        unsigned int pm = patMask;
        api.pfnSetKernelArg(k, 5, sizeof(int), &pr);
        api.pfnSetKernelArg(k, 6, sizeof(int), &pc);
        api.pfnSetKernelArg(k, 7, sizeof(unsigned int), &pm);
        api.pfnSetKernelArg(k, 8, sizeof(unsigned int), &maxHits);
        api.pfnSetKernelArg(k, 9, sizeof(cl_mem), &dHits);
        api.pfnSetKernelArg(k, 10, sizeof(cl_mem), &dCnt);
        const int strideX = 256 - patCols + 1, strideZ = 32 - patRows + 1;
        const int tilesX = (W - patCols + strideX) / strideX, tilesZ = (H - patRows + strideZ) / strideZ;
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
        std::printf("图案流式吞吐：8192² %.2f ms → %.1f 亿区块/秒（含判定+匹配+命中回写）\n", best,
                    chunks / (best / 1000.0) / 1e8);
        api.pfnReleaseMemObject(dHits);
        api.pfnReleaseMemObject(dCnt);
    }
    return 0;
}
