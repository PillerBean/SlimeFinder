// opt_probe20.cpp - GPU 新方向可行性：把「环形覆盖」搬到 GPU（磁盘卷积）
//   CPU 现状：250k 个候选中心 × 约 306 行查表 ≈ 33 ms（环形规划的主要开销）。
//   本探针用 OpenCL 内核做同一件事，和 CPU 版本对比正确性与耗时。
//   只做可行性测量，不接入产品代码。
#include "gpu/ClApi.h"

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

// CPU 参照：与内核完全同构的逐中心覆盖
int cpuCoverage(const std::vector<int>& pref, int pw, int ph, int cx, int cz, int rIn, int rOut)
{
    int total = 0;
    const auto pass = [&](long r2, int sign) {
        if (r2 < 0) return;
        const int r = static_cast<int>(std::sqrt(static_cast<double>(r2)));
        for (int dz = -r; dz <= r; ++dz) {
            long rem = r2 - static_cast<long>(dz) * dz;
            if (rem < 0) continue;
            int w = static_cast<int>(std::sqrt(static_cast<double>(rem)));
            while (static_cast<long>(w + 1) * (w + 1) + static_cast<long>(dz) * dz <= r2) ++w;
            while (w > 0 && static_cast<long>(w) * w + static_cast<long>(dz) * dz > r2) --w;
            const int z = cz + dz;
            if (z < 0 || z >= ph) continue;
            int a = cx - w, b = cx + w;
            if (a < 0) a = 0;
            if (b >= pw - 1) b = pw - 2;
            total += sign * (pref[static_cast<size_t>(z) * pw + b + 1] - pref[static_cast<size_t>(z) * pw + a]);
        }
    };
    pass(static_cast<long>(rOut) * rOut, +1);
    if (rIn > 0) pass(static_cast<long>(rIn) * rIn - 1, -1);
    return total;
}

} // namespace

int main()
{
    // ---- 1) 造数据：2000x2000 的「史莱姆位图」+ 每行前缀和 ----
    const int W = 2000, H = 2000;
    const int PW = W + 1;
    std::vector<int> pref(static_cast<size_t>(PW) * H, 0);
    int slime = 0;
    for (int z = 0; z < H; ++z) {
        int acc = 0;
        for (int x = 0; x < W; ++x) {
            const uint64_t h = static_cast<uint64_t>(x) * 0x9E3779B97F4A7C15ULL ^
                               static_cast<uint64_t>(z) * 0xC2B2AE3D27D4EB4FULL;
            const uint64_t v = h * 6364136223846793005ULL + 1442695040888963407ULL;
            const int s = (((v >> 33) % 10u) == 0u) ? 1 : 0;
            slime += s;
            acc += s;
            pref[static_cast<size_t>(z) * PW + x + 1] = acc;
        }
    }
    std::printf("数据：%dx%d，史莱姆 %d（%.1f%%）\n", W, H, slime, 100.0 * slime / (W * H));

    const int rOut = 128, rIn = 24, step = 4;
    const int nx = (W - rOut * 2) / step + 1;
    const int nz = (H - rOut * 2) / step + 1;
    const int centers = nx * nz;

    // ---- 2) CPU 参照耗时 ----
    std::vector<int> cpuOut(static_cast<size_t>(centers));
    const auto t0 = Clock::now();
    for (int iz = 0; iz < nz; ++iz) {
        for (int ix = 0; ix < nx; ++ix) {
            const int cx = rOut + ix * step;
            const int cz = rOut + iz * step;
            cpuOut[static_cast<size_t>(iz * nx + ix)] = cpuCoverage(pref, PW, H, cx, cz, rIn, rOut);
        }
    }
    const double cpuMs = msSince(t0);
    std::printf("CPU 覆盖 %d 中心：%.2f ms（%.1f ns/中心）\n", centers, cpuMs, cpuMs * 1e6 / centers);

    // ---- 3) GPU：找设备、建上下文 ----
    slimegpu::ClApi api;
    std::vector<std::string> mods;
    slimegpu::collectCandidateModules(mods);
    for (const std::string& m : mods) {
        if (api.openModule(m)) break;
    }
    if (!api.ok) {
        std::printf("OpenCL 不可用，跳过 GPU 部分\n");
        return 0;
    }
    cl_platform_id plat = nullptr;
    cl_device_id dev = nullptr;
    cl_uint np = 0;
    if (api.pfnGetPlatformIDs(1, &plat, &np) != CL_SUCCESS || np == 0 ||
        api.pfnGetDeviceIDs(plat, CL_DEVICE_TYPE_ALL, 1, &dev, nullptr) != CL_SUCCESS) {
        std::printf("找不到 OpenCL 设备\n");
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

    cl_mem dPref = api.pfnCreateBuffer(ctx, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
                                       pref.size() * sizeof(int), pref.data(), &rc);
    std::vector<int> gpuOut(static_cast<size_t>(centers), -1);
    cl_mem dOut = api.pfnCreateBuffer(ctx, CL_MEM_WRITE_ONLY, gpuOut.size() * sizeof(int), nullptr, &rc);

    const auto t1 = Clock::now();
    int cx0 = rOut, cz0 = rOut;
    api.pfnSetKernelArg(k, 0, sizeof(cl_mem), &dPref);
    api.pfnSetKernelArg(k, 1, sizeof(int), &PW);
    api.pfnSetKernelArg(k, 2, sizeof(int), &H);
    api.pfnSetKernelArg(k, 3, sizeof(int), &cx0);
    api.pfnSetKernelArg(k, 4, sizeof(int), &cz0);
    api.pfnSetKernelArg(k, 5, sizeof(int), &step);
    api.pfnSetKernelArg(k, 6, sizeof(int), &nx);
    api.pfnSetKernelArg(k, 7, sizeof(int), &nz);
    api.pfnSetKernelArg(k, 8, sizeof(int), &rIn);
    api.pfnSetKernelArg(k, 9, sizeof(int), &rOut);
    api.pfnSetKernelArg(k, 10, sizeof(cl_mem), &dOut);
    size_t global = static_cast<size_t>(centers);
    size_t local = 128;
    api.pfnEnqueueNDRangeKernel(q, k, 1, nullptr, &global, (global % local == 0) ? &local : nullptr, 0,
                                nullptr, nullptr);
    api.pfnEnqueueReadBuffer(q, dOut, CL_TRUE, 0, gpuOut.size() * sizeof(int), gpuOut.data(), 0, nullptr,
                             nullptr);
    const double gpuMs = msSince(t1);

    int mismatch = 0;
    for (int i = 0; i < centers; ++i) {
        if (gpuOut[static_cast<size_t>(i)] != cpuOut[static_cast<size_t>(i)]) ++mismatch;
    }
    std::printf("GPU 覆盖 %d 中心：%.2f ms（%.1f ns/中心，含上传/回读）| 与 CPU 不一致 %d 个\n", centers,
                gpuMs, gpuMs * 1e6 / centers, mismatch);
    std::printf("加速比：%.2fx（CPU %.2f ms → GPU %.2f ms）\n", cpuMs / gpuMs, cpuMs, gpuMs);
    return 0;
}
