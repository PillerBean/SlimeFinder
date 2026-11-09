// opt_probe21.cpp - 环形覆盖 GPU 加速的进一步验证（不改产品代码）
//   A) 与「产品现有 CPU 实现」对比：用预计算的磁盘行轮廓（DiskRows），而不是每中心重算 sqrt
//   B) 多组参数扫描（区域大小 / step / 半径 / 无内环），逐中心校验 GPU 与 CPU 全等
//   C) 把前缀和表的构建时间也算进去，给出「整条链路」的可比数字
#include "gpu/ClApi.h"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;
double msSince(Clock::time_point t0)
{
    return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

const char* kKernel = R"CL(
__kernel void annulus_cov(__global const int* rowPref, int pw, int ph,
                          int cx0, int cz0, int step, int nx, int nz,
                          int rIn, int rOut, __global int* out)
{
    int idx = (int)get_global_id(0);
    if (idx >= nx * nz) return;
    const int ix = idx % nx;
    const int iz = idx / nx;
    const int cx = cx0 + ix * step;
    const int cz = cz0 + iz * step;
    int total = 0;
    const long r2o = (long)rOut * rOut;
    for (int dz = -rOut; dz <= rOut; ++dz) {
        long rem = r2o - (long)dz * dz;
        if (rem < 0) continue;
        int w = (int)sqrt((float)rem);
        while ((long)(w + 1) * (w + 1) + (long)dz * dz <= r2o) ++w;
        while (w > 0 && (long)w * w + (long)dz * dz > r2o) --w;
        const int z = cz + dz;
        if (z < 0 || z >= ph) continue;
        int a = cx - w, b = cx + w;
        if (a < 0) a = 0;
        if (b >= pw - 1) b = pw - 2;
        total += rowPref[z * pw + b + 1] - rowPref[z * pw + a];
    }
    if (rIn > 0) {
        const long r2i = (long)rIn * rIn - 1;
        for (int dz = -rIn; dz <= rIn; ++dz) {
            long rem = r2i - (long)dz * dz;
            if (rem < 0) continue;
            int w = (int)sqrt((float)rem);
            while ((long)(w + 1) * (w + 1) + (long)dz * dz <= r2i) ++w;
            while (w > 0 && (long)w * w + (long)dz * dz > r2i) --w;
            const int z = cz + dz;
            if (z < 0 || z >= ph) continue;
            int a = cx - w, b = cx + w;
            if (a < 0) a = 0;
            if (b >= pw - 1) b = pw - 2;
            total -= rowPref[z * pw + b + 1] - rowPref[z * pw + a];
        }
    }
    out[idx] = total;
}
)CL";

struct DiskRows {
    int r = 0;
    std::vector<int> w;   // w[dz + r]
};

DiskRows makeDiskRowsR2(long r2)
{
    DiskRows d;
    if (r2 < 0) return d;
    d.r = static_cast<int>(std::sqrt(static_cast<double>(r2)));
    d.w.assign(static_cast<size_t>(2 * d.r + 1), 0);
    for (int dz = -d.r; dz <= d.r; ++dz) {
        long rem = r2 - static_cast<long>(dz) * dz;
        if (rem < 0) continue;
        int w = static_cast<int>(std::sqrt(static_cast<double>(rem)));
        while (static_cast<long>(w + 1) * (w + 1) + static_cast<long>(dz) * dz <= r2) ++w;
        while (w > 0 && static_cast<long>(w) * w + static_cast<long>(dz) * dz > r2) --w;
        d.w[static_cast<size_t>(dz + d.r)] = w;
    }
    return d;
}

// 产品现有实现的等价物：预算磁盘行轮廓 + 每中心只做行前缀差
int cpuWithDiskRows(const std::vector<int>& pref, int pw, int ph, const DiskRows& out, const DiskRows* in,
                    int cx, int cz)
{
    int total = 0;
    const auto pass = [&](const DiskRows& d, int sign) {
        for (int dz = -d.r; dz <= d.r; ++dz) {
            const int w = d.w[static_cast<size_t>(dz + d.r)];
            const int z = cz + dz;
            if (z < 0 || z >= ph) continue;
            int a = cx - w, b = cx + w;
            if (a < 0) a = 0;
            if (b >= pw - 1) b = pw - 2;
            total += sign * (pref[static_cast<size_t>(z) * pw + b + 1] - pref[static_cast<size_t>(z) * pw + a]);
        }
    };
    pass(out, +1);
    if (in) pass(*in, -1);
    return total;
}

struct Cfg {
    const char* name;
    int W, H, rOut, rIn, step;
};

} // namespace

int main()
{
    // OpenCL 初始化（与产品一致的动态加载路径）
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
    cl_program prog = api.pfnCreateProgramWithSource(ctx, 1, &kKernel, nullptr, &rc);
    if (!prog || api.pfnBuildProgram(prog, 1, &dev, nullptr, nullptr, nullptr) != CL_SUCCESS) {
        std::printf("内核编译失败\n");
        return 0;
    }
    cl_kernel k = api.pfnCreateKernel(prog, "annulus_cov", &rc);

    const Cfg cfgs[] = {
        { "2000² step4 R128/r24", 2000, 2000, 128, 24, 4 },
        { "2000² step8 R128/r24", 2000, 2000, 128, 24, 8 },
        { "2000² step4 R128/无内环", 2000, 2000, 128, 0, 4 },
        { "1000² step4 R128/r24", 1000, 1000, 128, 24, 4 },
        { "3000² step4 R128/r24", 3000, 3000, 128, 24, 4 },
        { "2000² step2 R64/r16", 2000, 2000, 64, 16, 2 },
    };

    std::printf("%-26s %8s %8s %9s %9s %9s %8s\n", "配置", "中心数", "前缀和", "CPU(旧)", "CPU(现)", "GPU", "加速");
    for (const Cfg& c : cfgs) {
        const int W = c.W, H = c.H;
        const int PW = W + 1;
        std::vector<int> pref(static_cast<size_t>(PW) * H, 0);
        // 前缀和构建（单线程，产品当前也是逐行累加）
        const auto t0 = Clock::now();
        for (int z = 0; z < H; ++z) {
            int acc = 0;
            for (int x = 0; x < W; ++x) {
                const uint64_t h = static_cast<uint64_t>(x) * 0x9E3779B97F4A7C15ULL ^
                                   static_cast<uint64_t>(z) * 0xC2B2AE3D27D4EB4FULL;
                const uint64_t v = h * 6364136223846793005ULL + 1442695040888963407ULL;
                const int s = (((v >> 33) % 10u) == 0u) ? 1 : 0;
                acc += s;
                pref[static_cast<size_t>(z) * PW + x + 1] = acc;
            }
        }
        const double prefMs = msSince(t0);

        const int nx = (W - c.rOut * 2) / c.step + 1;
        const int nz = (H - c.rOut * 2) / c.step + 1;
        const int centers = nx * nz;
        if (centers <= 0) {
            std::printf("%-26s 跳过（区域太小）\n", c.name);
            continue;
        }
        const DiskRows dOut = makeDiskRowsR2(static_cast<long>(c.rOut) * c.rOut);
        const DiskRows dIn = (c.rIn > 0) ? makeDiskRowsR2(static_cast<long>(c.rIn) * c.rIn - 1) : DiskRows{};
        const DiskRows* pIn = (c.rIn > 0 && !dIn.w.empty()) ? &dIn : nullptr;

        std::vector<int> cpuCur(static_cast<size_t>(centers));
        const auto t1 = Clock::now();
        for (int iz = 0; iz < nz; ++iz)
            for (int ix = 0; ix < nx; ++ix)
                cpuCur[static_cast<size_t>(iz * nx + ix)] = cpuWithDiskRows(
                    pref, PW, H, dOut, pIn, c.rOut + ix * c.step, c.rOut + iz * c.step);
        const double cpuCurMs = msSince(t1);

        std::vector<int> cpuOld(static_cast<size_t>(centers));
        const auto t2 = Clock::now();
        if (centers <= 250000) {   // 旧实现太慢，大配置跳过
            for (int iz = 0; iz < nz; ++iz)
                for (int ix = 0; ix < nx; ++ix) {
                    const int cx = c.rOut + ix * c.step, cz = c.rOut + iz * c.step;
                    const DiskRows& rowsRef = dOut;
                    int tot = 0;
                    for (int dz = -rowsRef.r; dz <= rowsRef.r; ++dz) {
                        const int w = rowsRef.w[static_cast<size_t>(dz + rowsRef.r)];
                        const int z = cz + dz;
                        if (z < 0 || z >= H) continue;
                        int a = cx - w, b = cx + w;
                        if (a < 0) a = 0;
                        if (b >= PW - 1) b = PW - 2;
                        tot += pref[static_cast<size_t>(z) * PW + b + 1] - pref[static_cast<size_t>(z) * PW + a];
                    }
                    cpuOld[static_cast<size_t>(iz * nx + ix)] = tot;
                }
        }
        const double cpuOldMs = (centers <= 250000) ? msSince(t2) : 0.0;

        // GPU
        std::vector<int> gpu(static_cast<size_t>(centers), -1);
        cl_mem dPref = api.pfnCreateBuffer(ctx, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
                                           pref.size() * sizeof(int), pref.data(), &rc);
        cl_mem dOutB = api.pfnCreateBuffer(ctx, CL_MEM_WRITE_ONLY, gpu.size() * sizeof(int), nullptr, &rc);
        const auto t3 = Clock::now();
        int cx0 = c.rOut, cz0 = c.rOut;
        api.pfnSetKernelArg(k, 0, sizeof(cl_mem), &dPref);
        api.pfnSetKernelArg(k, 1, sizeof(int), &PW);
        api.pfnSetKernelArg(k, 2, sizeof(int), &H);
        api.pfnSetKernelArg(k, 3, sizeof(int), &cx0);
        api.pfnSetKernelArg(k, 4, sizeof(int), &cz0);
        api.pfnSetKernelArg(k, 5, sizeof(int), &c.step);
        api.pfnSetKernelArg(k, 6, sizeof(int), &nx);
        api.pfnSetKernelArg(k, 7, sizeof(int), &nz);
        api.pfnSetKernelArg(k, 8, sizeof(int), &c.rIn);
        api.pfnSetKernelArg(k, 9, sizeof(int), &c.rOut);
        api.pfnSetKernelArg(k, 10, sizeof(cl_mem), &dOutB);
        size_t global = static_cast<size_t>(centers);
        size_t local = 128;
        api.pfnEnqueueNDRangeKernel(q, k, 1, nullptr, &global, (global % local == 0) ? &local : nullptr, 0,
                                    nullptr, nullptr);
        api.pfnEnqueueReadBuffer(q, dOutB, CL_TRUE, 0, gpu.size() * sizeof(int), gpu.data(), 0, nullptr,
                                 nullptr);
        const double gpuMs = msSince(t3);

        int mismatch = 0;
        for (int i = 0; i < centers; ++i)
            if (gpu[static_cast<size_t>(i)] != cpuCur[static_cast<size_t>(i)]) ++mismatch;
        api.pfnReleaseMemObject(dPref);
        api.pfnReleaseMemObject(dOutB);

        std::printf("%-26s %8d %7.2fms %8.2fms %8.2fms %8.2fms %7.1fx  不一致=%d\n", c.name, centers, prefMs,
                    cpuOldMs, cpuCurMs, gpuMs, cpuCurMs / gpuMs, mismatch);
    }
    std::printf("\n注：CPU(旧)=每中心重算 sqrt；CPU(现)=产品在用的预算磁盘行轮廓；GPU 含上传前缀表与回读。\n");
    return 0;
}
