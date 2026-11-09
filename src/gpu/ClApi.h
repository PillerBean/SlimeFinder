// ClApi.h - OpenCL 运行时动态加载（v3.2.1 GPU 加速；自 v3.2.0 引入）
//
// 设计目标：不依赖任何 OpenCL SDK/导入库，运行期 LoadLibrary 动态加载：
//   1) 系统 ICD 加载器 opencl.dll（标准路径，正常机器经注册表 Vendors 派发）
//   2) 注册表 HKLM\SOFTWARE\Khronos\OpenCL\Vendors 列出的 ICD（直连）
//   3) DriverStore 中的 AMD 运行时 amdocl64.dll（部分机器注册表缺失时的兜底）
//   4) System32 下已知 ICD 名称
//
// 使用 Khronos 官方头文件（third_party/OpenCL-Headers）仅作类型/常量来源，
// 不做链接（函数全部经函数指针调用）。收集到的候选由 GpuSession 依次尝试，
// 选取第一个含可用（CL_DEVICE_AVAILABLE）GPU 设备的模块。
#pragma once

#define CL_TARGET_OPENCL_VERSION 120
#include <CL/cl.h>

#include <string>
#include <vector>

namespace slimegpu {

// OpenCL 1.2 核心 API 函数指针表（签名逐字取自 Khronos cl.h）
struct ClApi {
    bool ok = false;           // 当前模块已成功解析
    void* module = nullptr;    // HMODULE（析构不自动释放，需显式 closeModule）
    std::string modulePath;    // 实际加载的模块（信息/日志用）

    // 独占所有权语义：移动后源对象 module 置空（默认移动会把裸指针"复制"，
    // 导致源对象 closeModule 时误卸载目标仍持有的模块——必须显式置空）
    ClApi() = default;
    ClApi(const ClApi&) = delete;
    ClApi& operator=(const ClApi&) = delete;
    ClApi(ClApi&& o) noexcept;
    ClApi& operator=(ClApi&& o) noexcept;

    cl_int (CL_API_CALL* pfnGetPlatformIDs)(cl_uint, cl_platform_id*, cl_uint*) = nullptr;
    cl_int (CL_API_CALL* pfnGetPlatformInfo)(cl_platform_id, cl_platform_info, size_t, void*, size_t*) = nullptr;
    cl_int (CL_API_CALL* pfnGetDeviceIDs)(cl_platform_id, cl_device_type, cl_uint, cl_device_id*, cl_uint*) = nullptr;
    cl_int (CL_API_CALL* pfnGetDeviceInfo)(cl_device_id, cl_device_info, size_t, void*, size_t*) = nullptr;
    cl_context (CL_API_CALL* pfnCreateContext)(const cl_context_properties*, cl_uint, const cl_device_id*,
                                               void (CL_CALLBACK*)(const char*, const void*, size_t, void*),
                                               void*, cl_int*) = nullptr;
    cl_int (CL_API_CALL* pfnReleaseContext)(cl_context) = nullptr;
    cl_command_queue (CL_API_CALL* pfnCreateCommandQueue)(cl_context, cl_device_id, cl_command_queue_properties, cl_int*) = nullptr;
    cl_int (CL_API_CALL* pfnReleaseCommandQueue)(cl_command_queue) = nullptr;
    cl_program (CL_API_CALL* pfnCreateProgramWithSource)(cl_context, cl_uint, const char**, const size_t*, cl_int*) = nullptr;
    cl_int (CL_API_CALL* pfnBuildProgram)(cl_program, cl_uint, const cl_device_id*, const char*,
                                          void (CL_CALLBACK*)(cl_program, void*), void*) = nullptr;
    cl_int (CL_API_CALL* pfnGetProgramBuildInfo)(cl_program, cl_device_id, cl_program_build_info, size_t, void*, size_t*) = nullptr;
    cl_int (CL_API_CALL* pfnReleaseProgram)(cl_program) = nullptr;
    cl_kernel (CL_API_CALL* pfnCreateKernel)(cl_program, const char*, cl_int*) = nullptr;
    cl_int (CL_API_CALL* pfnReleaseKernel)(cl_kernel) = nullptr;
    cl_int (CL_API_CALL* pfnSetKernelArg)(cl_kernel, cl_uint, size_t, const void*) = nullptr;
    cl_mem (CL_API_CALL* pfnCreateBuffer)(cl_context, cl_mem_flags, size_t, void*, cl_int*) = nullptr;
    cl_int (CL_API_CALL* pfnReleaseMemObject)(cl_mem) = nullptr;
    cl_int (CL_API_CALL* pfnEnqueueWriteBuffer)(cl_command_queue, cl_mem, cl_bool, size_t, size_t, const void*,
                                                cl_uint, const cl_event*, cl_event*) = nullptr;
    cl_int (CL_API_CALL* pfnEnqueueReadBuffer)(cl_command_queue, cl_mem, cl_bool, size_t, size_t, void*,
                                               cl_uint, const cl_event*, cl_event*) = nullptr;
    cl_int (CL_API_CALL* pfnEnqueueNDRangeKernel)(cl_command_queue, cl_kernel, cl_uint, const size_t*, const size_t*,
                                                  const size_t*, cl_uint, const cl_event*, cl_event*) = nullptr;
    cl_int (CL_API_CALL* pfnFinish)(cl_command_queue) = nullptr;
    cl_int (CL_API_CALL* pfnFlush)(cl_command_queue) = nullptr;

    // 尝试加载并解析指定模块；成功返回 true（ok=true）
    bool openModule(const std::string& path);
    // 释放当前模块（ok=false）
    void closeModule();
};

// 候选模块路径（有序，去重）；见文件头注释
void collectCandidateModules(std::vector<std::string>& out);

// 错误码 -> 文本（日志友好）
const char* clErrorText(cl_int err);

} // namespace slimegpu
