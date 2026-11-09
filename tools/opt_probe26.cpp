// opt_probe26.cpp - 三个候选项的快速可行性测试（不动产品代码）
//   A) 多命令队列并发：把同一批工作分到 1/2/3 个 queue，看能否叠出更多吞吐
//   B) 命中回写成本：同一内核在「稀疏命中」与「密集命中」下的耗时差
#include "gpu/ClApi.h"

#include <chrono>
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

// 每线程 64 个区块 × 396 步链（模拟基岩那种「重判定」负载），输出 ulong
const char* kSrc = R"CL(
static inline uint heavy_step(uint s, uint i)
{
    return 1812433253u * (s ^ (s >> 30u)) + i;
}

__kernel void heavy(long seed, int base, __global ulong* out)
{
    size_t idx = get_global_id(0);
    uint s0 = (uint)(seed) + (uint)idx * 2654435761u;
    uint s1 = s0 ^ (s0 >> 15);
    uint a = s1, b = s1 + 0x9E3779B9u;
    for (uint i = 2u; i <= 397u; ++i) {
        a = heavy_step(a, i);
        b = heavy_step(b, i);
    }
    out[idx] = (ulong)a | ((ulong)b << 32);
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
    cl_program prog = api.pfnCreateProgramWithSource(ctx, 1, &kSrc, nullptr, &rc);
    if (!prog || api.pfnBuildProgram(prog, 1, &dev, nullptr, nullptr, nullptr) != CL_SUCCESS) {
        std::printf("编译失败\n");
        return 0;
    }
    cl_kernel k = api.pfnCreateKernel(prog, "heavy", &rc);

    const int64_t items = 1 << 21;   // 209.7 万个 work-item，每个 64 区块 ≈ 1.34 亿区块当量
    cl_mem dOut = api.pfnCreateBuffer(ctx, CL_MEM_WRITE_ONLY, static_cast<size_t>(items) * sizeof(cl_ulong),
                                      nullptr, &rc);
    const cl_long seed = 12345;
    int base = 0;

    // 建 4 个队列
    const int maxQ = 4;
    cl_command_queue queues[maxQ] = { nullptr, nullptr, nullptr, nullptr };
    for (int i = 0; i < maxQ; ++i) queues[i] = api.pfnCreateCommandQueue(ctx, dev, 0, &rc);

    std::printf("多队列并发测试：%lld 个 work-item（每个 2×396 步链）\n\n", static_cast<long long>(items));
    for (int nq = 1; nq <= maxQ; ++nq) {
        double best = 1e9;
        for (int rep = 0; rep < 5; ++rep) {
            const auto t0 = Clock::now();
            const int64_t per = items / nq;
            for (int qi = 0; qi < nq; ++qi) {
                const int64_t off = qi * per;
                const int64_t cnt = (qi + 1 == nq) ? (items - off) : per;
                size_t global = static_cast<size_t>(cnt);
                api.pfnSetKernelArg(k, 0, sizeof(cl_long), &seed);
                api.pfnSetKernelArg(k, 1, sizeof(int), &base);
                api.pfnSetKernelArg(k, 2, sizeof(cl_mem), &dOut);
                api.pfnEnqueueNDRangeKernel(queues[qi], k, 1, nullptr, &global, nullptr, 0, nullptr, nullptr);
            }
            for (int qi = 0; qi < nq; ++qi) api.pfnFinish(queues[qi]);
            const double ms = msSince(t0);
            if (ms < best) best = ms;
        }
        std::printf("  %d 个队列：%7.2f ms（相对 1 队列 %.2fx）\n", nq, best, 0.0);
        if (nq == 1) {
            std::printf("    （基准 %.2f ms）\n", best);
        }
    }
    return 0;
}
