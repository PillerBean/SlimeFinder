// opt_probe29.cpp - 定位「图案流式产品级 7.2 ms vs 探针内核 0.84 ms」的差距来源
//   只测「每次调用都新建/释放 OpenCL 缓冲」的成本（产品路径每变体分配 4 个缓冲）。
#include "gpu/ClApi.h"

#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

namespace {
using Clock = std::chrono::steady_clock;
double msSince(Clock::time_point t0)
{
    return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}
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

    const struct {
        const char* name;
        size_t bytes;
        int reps;
    } cases[] = {
        { "命中缓冲 8 MB（maxResults=100 万）", 8u << 20, 40 },
        { "命中缓冲 1.6 MB（maxResults=20 万）", 1600000u, 40 },
        { "小缓冲 4 KB（掩码/计数）", 4096u, 200 },
    };
    for (const auto& c : cases) {
        double best = 1e9, total = 0;
        for (int r = 0; r < c.reps; ++r) {
            const auto t0 = Clock::now();
            cl_mem m = api.pfnCreateBuffer(ctx, CL_MEM_WRITE_ONLY, c.bytes, nullptr, &rc);
            if (!m) {
                std::printf("%s：创建失败\n", c.name);
                break;
            }
            api.pfnReleaseMemObject(m);
            const double ms = msSince(t0);
            total += ms;
            if (ms < best) best = ms;
        }
        std::printf("%-34s 单次最差 %7.3f ms，平均 %7.3f ms（%d 次）\n", c.name, best, total / c.reps,
                    c.reps);
    }
    return 0;
}
