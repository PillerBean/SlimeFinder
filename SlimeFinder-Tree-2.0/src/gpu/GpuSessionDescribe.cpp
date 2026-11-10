// GpuSessionDescribe.cpp - v3.3.1：GpuSession::describe() 的独立实现单元
//
// 说明：describe() 是 GpuSession 的对外摘要接口（供 tools/ocl_probe.cpp 打印设备信息）。
// 单独放在本文件（而非 GpuSession.cpp）是为了：
//   1) 与 GPU 内核实现解耦，避免大文件重构时遗漏该符号导致链接失败；
//   2) 只依赖 GpuSession 的只读查询接口（device()/modulePath()/lastError()），
//      不触碰任何 OpenCL 资源，天然线程安全、不会抛异常。
#include "gpu/GpuSession.h"

#include <cstdio>
#include <string>

namespace slimegpu {

std::string GpuSession::describe() const
{
    const GpuDeviceInfo& d = m_device;
    std::string s;
    if (!m_ready) {
        s = "GPU: 未就绪";
        if (!m_lastError.empty()) s += "（" + m_lastError + "）";
        return s;
    }
    char buf[512];
    std::snprintf(buf, sizeof(buf),
                  "GPU: %s (%s) | 平台: %s | OpenCL: %s | 驱动: %s | CU: %llu | 显存: %llu MB | "
                  "单次分配上限: %llu MB | 本地内存: %llu KB | 工作组上限: %llu",
                  d.name.empty() ? "?" : d.name.c_str(), d.vendor.empty() ? "?" : d.vendor.c_str(),
                  d.platformName.empty() ? "?" : d.platformName.c_str(),
                  d.deviceVersion.empty() ? "?" : d.deviceVersion.c_str(),
                  d.driverVersion.empty() ? "?" : d.driverVersion.c_str(),
                  static_cast<unsigned long long>(d.maxComputeUnits),
                  static_cast<unsigned long long>(d.globalMemBytes / (1024ULL * 1024ULL)),
                  static_cast<unsigned long long>(d.maxMemAllocBytes / (1024ULL * 1024ULL)),
                  static_cast<unsigned long long>(d.localMemBytes / 1024ULL),
                  static_cast<unsigned long long>(d.maxWorkGroupSize));
    s = buf;
    if (!m_modulePath.empty()) s += " | 模块: " + m_modulePath;
    return s;
}

} // namespace slimegpu
