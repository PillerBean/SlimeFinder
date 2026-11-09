// opt_probe24.cpp - GPU 内核形态对比（不动产品代码）
//   目的：找出 Java 判定内核在 RX 6750 GRE 上的真实上限，以及「每 work-item 处理多少区块」
//   对吞吐的影响。用于判断「社区工具等效 870 亿/秒」是否可达。
//
//   变体：
//     V1 每 item 1 区块 → 输出 uchar            （最大并行度，最差写放大）
//     V2 每 item 8 区块 → 输出 uchar（8 位打包）  （写放大 1/8）
//     V3 每 item 64 区块 → 输出 ulong（= 产品版本）
//     V4 每 item 64 区块，但用纯 32 位状态表示（不出现 ulong 乘法/掩码）
#include "gpu/ClApi.h"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;
double msSince(Clock::time_point t0)
{
    return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

// 公共：行基座（与产品实现逐位一致）
const char* kCommon = R"CL(
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

// 产品同款：48 位 LCG 单步 + %10
static inline int java_hit(ulong rowBase, int x)
{
    ulong s = rowBase + java_x_tail(x);
    s ^= 987234911UL;
    ulong lcg = (s ^ 0x5DEECE66DUL) & 0xFFFFFFFFFFFFUL;
    lcg = (lcg * 0x5DEECE66DUL + 0xBUL) & 0xFFFFFFFFFFFFUL;
    uint bits = (uint)(lcg >> 17u);
    return (bits % 10u) == 0u ? 1 : 0;
}

// 纯 32 位版本：状态拆成 lo(32位) + hi(低16位)，乘法全部走 32×32→64 的 mad
static inline int java_hit32(ulong rowBase, ulong tail, int x)
{
    // s0 = (rowBase + tail)^987234911，取低 48 位后 ^MULT
    ulong s = rowBase + tail;
    s ^= 987234911UL;
    uint lo = (uint)((s ^ 0x5DEECE66DUL) & 0xFFFFFFFFUL);
    uint hi = (uint)(((s ^ 0x5DEECE66DUL) >> 32) & 0xFFFFUL);
    const uint kMlo = 0xDEECE66Du, kMhi = 5u, kMlo16 = 0xE66Du;
    ulong p1 = (ulong)lo * (ulong)kMlo;                       // 32×32→64
    uint cross = ((uint)(lo * kMhi) + (uint)(hi * kMlo16)) & 0xFFFFu;
    ulong r = (p1 + ((ulong)cross << 32) + 0xBUL) & 0xFFFFFFFFFFFFUL;
    uint bits = (uint)(r >> 17u);
    uint q = (uint)(((ulong)bits * 0xCCCCCCCDu) >> 35);
    return (bits - q * 10u) == 0u ? 1 : 0;
}
)CL";

const char* kKernels = R"CL(
// V1：每 item 1 区块
__kernel void v1(long seed, int minX, int minZ, int w, int h, __global uchar* out)
{
    size_t idx = get_global_id(0);
    if (idx >= (size_t)w * h) return;
    int row = (int)(idx / (size_t)w);
    int x = minX + (int)(idx % (size_t)w);
    out[idx] = (uchar)java_hit(java_row_base(seed, minZ + row), x);
}

// V2：每 item 8 区块打包成 1 字节
__kernel void v2(long seed, int minX, int minZ, int w, int h, __global uchar* out)
{
    size_t wpr = (size_t)w / 8;
    size_t idx = get_global_id(0);
    if (idx >= wpr * (size_t)h) return;
    int row = (int)(idx / wpr);
    int x0 = minX + (int)(idx % wpr) * 8;
    ulong rb = java_row_base(seed, minZ + row);
    uint bits = 0;
    for (int b = 0; b < 8; ++b) bits |= (uint)java_hit(rb, x0 + b) << b;
    out[idx] = (uchar)bits;
}

// V3：每 item 64 区块打包成 ulong（= 产品实现）
__kernel void v3(long seed, int minX, int minZ, int w, int h, __global ulong* out)
{
    size_t wpr = (size_t)w / 64;
    size_t idx = get_global_id(0);
    if (idx >= wpr * (size_t)h) return;
    int row = (int)(idx / wpr);
    int x0 = minX + (int)(idx % wpr) * 64;
    ulong rb = java_row_base(seed, minZ + row);
    ulong bits = 0;
    for (int b = 0; b < 64; ++b) bits |= (ulong)java_hit(rb, x0 + b) << b;
    out[idx] = bits;
}

// V5：每 item 64 区块，尾项用差分递推（3 次加法替代 3 次乘法）；状态拆成两个 uint32
static inline int java_hit_incr(uint baseLo, uint baseHi, uint t1, uint d, uint t2)
{
    // tail = sext32(t1) + sext32(t2)（mod 2^64）
    const uint lo = t1 + t2;
    const uint carry = (lo < t1) ? 1u : 0u;
    const int hiTail = (int)carry - (int)(t1 >> 31) - (int)(t2 >> 31);
    const uint sLo = baseLo + lo;
    const uint carry2 = (sLo < baseLo) ? 1u : 0u;
    uint lcgLo = sLo ^ 987234911u;
    uint lcgHi = baseHi + (uint)hiTail + carry2;
    lcgLo ^= 0xDEECE66Du;
    lcgHi = (lcgHi ^ 5u) & 0xFFFFu;
    // (lcgLo + lcgHi·2^32) × MULT + 11 mod 2^48，只取第 17..47 位
    const uint kMhi = 5u, kMlo16 = 0xE66Du;
    const ulong p = (ulong)lcgLo * (ulong)0xDEECE66Du;
    const uint cross = ((uint)(lcgLo * kMhi) + (uint)(lcgHi * kMlo16)) & 0xFFFFu;
    const ulong r = (p + ((ulong)cross << 32) + 0xBUL) & 0xFFFFFFFFFFFFUL;
    const uint bits = (uint)(r >> 17u);
    return (bits % 10u) == 0u ? 1 : 0;
}

__kernel void v5(long seed, int minX, int minZ, int w, int h, __global ulong* out)
{
    size_t wpr = (size_t)w / 64;
    size_t idx = get_global_id(0);
    if (idx >= wpr * (size_t)h) return;
    int row = (int)(idx / wpr);
    int x0 = minX + (int)(idx % wpr) * 64;
    ulong rb = java_row_base(seed, minZ + row);
    uint baseLo = (uint)rb, baseHi = (uint)(rb >> 32);
    // 初值 + 差分
    uint xu = (uint)x0;
    uint t1 = (xu * xu) * 4987142u;
    uint t2 = xu * 5947611u;
    uint d = (2u * xu + 1u) * 4987142u;
    const uint dStep = 2u * 4987142u;
    const uint t2Step = 5947611u;
    ulong bits = 0;
    for (int b = 0; b < 64; ++b) {
        bits |= (ulong)java_hit_incr(baseLo, baseHi, t1, d, t2) << b;
        t1 += d;
        d += dStep;
        t2 += t2Step;
    }
    out[idx] = bits;
}

// V6：在 V5（差分递推）基础上再去掉 48 位掩码与 64 位合成：
//   直接按 32 位高低字算出 r，再从 rLo/rHi 取第 17..47 位
static inline int java_hit_incr2(uint baseLo, uint baseHi, uint t1, uint t2)
{
    const uint lo = t1 + t2;
    const uint carry = (lo < t1) ? 1u : 0u;
    const int hiTail = (int)carry - (int)(t1 >> 31) - (int)(t2 >> 31);
    const uint sLo = baseLo + lo;
    const uint carry2 = (sLo < baseLo) ? 1u : 0u;
    const uint lcgLo = (sLo ^ 987234911u) ^ 0xDEECE66Du;
    const uint lcgHi = ((baseHi + (uint)hiTail + carry2) ^ 5u) & 0xFFFFu;
    const ulong prod = (ulong)lcgLo * (ulong)0xDEECE66Du;
    const uint cross = ((uint)(lcgLo * 5u) + (uint)(lcgHi * 0xE66Du)) & 0xFFFFu;
    const uint pLo = (uint)prod;
    const uint pHi = (uint)(prod >> 32);
    const uint rLo = pLo + 0xBu;
    const uint c = (rLo < pLo) ? 1u : 0u;
    const uint rHi = pHi + cross + c;
    const uint bits = (rLo >> 17u) | ((rHi & 0xFFFFu) << 15u);
    const uint q = (uint)(((ulong)bits * 0xCCCCCCCDu) >> 35);
    return (bits - q * 10u) == 0u ? 1 : 0;
}

__kernel void v6(long seed, int minX, int minZ, int w, int h, __global ulong* out)
{
    size_t wpr = (size_t)w / 64;
    size_t idx = get_global_id(0);
    if (idx >= wpr * (size_t)h) return;
    int row = (int)(idx / wpr);
    int x0 = minX + (int)(idx % wpr) * 64;
    ulong rb = java_row_base(seed, minZ + row);
    uint baseLo = (uint)rb, baseHi = (uint)(rb >> 32);
    uint xu = (uint)x0;
    uint t1 = (xu * xu) * 4987142u;
    uint t2 = xu * 5947611u;
    uint d = (2u * xu + 1u) * 4987142u;
    const uint dStep = 2u * 4987142u;
    const uint t2Step = 5947611u;
    ulong bits = 0;
    for (int b = 0; b < 64; ++b) {
        bits |= (ulong)java_hit_incr2(baseLo, baseHi, t1, t2) << b;
        t1 += d;
        d += dStep;
        t2 += t2Step;
    }
    out[idx] = bits;
}

// V4：每 item 64 区块 + 纯 32 位路径（尾项只算一次增量）
__kernel void v4(long seed, int minX, int minZ, int w, int h, __global ulong* out)
{
    size_t wpr = (size_t)w / 64;
    size_t idx = get_global_id(0);
    if (idx >= wpr * (size_t)h) return;
    int row = (int)(idx / wpr);
    int x0 = minX + (int)(idx % wpr) * 64;
    ulong rb = java_row_base(seed, minZ + row);
    ulong bits = 0;
    for (int b = 0; b < 64; ++b) {
        bits |= (ulong)java_hit32(rb, java_x_tail(x0 + b), x0 + b) << b;
    }
    out[idx] = bits;
}
)CL";

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

    std::string src = std::string(kCommon) + kKernels;
    const char* srcPtr = src.c_str();
    cl_program prog = api.pfnCreateProgramWithSource(ctx, 1, &srcPtr, nullptr, &rc);
    if (!prog) {
        std::printf("program 创建失败\n");
        return 0;
    }
    const cl_int brc = api.pfnBuildProgram(prog, 1, &dev, nullptr, nullptr, nullptr);
    if (brc != CL_SUCCESS) {
        size_t n = 0;
        api.pfnGetProgramBuildInfo(prog, dev, CL_PROGRAM_BUILD_LOG, 0, nullptr, &n);
        std::string log(n, '\0');
        api.pfnGetProgramBuildInfo(prog, dev, CL_PROGRAM_BUILD_LOG, n, log.data(), &n);
        std::printf("内核编译失败:\n%s\n", log.c_str());
        return 0;
    }

    const int W = 16384, H = 8192;   // 1.34 亿区块
    const int64_t total = static_cast<int64_t>(W) * H;
    const long long seed = 12345;
    int minX = -8192, minZ = -4096;

    struct Variant {
        const char* name;
        const char* kernel;
        int chunksPerItem;
        bool out64;
    } variants[] = {
        { "V1 1区块/item (uchar)", "v1", 1, false },
        { "V2 8区块/item (uchar)", "v2", 8, false },
        { "V3 64区块/item (ulong，产品)", "v3", 64, true },
        { "V4 64区块/item (ulong，32位路径)", "v4", 64, true },
        { "V5 64区块/item (差分递推尾项)", "v5", 64, true },
        { "V6 64区块/item (差分递推+去48位掩码)", "v6", 64, true },
    };

    // ---- 正确性：小区域 V3 vs V5 逐字比对 ----
    {
        const int CW = 4096, CH = 512;
        const long long cseed = 987654321;
        int cminX = -2048, cminZ = 256;
        const size_t words = static_cast<size_t>(CW / 64) * CH;
        std::vector<unsigned long long> buf3(words), buf5(words);
        const auto runK = [&](const char* name, std::vector<unsigned long long>& dst) {
            cl_kernel kk = api.pfnCreateKernel(prog, name, &rc);
            cl_mem d = api.pfnCreateBuffer(ctx, CL_MEM_WRITE_ONLY, words * sizeof(cl_ulong), nullptr, &rc);
            api.pfnSetKernelArg(kk, 0, sizeof(cl_long), &cseed);
            api.pfnSetKernelArg(kk, 1, sizeof(int), &cminX);
            api.pfnSetKernelArg(kk, 2, sizeof(int), &cminZ);
            api.pfnSetKernelArg(kk, 3, sizeof(int), &CW);
            api.pfnSetKernelArg(kk, 4, sizeof(int), &CH);
            api.pfnSetKernelArg(kk, 5, sizeof(cl_mem), &d);
            const size_t g = words;
            api.pfnEnqueueNDRangeKernel(q, kk, 1, nullptr, &g, nullptr, 0, nullptr, nullptr);
            api.pfnEnqueueReadBuffer(q, d, CL_TRUE, 0, words * sizeof(cl_ulong), dst.data(), 0, nullptr, nullptr);
            api.pfnReleaseMemObject(d);
            api.pfnReleaseKernel(kk);
        };
        runK("v3", buf3);
        runK("v5", buf5);
        size_t diff = 0;
        for (size_t i = 0; i < words; ++i)
            if (buf3[i] != buf5[i]) ++diff;
        std::printf("正确性 V3 vs V5：%zu 个字，不一致 %zu 个 → %s\n\n", words, diff,
                    diff == 0 ? "通过" : "失败");
    }

    std::printf("区域 %dx%d = %lld 区块\n\n", W, H, static_cast<long long>(total));
    for (const Variant& v : variants) {
        cl_kernel k = api.pfnCreateKernel(prog, v.kernel, &rc);
        if (!k) {
            std::printf("%-36s 内核创建失败\n", v.name);
            continue;
        }
        const size_t outCount = v.out64 ? (static_cast<size_t>(total) / v.chunksPerItem) * sizeof(cl_ulong)
                                        : (static_cast<size_t>(total) / v.chunksPerItem) * sizeof(cl_uchar);
        cl_mem dOut = api.pfnCreateBuffer(ctx, CL_MEM_WRITE_ONLY, outCount, nullptr, &rc);
        api.pfnSetKernelArg(k, 0, sizeof(cl_long), &seed);
        api.pfnSetKernelArg(k, 1, sizeof(int), &minX);
        api.pfnSetKernelArg(k, 2, sizeof(int), &minZ);
        api.pfnSetKernelArg(k, 3, sizeof(int), &W);
        api.pfnSetKernelArg(k, 4, sizeof(int), &H);
        api.pfnSetKernelArg(k, 5, sizeof(cl_mem), &dOut);
        const size_t global = static_cast<size_t>(total / v.chunksPerItem);

        // 预热一次
        api.pfnEnqueueNDRangeKernel(q, k, 1, nullptr, &global, nullptr, 0, nullptr, nullptr);
        api.pfnFinish(q);

        double best = 1e9;
        const int reps = 7;
        for (int r = 0; r < reps; ++r) {
            const auto t0 = Clock::now();
            api.pfnEnqueueNDRangeKernel(q, k, 1, nullptr, &global, nullptr, 0, nullptr, nullptr);
            api.pfnFinish(q);
            const double ms = msSince(t0);
            if (ms < best) best = ms;
        }
        const double cps = static_cast<double>(total) / (best / 1000.0);
        std::printf("%-36s %7.2f ms → %8.1f 亿区块/秒\n", v.name, best, cps / 1e8);
        api.pfnReleaseMemObject(dOut);
        api.pfnReleaseKernel(k);
    }
    return 0;
}
