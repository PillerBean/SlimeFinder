// GpuSession.cpp - GPU 加速会话实现
#include "gpu/GpuSession.h"
#include "utils/SehGuard.h"
#include "gpu/ClApi.h"
#include "slime_kernel_embed.h" // 生成的 kSlimeKernelSource（源自 slime_kernel.cl）

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <mutex>
#include <string>
#include <utility>
#include <vector>
#include <queue>
#include <algorithm>

namespace slimegpu {

namespace {

// 调试开关：环境变量 SLIME_GPU_DEBUG=1（输出走 stderr，避免 stdout 缓冲丢失）
bool dbgEnv()
{
    const char* e = std::getenv("SLIME_GPU_DEBUG");
    return e && *e == '1';
}

std::string queryPlatformString(const ClApi& api, cl_platform_id p, cl_platform_info param)
{
    size_t sz = 0;
    if (api.pfnGetPlatformInfo(p, param, 0, nullptr, &sz) != CL_SUCCESS || sz == 0) return {};
    std::string s(sz, '\0');
    if (api.pfnGetPlatformInfo(p, param, sz, &s[0], nullptr) != CL_SUCCESS) return {};
    while (!s.empty() && s.back() == '\0') s.pop_back();
    return s;
}

std::string queryDeviceString(const ClApi& api, cl_device_id d, cl_device_info param)
{
    size_t sz = 0;
    if (api.pfnGetDeviceInfo(d, param, 0, nullptr, &sz) != CL_SUCCESS || sz == 0) return {};
    std::string s(sz, '\0');
    if (api.pfnGetDeviceInfo(d, param, sz, &s[0], nullptr) != CL_SUCCESS) return {};
    while (!s.empty() && s.back() == '\0') s.pop_back();
    return s;
}

template <typename T>
bool queryDevice(const ClApi& api, cl_device_id d, cl_device_info param, T& out)
{
    return api.pfnGetDeviceInfo(d, param, sizeof(T), &out, nullptr) == CL_SUCCESS;
}

// 设备可用性检查 + 信息填充；返回是否可用（CL_DEVICE_AVAILABLE == CL_TRUE）
bool fillDeviceInfo(const ClApi& api, cl_platform_id plat, cl_device_type type,
                    cl_device_id dev, GpuDeviceInfo& info)
{
    cl_bool avail = CL_FALSE;
    if (!queryDevice(api, dev, CL_DEVICE_AVAILABLE, avail)) return false;
    if (avail != CL_TRUE) return false;

    info.isGpu = (type & CL_DEVICE_TYPE_GPU) != 0;
    info.platformName = queryPlatformString(api, plat, CL_PLATFORM_NAME);
    info.platformVendor = queryPlatformString(api, plat, CL_PLATFORM_VENDOR);
    info.platformVersion = queryPlatformString(api, plat, CL_PLATFORM_VERSION);
    info.name = queryDeviceString(api, dev, CL_DEVICE_NAME);
    info.vendor = queryDeviceString(api, dev, CL_DEVICE_VENDOR);
    info.driverVersion = queryDeviceString(api, dev, CL_DRIVER_VERSION);
    info.deviceVersion = queryDeviceString(api, dev, CL_DEVICE_VERSION);
    info.extensions = queryDeviceString(api, dev, CL_DEVICE_EXTENSIONS);
    queryDevice(api, dev, CL_DEVICE_MAX_COMPUTE_UNITS, info.maxComputeUnits);
    queryDevice(api, dev, CL_DEVICE_MAX_CLOCK_FREQUENCY, info.maxClockMhz);
    queryDevice(api, dev, CL_DEVICE_GLOBAL_MEM_SIZE, info.globalMemBytes);
    queryDevice(api, dev, CL_DEVICE_LOCAL_MEM_SIZE, info.localMemBytes);
    queryDevice(api, dev, CL_DEVICE_MAX_WORK_GROUP_SIZE, info.maxWorkGroupSize);
    queryDevice(api, dev, CL_DEVICE_ADDRESS_BITS, info.addressBits);
    queryDevice(api, dev, CL_DEVICE_MAX_MEM_ALLOC_SIZE, info.maxMemAllocBytes);
    // 各维度工作组上限（size_t[3]）：2D kernel 的局部尺寸必须逐维满足
    {
        size_t dims[3] = { 0, 0, 0 };
        if (api.pfnGetDeviceInfo(dev, CL_DEVICE_MAX_WORK_ITEM_SIZES, sizeof(dims), dims, nullptr) == CL_SUCCESS) {
            info.maxWorkItemSizes[0] = dims[0];
            info.maxWorkItemSizes[1] = dims[1];
            info.maxWorkItemSizes[2] = dims[2];
        }
    }
    return true;
}

// 选择得分：GPU 远高于 CPU；同类内比较计算单元数 x 时钟
uint64_t deviceScore(const GpuDeviceInfo& info)
{
    uint64_t base = info.isGpu ? (1ULL << 52) : (1ULL << 30);
    return base + info.maxComputeUnits * 65536ULL + info.maxClockMhz;
}

std::string buildLog(const ClApi& api, cl_program prog, cl_device_id dev)
{
    cl_build_status st = CL_BUILD_NONE;
    if (api.pfnGetProgramBuildInfo(prog, dev, CL_PROGRAM_BUILD_STATUS, sizeof(st), &st, nullptr) != CL_SUCCESS) {
        return {};
    }
    size_t sz = 0;
    if (api.pfnGetProgramBuildInfo(prog, dev, CL_PROGRAM_BUILD_LOG, 0, nullptr, &sz) != CL_SUCCESS || sz == 0) {
        return {};
    }
    std::string s(sz, '\0');
    api.pfnGetProgramBuildInfo(prog, dev, CL_PROGRAM_BUILD_LOG, sz, &s[0], nullptr);
    return s;
}

} // namespace

// 持有全部 OpenCL 对象（不透明，避免向公共头泄漏 CL 类型）
struct GpuSession::Impl {
    std::mutex mu; // v3.2.0：引擎分块路径多线程并发调用扫描 → 串行化（GPU 单队列语义）
    ClApi api;
    cl_context ctx = nullptr;
    cl_command_queue queue = nullptr;
    cl_program program = nullptr;
    cl_kernel kJava = nullptr;
    cl_kernel kJavaWords = nullptr;   // v3.7.3
    // v3.7.7：环形覆盖的前缀表常驻显存（同一会话内反复搜索时不再每次重新分配/上传）
    cl_mem prefBuf = nullptr;
    size_t prefCap = 0;
    cl_kernel kBedrockWords = nullptr; // v3.7.4
    cl_kernel kAnnulus = nullptr;      // v3.7.6
    cl_kernel kDensityStream = nullptr; // v3.8.1
    cl_kernel kPatternStream = nullptr; // v3.8.4
    cl_kernel kBedrock = nullptr;
    cl_mem outBuf = nullptr;
    size_t outCap = 0;
    cl_platform_id platform = nullptr;
    cl_device_id device = nullptr;
};

GpuSession::~GpuSession()
{
    if (!m_impl) return;
    if (m_impl->kJava) m_impl->api.pfnReleaseKernel(m_impl->kJava);
    if (m_impl->kJavaWords) m_impl->api.pfnReleaseKernel(m_impl->kJavaWords);
    if (m_impl->kBedrockWords) m_impl->api.pfnReleaseKernel(m_impl->kBedrockWords);
    if (m_impl->kAnnulus) m_impl->api.pfnReleaseKernel(m_impl->kAnnulus);
    if (m_impl->kDensityStream) m_impl->api.pfnReleaseKernel(m_impl->kDensityStream);
    if (m_impl->kPatternStream) m_impl->api.pfnReleaseKernel(m_impl->kPatternStream);
    if (m_impl->prefBuf) m_impl->api.pfnReleaseMemObject(m_impl->prefBuf);
    if (m_impl->kBedrock) m_impl->api.pfnReleaseKernel(m_impl->kBedrock);
    if (m_impl->program) m_impl->api.pfnReleaseProgram(m_impl->program);
    if (m_impl->outBuf) m_impl->api.pfnReleaseMemObject(m_impl->outBuf);
    if (m_impl->queue) m_impl->api.pfnReleaseCommandQueue(m_impl->queue);
    if (m_impl->ctx) m_impl->api.pfnReleaseContext(m_impl->ctx);
    m_impl->api.closeModule();
    delete m_impl;
    m_impl = nullptr;
}

bool GpuSession::init(std::string* err)
{
    // v3.2.1：异常兜底——OpenCL 驱动/库在异常机器上的任何 C++ 异常都转为失败返回，
    // 保证 GUI/引擎侧「不可用即回退 CPU」，绝不外抛导致崩溃。
    try {
        return initImpl(err);
    } catch (const std::exception& e) {
        m_lastError = std::string("GPU 初始化异常：") + e.what();
    } catch (...) {
        m_lastError = "GPU 初始化异常：未知错误";
    }
    if (err) *err = m_lastError;
    return false;
}

bool GpuSession::initImpl(std::string* err)
{
    if (err) err->clear();
    const bool dbg = dbgEnv();

    std::vector<std::string> modules;
    collectCandidateModules(modules);
    if (dbg) {
        std::fprintf(stderr, "[gpu-dbg] candidates: %zu\n", modules.size());
        for (const auto& m : modules) std::fprintf(stderr, "    %s\n", m.c_str());
    }

    // 依次尝试每个模块，记录（模块、平台、设备、得分）最优项
    ClApi bestApi;
    cl_platform_id bestPlatform = nullptr;
    cl_device_id bestDevice = nullptr;
    GpuDeviceInfo bestInfo;
    std::string bestModule;
    uint64_t bestScore = 0;

    for (const std::string& mod : modules) {
        ClApi trial;
        if (!trial.openModule(mod)) {
            if (dbg) std::fprintf(stderr, "[gpu-dbg] openModule FAILED: %s (err=%lu)\n", mod.c_str(),
                                  (unsigned long)GetLastError());
            continue;
        }
        if (dbg) std::fprintf(stderr, "[gpu-dbg] openModule OK: %s\n", mod.c_str());

        cl_uint np = 0;
        // v3.6.2：驱动在 clGetPlatformIDs 里崩（0xC0000005）时按"不可用"处理，不让进程消失
        cl_int prc = CL_SUCCESS;
        bool sehOk = slime::seh::guard([&] { prc = trial.pfnGetPlatformIDs(0, nullptr, &np); });
        if (!sehOk) {
            std::fprintf(stderr, "[gpu] 驱动异常（clGetPlatformIDs / SEH），跳过该模块\n");
            continue;
        }
        if (prc != CL_SUCCESS || np == 0) {
            if (dbg) std::fprintf(stderr, "[gpu-dbg]   platforms=0 (rc=%d)\n", (int)prc);
            trial.closeModule();
            continue;
        }
        std::vector<cl_platform_id> plats(np);
        if (trial.pfnGetPlatformIDs(np, plats.data(), &np) != CL_SUCCESS || np == 0) {
            trial.closeModule();
            continue;
        }
        if (dbg) std::fprintf(stderr, "[gpu-dbg]   platforms=%u\n", np);

        // 模块内先选最优（同一模块只需提交一次函数表）
        cl_platform_id localPlat = nullptr;
        cl_device_id localDev = nullptr;
        GpuDeviceInfo localInfo;
        uint64_t localScore = 0;

        const cl_device_type kinds[2] = {CL_DEVICE_TYPE_GPU, CL_DEVICE_TYPE_CPU};
        int platIdx = 0;
        for (cl_platform_id plat : plats) {
            if (dbg) {
                std::string pn = queryPlatformString(trial, plat, CL_PLATFORM_NAME);
                std::fprintf(stderr, "[gpu-dbg]     platform[%d] %s\n", platIdx, pn.c_str());
            }
            ++platIdx;
            for (cl_device_type kind : kinds) {
                cl_uint nd = 0;
                cl_int drc = trial.pfnGetDeviceIDs(plat, kind, 0, nullptr, &nd);
                if (drc != CL_SUCCESS || nd == 0) {
                    if (dbg) std::fprintf(stderr, "[gpu-dbg]       devs %s=0 (rc=%d)\n",
                                          (kind & CL_DEVICE_TYPE_GPU) ? "GPU" : "CPU", (int)drc);
                    continue;
                }
                std::vector<cl_device_id> devs(nd);
                if (trial.pfnGetDeviceIDs(plat, kind, nd, devs.data(), &nd) != CL_SUCCESS) continue;
                if (dbg) std::fprintf(stderr, "[gpu-dbg]       devs %s=%u\n",
                                      (kind & CL_DEVICE_TYPE_GPU) ? "GPU" : "CPU", nd);
                for (cl_device_id dev : devs) {
                    if (dbg) {
                        std::string dn = queryDeviceString(trial, dev, CL_DEVICE_NAME);
                        cl_bool av = CL_FALSE;
                        trial.pfnGetDeviceInfo(dev, CL_DEVICE_AVAILABLE, sizeof(av), &av, nullptr);
                        std::fprintf(stderr, "[gpu-dbg]         dev \"%s\" available=%d\n", dn.c_str(), (int)av);
                    }
                    GpuDeviceInfo info;
                    if (!fillDeviceInfo(trial, plat, kind, dev, info)) continue;
                    uint64_t score = deviceScore(info);
                    if (score > localScore) {
                        localScore = score;
                        localInfo = std::move(info);
                        localPlat = plat;
                        localDev = dev;
                    }
                }
            }
        }

        if (localDev && localScore > bestScore) {
            bestApi.closeModule(); // 释放先前接管（若有）的模块
            bestScore = localScore;
            bestInfo = std::move(localInfo);
            bestPlatform = localPlat;
            bestDevice = localDev;
            bestModule = mod;
            bestApi = std::move(trial);
        }
        trial.closeModule(); // 已接管时 module 为空，安全无操作

        // 已获得可用 GPU 即停止：同一驱动被多次加载（loader 派发 + 直连）会破坏运行时状态
        if (bestDevice && bestInfo.isGpu) {
            if (dbg) std::fprintf(stderr, "[gpu-dbg] adopt GPU done, stop scanning\n");
            break;
        }
    }

    if (!bestDevice) {
        m_lastError = "未找到可用的 OpenCL 设备（已尝试 " + std::to_string(modules.size()) + " 个模块）";
        if (err) *err = m_lastError;
        return false;
    }
    if (dbg) std::fprintf(stderr, "[gpu-dbg] adopt module=%s device=%s\n",
                          bestModule.c_str(), bestInfo.name.c_str());

    m_impl = new Impl();
    m_impl->api = std::move(bestApi);
    m_impl->platform = bestPlatform;
    m_impl->device = bestDevice;
    m_device = std::move(bestInfo);
    m_modulePath = std::move(bestModule);

    // context（绑定平台）+ 命令队列
    cl_context_properties props[3] = {CL_CONTEXT_PLATFORM,
                                      reinterpret_cast<cl_context_properties>(bestPlatform), 0};
    cl_int rc = CL_SUCCESS;
    if (dbg) {
        auto& api = m_impl->api;
        HMODULE hmod = (HMODULE)api.module;
        const unsigned char* ep = (const unsigned char*)api.pfnCreateContext;
        std::fprintf(stderr, "[gpu-dbg] clCreateContext... pfn=%p module=%p firstbytes=%02X %02X %02X %02X %02X %02X %02X %02X\n",
                     (void*)api.pfnCreateContext, (void*)hmod,
                     ep[0], ep[1], ep[2], ep[3], ep[4], ep[5], ep[6], ep[7]);
    }
    // v3.6.2：同上 —— 某些驱动在 clCreateContext 里直接崩
    const bool ctxSeh = slime::seh::guard([&] {
        m_impl->ctx = m_impl->api.pfnCreateContext(props, 1, &m_impl->device, nullptr, nullptr, &rc);
    });
    if (!ctxSeh) {
        std::fprintf(stderr, "[gpu] 驱动异常（clCreateContext / SEH），GPU 不可用\n");
        m_impl->ctx = nullptr;
        return false;
    }
    if (dbg) std::fprintf(stderr, "[gpu-dbg] clCreateContext rc=%d ctx=%p\n", (int)rc, (void*)m_impl->ctx);
    if (!m_impl->ctx || rc != CL_SUCCESS) {
        m_lastError = std::string("clCreateContext 失败: ") + clErrorText(rc);
        if (err) *err = m_lastError;
        delete m_impl;
        m_impl = nullptr;
        return false;
    }
    if (dbg) std::fprintf(stderr, "[gpu-dbg] clCreateCommandQueue...\n");
    m_impl->queue = m_impl->api.pfnCreateCommandQueue(m_impl->ctx, m_impl->device, 0, &rc);
    if (dbg) std::fprintf(stderr, "[gpu-dbg] clCreateCommandQueue rc=%d queue=%p\n", (int)rc, (void*)m_impl->queue);
    if (!m_impl->queue || rc != CL_SUCCESS) {
        m_lastError = std::string("clCreateCommandQueue 失败: ") + clErrorText(rc);
        if (err) *err = m_lastError;
        delete m_impl;
        m_impl = nullptr;
        return false;
    }
    if (dbg) std::fprintf(stderr, "[gpu-dbg] buildProgram...\n");

    if (!buildProgram(err)) {
        delete m_impl;
        m_impl = nullptr;
        return false;
    }

    m_ready = true;
    return true;
}

bool GpuSession::buildProgram(std::string* err)
{
    cl_int rc = CL_SUCCESS;
    const char* src = kSlimeKernelSource;
    size_t srcLen = std::strlen(src);
    m_impl->program = m_impl->api.pfnCreateProgramWithSource(m_impl->ctx, 1, &src, &srcLen, &rc);
    if (!m_impl->program || rc != CL_SUCCESS) {
        m_lastError = std::string("clCreateProgramWithSource 失败: ") + clErrorText(rc);
        if (err) *err = m_lastError;
        return false;
    }
    rc = m_impl->api.pfnBuildProgram(m_impl->program, 1, &m_impl->device, nullptr, nullptr, nullptr);
    m_kernelBuildLog = buildLog(m_impl->api, m_impl->program, m_impl->device);
    if (dbgEnv()) std::fprintf(stderr, "[gpu-dbg] clBuildProgram rc=%d loglen=%zu\n", (int)rc,
                               m_kernelBuildLog.size());
    if (rc != CL_SUCCESS) {
        m_lastError = std::string("内核编译失败: ") + clErrorText(rc);
        if (!m_kernelBuildLog.empty()) {
            m_lastError += "\n--- 构建日志 ---\n" + m_kernelBuildLog;
        }
        if (err) *err = m_lastError;
        return false;
    }
    m_impl->kJava = m_impl->api.pfnCreateKernel(m_impl->program, "scan_java_slime", &rc);
    if (!m_impl->kJava || rc != CL_SUCCESS) {
        m_lastError = std::string("clCreateKernel(scan_java_slime) 失败: ") + clErrorText(rc);
        if (err) *err = m_lastError;
        return false;
    }
    // v3.7.3：打包字内核（可选增强；创建失败则引擎自动用字节路径）
    m_impl->kJavaWords = m_impl->api.pfnCreateKernel(m_impl->program, "scan_java_words", &rc);
    if (rc != CL_SUCCESS) m_impl->kJavaWords = nullptr;

    m_impl->kBedrockWords = m_impl->api.pfnCreateKernel(m_impl->program, "scan_bedrock_words", &rc);
    if (rc != CL_SUCCESS) m_impl->kBedrockWords = nullptr;

    // v3.7.6：环形覆盖内核（可选增强）
    m_impl->kAnnulus = m_impl->api.pfnCreateKernel(m_impl->program, "annulus_cov", &rc);
    if (rc != CL_SUCCESS) m_impl->kAnnulus = nullptr;

    // v3.8.1：流式密度匹配内核（可选增强）
    m_impl->kDensityStream = m_impl->api.pfnCreateKernel(m_impl->program, "density_stream", &rc);
    if (rc != CL_SUCCESS) m_impl->kDensityStream = nullptr;
    m_impl->kPatternStream = m_impl->api.pfnCreateKernel(m_impl->program, "pattern_stream", &rc);
    if (rc != CL_SUCCESS) m_impl->kPatternStream = nullptr;

    m_impl->kBedrock = m_impl->api.pfnCreateKernel(m_impl->program, "scan_bedrock_slime", &rc);
    if (!m_impl->kBedrock || rc != CL_SUCCESS) {
        m_lastError = std::string("clCreateKernel(scan_bedrock_slime) 失败: ") + clErrorText(rc);
        if (err) *err = m_lastError;
        return false;
    }
    // v3.3.1：GPU 密度匹配内核开发已暂停（代码移至 src/gpu/experimental/，作为 v3.4.0 起点），
    // 本模块只保留扫描内核；密度匹配恒走 CPU 路径（引擎侧接口默认返回 false）。
    return true;
}

bool GpuSession::ensureOutBuffer(size_t bytes, std::string* err)
{
    if (m_impl->outCap >= bytes && m_impl->outBuf) return true;
    if (m_impl->outBuf) {
        m_impl->api.pfnReleaseMemObject(m_impl->outBuf);
        m_impl->outBuf = nullptr;
        m_impl->outCap = 0;
    }
    cl_int rc = CL_SUCCESS;
    m_impl->outBuf = m_impl->api.pfnCreateBuffer(m_impl->ctx, CL_MEM_READ_WRITE, bytes, nullptr, &rc);
    if (!m_impl->outBuf || rc != CL_SUCCESS) {
        m_lastError = std::string("clCreateBuffer 失败: ") + clErrorText(rc);
        if (err) *err = m_lastError;
        return false;
    }
    m_impl->outCap = bytes;
    return true;
}

bool GpuSession::runScan(int version, int64_t seed, int minX, int minZ, int w, int h,
                         std::vector<uint8_t>& out, std::string* err)
{
    if (!m_ready || !m_impl) {
        m_lastError = "GPU 会话未就绪";
        if (err) *err = m_lastError;
        return false;
    }
    if (w <= 0 || h <= 0) {
        m_lastError = "非法扫描尺寸";
        if (err) *err = m_lastError;
        return false;
    }
    const size_t bytes = static_cast<size_t>(w) * static_cast<size_t>(h);
    std::lock_guard<std::mutex> lock(m_impl->mu); // 引擎多线程（分块路径）并发安全
    if (!ensureOutBuffer(bytes, err)) return false;

    cl_kernel k = (version == 0) ? m_impl->kJava : m_impl->kBedrock;
    cl_int rc = CL_SUCCESS;
    const auto setArg = [&](cl_uint idx, size_t sz, const void* v) {
        rc = m_impl->api.pfnSetKernelArg(k, idx, sz, v);
        return rc == CL_SUCCESS;
    };

    if (version == 0) {
        if (!setArg(0, sizeof(seed), &seed)) goto fail;
        if (!setArg(1, sizeof(minX), &minX)) goto fail;
        if (!setArg(2, sizeof(minZ), &minZ)) goto fail;
        if (!setArg(3, sizeof(w), &w)) goto fail;
        if (!setArg(4, sizeof(h), &h)) goto fail;
        if (!setArg(5, sizeof(cl_mem), &m_impl->outBuf)) goto fail;
    } else {
        if (!setArg(0, sizeof(minX), &minX)) goto fail;
        if (!setArg(1, sizeof(minZ), &minZ)) goto fail;
        if (!setArg(2, sizeof(w), &w)) goto fail;
        if (!setArg(3, sizeof(h), &h)) goto fail;
        if (!setArg(4, sizeof(cl_mem), &m_impl->outBuf)) goto fail;
    }

    {
        size_t global = bytes;
        rc = m_impl->api.pfnEnqueueNDRangeKernel(m_impl->queue, k, 1, nullptr, &global, nullptr,
                                                 0, nullptr, nullptr);
        if (rc != CL_SUCCESS) {
            m_lastError = std::string("clEnqueueNDRangeKernel 失败: ") + clErrorText(rc);
            if (err) *err = m_lastError;
            return false;
        }
        rc = m_impl->api.pfnFinish(m_impl->queue);
        if (rc != CL_SUCCESS) {
            m_lastError = std::string("clFinish 失败: ") + clErrorText(rc);
            if (err) *err = m_lastError;
            return false;
        }
        out.resize(bytes);
        rc = m_impl->api.pfnEnqueueReadBuffer(m_impl->queue, m_impl->outBuf, CL_TRUE, 0, bytes,
                                              out.data(), 0, nullptr, nullptr);
        if (rc != CL_SUCCESS) {
            m_lastError = std::string("clEnqueueReadBuffer 失败: ") + clErrorText(rc);
            if (err) *err = m_lastError;
            return false;
        }
    }
    return true;

fail:
    m_lastError = std::string("clSetKernelArg 失败: ") + clErrorText(rc);
    if (err) *err = m_lastError;
    return false;
}

bool GpuSession::scanJava(int64_t worldSeed, int minX, int minZ, int w, int h,
                          std::vector<uint8_t>& out, std::string* err)
{
    return runScan(0, worldSeed, minX, minZ, w, h, out, err);
}

namespace {
// v3.7.5：打包字内核的本地工作组大小。传 nullptr 让驱动自选往往拿不到最优占用率，
//   这里默认 64（所有 OpenCL 1.2 设备都支持），可用 SLIME_GPU_LOCAL 覆盖做 A/B。
size_t wordsLocalSize() noexcept
{
    static const size_t v = [] {
        const char* e = std::getenv("SLIME_GPU_LOCAL");
        const int n = (e && *e) ? std::atoi(e) : 64;
        return static_cast<size_t>(n > 0 ? n : 64);
    }();
    return v;
}
} // namespace

bool GpuSession::scanJavaWords(int64_t worldSeed, int minX, int minZ, int w, int h,
                               std::vector<uint64_t>& out, std::string* err)
{
    if (!m_ready || !m_impl) {
        m_lastError = "GPU 会话未就绪";
        if (err) *err = m_lastError;
        return false;
    }
    if (!m_impl->kJavaWords) {
        m_lastError = "打包字内核不可用";
        if (err) *err = m_lastError;
        return false;
    }
    if (w <= 0 || h <= 0) {
        m_lastError = "非法扫描尺寸";
        if (err) *err = m_lastError;
        return false;
    }
    const int wpr = (w + 63) / 64;
    const size_t words = static_cast<size_t>(wpr) * static_cast<size_t>(h);
    const size_t bytes = words * sizeof(cl_ulong);
    std::lock_guard<std::mutex> lock(m_impl->mu);
    if (!ensureOutBuffer(bytes, err)) return false;

    cl_kernel k = m_impl->kJavaWords;
    cl_int rc = CL_SUCCESS;
    const auto setArg = [&](cl_uint idx, size_t sz, const void* v) {
        rc = m_impl->api.pfnSetKernelArg(k, idx, sz, v);
        return rc == CL_SUCCESS;
    };
    if (!setArg(0, sizeof(worldSeed), &worldSeed)) goto fail;
    if (!setArg(1, sizeof(minX), &minX)) goto fail;
    if (!setArg(2, sizeof(minZ), &minZ)) goto fail;
    if (!setArg(3, sizeof(w), &w)) goto fail;
    if (!setArg(4, sizeof(h), &h)) goto fail;
    if (!setArg(5, sizeof(wpr), &wpr)) goto fail;
    if (!setArg(6, sizeof(cl_mem), &m_impl->outBuf)) goto fail;
    {
        size_t global = words;
        const size_t local = wordsLocalSize();
        rc = m_impl->api.pfnEnqueueNDRangeKernel(m_impl->queue, k, 1, nullptr, &global,
                                                 (local > 0 && global % local == 0) ? &local : nullptr,
                                                 0, nullptr, nullptr);
        if (rc != CL_SUCCESS) goto fail;
        out.resize(words);
        rc = m_impl->api.pfnEnqueueReadBuffer(m_impl->queue, m_impl->outBuf, CL_TRUE, 0, bytes,
                                              out.data(), 0, nullptr, nullptr);
        if (rc != CL_SUCCESS) goto fail;
    }
    return true;
fail:
    m_lastError = std::string("打包字扫描失败: ") + clErrorText(rc);
    if (err) *err = m_lastError;
    return false;
}

bool GpuSession::patternStream(int64_t worldSeed, bool bedrock, int rows, int cols, int w, int h,
                               int axMin, int axMax, int azMin, int azMax, const uint32_t* enableMasks,
                               const uint32_t* greenMasks, uint32_t maxHits, const std::atomic<bool>& cancel,
                               std::vector<StreamDensityHit>& out, std::string* err)
{
    out.clear();
    if (!m_ready || !m_impl) {
        if (err) *err = "GPU 会话未就绪";
        return false;
    }
    if (!m_impl->kPatternStream) {
        if (err) *err = "图案流式内核不可用";
        return false;
    }
    if (rows < 1 || rows > 32 || cols < 1 || cols > 32 || w < cols || h < rows || !enableMasks ||
        !greenMasks || maxHits == 0) {
        if (err) *err = "图案流式参数非法";
        return false;
    }
    const int tileW = 256, tileH = 64;   // v3.10.12.5：与内核 PAT_TILE_H=64 对齐
    const int strideX = tileW - cols + 1, strideZ = tileH - rows + 1;
    const int tilesX = (w - cols + 1 + strideX - 1) / strideX;
    const int anchorRowsZ = h - rows + 1;
    const int totalTilesZ = (anchorRowsZ + strideZ - 1) / strideZ;
    const bool dbg = std::getenv("SLIME_DEBUG") != nullptr;
    const auto tEnter = std::chrono::steady_clock::now();
    std::lock_guard<std::mutex> lock(m_impl->mu);
    const auto tLock = std::chrono::steady_clock::now();
    cl_kernel k = m_impl->kPatternStream;
    cl_int rc = CL_SUCCESS;
    cl_mem dHits = m_impl->api.pfnCreateBuffer(m_impl->ctx, CL_MEM_WRITE_ONLY,
                                               static_cast<size_t>(maxHits) * sizeof(cl_int2), nullptr, &rc);
    cl_mem dCounter = m_impl->api.pfnCreateBuffer(m_impl->ctx, CL_MEM_READ_WRITE, sizeof(cl_uint), nullptr, &rc);
    cl_mem dEn = m_impl->api.pfnCreateBuffer(m_impl->ctx, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
                                             static_cast<size_t>(rows) * sizeof(cl_uint),
                                             const_cast<uint32_t*>(enableMasks), &rc);
    cl_mem dGr = m_impl->api.pfnCreateBuffer(m_impl->ctx, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
                                             static_cast<size_t>(rows) * sizeof(cl_uint),
                                             const_cast<uint32_t*>(greenMasks), &rc);
    if (!dHits || !dCounter || !dEn || !dGr) {
        if (dHits) m_impl->api.pfnReleaseMemObject(dHits);
        if (dCounter) m_impl->api.pfnReleaseMemObject(dCounter);
        if (dEn) m_impl->api.pfnReleaseMemObject(dEn);
        if (dGr) m_impl->api.pfnReleaseMemObject(dGr);
        if (err) *err = "图案流式缓冲创建失败";
        return false;
    }
    const auto tAlloc = std::chrono::steady_clock::now();
    uint32_t zero = 0;
    m_impl->api.pfnEnqueueWriteBuffer(m_impl->queue, dCounter, CL_TRUE, 0, sizeof(zero), &zero, 0, nullptr,
                                      nullptr);
    const auto tZero = std::chrono::steady_clock::now();
    int bedrockFlag = bedrock ? 1 : 0;
    int ozBase = 0;
    const int minX = axMin, minZ = azMin;
    const auto setArg = [&](cl_uint idx, size_t sz, const void* v) {
        rc = m_impl->api.pfnSetKernelArg(k, idx, sz, v);
        return rc == CL_SUCCESS;
    };
    bool ok = setArg(0, sizeof(worldSeed), &worldSeed) && setArg(1, sizeof(bedrockFlag), &bedrockFlag) &&
              setArg(2, sizeof(minX), &minX) && setArg(3, sizeof(minZ), &minZ) && setArg(4, sizeof(w), &w) &&
              setArg(5, sizeof(h), &h) && setArg(6, sizeof(rows), &rows) && setArg(7, sizeof(cols), &cols) &&
              setArg(8, sizeof(ozBase), &ozBase) && setArg(9, sizeof(cl_mem), &dEn) &&
              setArg(10, sizeof(cl_mem), &dGr) && setArg(11, sizeof(axMin), &axMin) &&
              setArg(12, sizeof(axMax), &axMax) && setArg(13, sizeof(azMin), &azMin) &&
              setArg(14, sizeof(azMax), &azMax) && setArg(15, sizeof(maxHits), &maxHits) &&
              setArg(16, sizeof(cl_mem), &dHits) && setArg(17, sizeof(cl_mem), &dCounter);
    const auto tArgs = std::chrono::steady_clock::now();
    const int tilesPerBand = (std::max)(1, 1024 / strideZ);
    const size_t local = 256;
    size_t dispatches = 0;
    if (ok) {
        for (int startTileZ = 0; startTileZ < totalTilesZ; startTileZ += tilesPerBand) {
            if (cancel.load(std::memory_order_relaxed)) break;
            const int tilesZBand = (std::min)(tilesPerBand, totalTilesZ - startTileZ);
            ozBase = startTileZ * strideZ;
            if (m_impl->api.pfnSetKernelArg(k, 8, sizeof(ozBase), &ozBase) != CL_SUCCESS) {
                ok = false;
                break;
            }
            const size_t global = static_cast<size_t>(tilesX) * static_cast<size_t>(tilesZBand) * local;
            const cl_int err2 = m_impl->api.pfnEnqueueNDRangeKernel(m_impl->queue, k, 1, nullptr, &global, &local,
                                                                    0, nullptr, nullptr);
            if (err2 != CL_SUCCESS) {
                rc = err2;
                ok = false;
                break;
            }
            ++dispatches;
        }
    }
    const auto tDispatch = std::chrono::steady_clock::now();
    uint32_t total = 0;
    if (ok) {
        m_impl->api.pfnEnqueueReadBuffer(m_impl->queue, dCounter, CL_TRUE, 0, sizeof(total), &total, 0, nullptr,
                                         nullptr);
    }
    const auto tRead = std::chrono::steady_clock::now();
    if (!ok) {
        m_impl->api.pfnReleaseMemObject(dHits);
        m_impl->api.pfnReleaseMemObject(dCounter);
        m_impl->api.pfnReleaseMemObject(dEn);
        m_impl->api.pfnReleaseMemObject(dGr);
        if (err) *err = std::string("图案流式内核失败: ") + clErrorText(rc);
        return false;
    }
    const uint32_t n = (std::min)(total, maxHits);
    out.resize(n);
    if (n > 0) {
        std::vector<cl_int2> pos(n);
        m_impl->api.pfnEnqueueReadBuffer(m_impl->queue, dHits, CL_TRUE, 0, n * sizeof(cl_int2), pos.data(), 0,
                                         nullptr, nullptr);
        for (uint32_t i = 0; i < n; ++i) {
            out[i].x = pos[i].s[0];
            out[i].z = pos[i].s[1];
            out[i].count = 1;
        }
    }
    const auto tEnd = std::chrono::steady_clock::now();
    if (dbg) {
        const auto ms = [](auto a, auto b) {
            return std::chrono::duration<double, std::milli>(b - a).count();
        };
        std::fprintf(stderr,
                     "[gpu] 图案流式：入口→锁 %.2f | 分配 %.2f | 计数清零 %.2f | 设参 %.2f | 派发(%.0f 次) %.2f | "
                     "回读 %.2f | 总计 %.2f ms，命中 %u（上限 %u）\n",
                     ms(tEnter, tLock), ms(tLock, tAlloc), ms(tAlloc, tZero), ms(tZero, tArgs),
                     ms(tAlloc, tDispatch) > 0 ? 0.0 : 0.0, ms(tArgs, tDispatch), ms(tRead, tEnd),
                     ms(tEnter, tEnd), total, maxHits);
    }
    m_impl->api.pfnReleaseMemObject(dHits);
    m_impl->api.pfnReleaseMemObject(dCounter);
    m_impl->api.pfnReleaseMemObject(dEn);
    m_impl->api.pfnReleaseMemObject(dGr);
    if (total > maxHits) {
        if (err) *err = "命中数超过上限";
        return false;
    }
    return true;
}

bool GpuSession::matchDensityStream(int64_t worldSeed, bool bedrock, int gridSize, int thr, int w, int h,
                                    int axMin, int axMax, int azMin, int azMax, uint32_t maxHits,
                                    int64_t startX, int64_t startZ, const std::atomic<bool>& cancel,
                                    std::vector<StreamDensityHit>& out, std::string* err, uint32_t* outTotal,
                                    bool earlyStop, int64_t earlyStopTarget, uint32_t keepTop)
{
    out.clear();
    if (!m_ready || !m_impl) {
        if (err) *err = "GPU 会话未就绪";
        return false;
    }
    if (!m_impl->kDensityStream) {
        if (err) *err = "流式密度匹配内核不可用";
        return false;
    }
    if (gridSize < 1 || gridSize > 15 || w <= 0 || h <= 0 || maxHits == 0) {
        if (err) *err = "流式匹配参数非法";
        return false;
    }


    // ================= v3.10.11.1：结果自优化（提前停止） =================
    // 用户开关开启时：按「到起点的最小可能距离」排序行带，逐带派发；
    // 每带回传后并入 TopN（(d2, rank) 与 CPU 完全同口径），
    // 一旦凑够 earlyStopTarget 且下一条带的最小可能 d2 > 当前第 N 名 d2（严格 >）即停止。
    // 未扫描区域的最小距离都大于当前阈值 ⇒ 不会漏掉任何更近的候选，结果与全量扫描逐条一致。
    bool useEarlyStop = earlyStop && earlyStopTarget > 0;
    if (useEarlyStop) {
        // 探针：先只扫最近的一带，估算命中密度；若太少（稀疏命中/低阈值之外）就退回旧路径，
        //   避免「每批同步」在需要全图扫描时反而变慢。
        const int gp_g = gridSize;
        const int gp_strideX = 256 - gp_g + 1, gp_strideZ = 32 - gp_g + 1;
        const int gp_tilesX = (w - gp_g + 1 + gp_strideX - 1) / gp_strideX;
        const int gp_anchorRowsZ = h - gp_g + 1;
        const int gp_totalTilesZ = (gp_anchorRowsZ + gp_strideZ - 1) / gp_strideZ;
        const int gp_bandTiles = (std::max)(1, 160 / gp_strideZ);
        int gp_bestStart = 0;
        int64_t gp_bestMin = -1;
        for (int gs = 0; gs < gp_totalTilesZ; gs += gp_bandTiles) {
            const int64_t azBase = static_cast<int64_t>(azMin) + static_cast<int64_t>(gs) * gp_strideZ;
            const int rows = (std::min)(gp_bandTiles, gp_totalTilesZ - gs);
            const int64_t azHi = (std::min)(azBase + static_cast<int64_t>(rows) * gp_strideZ - 1,
                                            static_cast<int64_t>(azMax));
            int64_t dz = 0;
            if (azHi >= azBase) {
                const int64_t lo = 2 * azBase + (gp_g - 1) - 2 * startZ;
                const int64_t hi = 2 * azHi + (gp_g - 1) - 2 * startZ;
                dz = (lo <= 0 && hi >= 0) ? 0 : (std::min)(std::llabs(lo), std::llabs(hi));
            }
            const int64_t xlo = 2 * static_cast<int64_t>(axMin) + (gp_g - 1) - 2 * startX;
            const int64_t xhi = 2 * static_cast<int64_t>(axMax) + (gp_g - 1) - 2 * startX;
            const int64_t dx = (xlo <= 0 && xhi >= 0) ? 0 : (std::min)(std::llabs(xlo), std::llabs(xhi));
            const int64_t md = dx * dx + dz * dz;
            if (gp_bestMin < 0 || md < gp_bestMin) {
                gp_bestMin = md;
                gp_bestStart = gs;
            }
        }
        const uint32_t gpHitCap = 64;
        cl_int gprc = CL_SUCCESS;
        cl_mem gpHits = m_impl->api.pfnCreateBuffer(m_impl->ctx, CL_MEM_WRITE_ONLY,
                                                    gpHitCap * sizeof(cl_int2), nullptr, &gprc);
        cl_mem gpCounts = m_impl->api.pfnCreateBuffer(m_impl->ctx, CL_MEM_WRITE_ONLY, gpHitCap * sizeof(cl_uchar),
                                                      nullptr, &gprc);
        cl_mem gpCounter = m_impl->api.pfnCreateBuffer(m_impl->ctx, CL_MEM_READ_WRITE, sizeof(cl_uint), nullptr,
                                                       &gprc);
        if (gpHits && gpCounts && gpCounter) {
            std::lock_guard<std::mutex> gplock(m_impl->mu);
            cl_kernel gpk = m_impl->kDensityStream;
            const int minXg = axMin, minZg = azMin, gpg = gp_g;
            int gpmode = 0, gpoz = 0, gpbucket = 0;
            const int gpbedrock = bedrock ? 1 : 0;
            cl_long gpstart2x = 0, gpstart2z = 0, gpcut = 0;
            cl_mem gphist = nullptr;
            const auto setArgG = [&](cl_uint idx, size_t sz, const void* v) {
                return m_impl->api.pfnSetKernelArg(gpk, idx, sz, v) == CL_SUCCESS;
            };
            bool gpok = setArgG(0, sizeof(worldSeed), &worldSeed) && setArgG(1, sizeof(minXg), &minXg) &&
                        setArgG(2, sizeof(minZg), &minZg) && setArgG(3, sizeof(w), &w) &&
                        setArgG(4, sizeof(h), &h) && setArgG(5, sizeof(gpoz), &gpoz) &&
                        setArgG(6, sizeof(gpg), &gpg) && setArgG(7, sizeof(thr), &thr) &&
                        setArgG(8, sizeof(axMin), &axMin) && setArgG(9, sizeof(axMax), &axMax) &&
                        setArgG(10, sizeof(azMin), &azMin) && setArgG(11, sizeof(azMax), &azMax) &&
                        setArgG(12, sizeof(gpbedrock), &gpbedrock) && setArgG(13, sizeof(gpHitCap), &gpHitCap) &&
                        setArgG(14, sizeof(cl_mem), &gpHits) && setArgG(15, sizeof(cl_mem), &gpCounts) &&
                        setArgG(16, sizeof(cl_mem), &gpCounter) && setArgG(17, sizeof(gpmode), &gpmode) &&
                        setArgG(18, sizeof(gpbucket), &gpbucket) && setArgG(19, sizeof(gpstart2x), &gpstart2x) &&
                        setArgG(20, sizeof(gpstart2z), &gpstart2z) && setArgG(21, sizeof(gpcut), &gpcut) &&
                        setArgG(22, sizeof(cl_mem), &gphist);
            if (gpok) {
                const int rows = (std::min)(gp_bandTiles, gp_totalTilesZ - gp_bestStart);
                gpoz = gp_bestStart * gp_strideZ;
                m_impl->api.pfnSetKernelArg(gpk, 5, sizeof(gpoz), &gpoz);
                const uint32_t zeroG = 0;
                m_impl->api.pfnEnqueueWriteBuffer(m_impl->queue, gpCounter, CL_TRUE, 0, sizeof(zeroG), &zeroG, 0,
                                                  nullptr, nullptr);
                const size_t localG = 256;
                const size_t globalG = static_cast<size_t>(gp_tilesX) * static_cast<size_t>(rows) * localG;
                if (m_impl->api.pfnEnqueueNDRangeKernel(m_impl->queue, gpk, 1, nullptr, &globalG, &localG, 0,
                                                        nullptr, nullptr) != CL_SUCCESS) {
                    gpok = false;
                }
                if (gpok) {
                    uint32_t totalG = 0;
                    m_impl->api.pfnEnqueueReadBuffer(m_impl->queue, gpCounter, CL_TRUE, 0, sizeof(totalG), &totalG, 0,
                                                     nullptr, nullptr);
                    const int64_t need = (std::max)(int64_t{ 4 }, earlyStopTarget / 4);
                    if (static_cast<int64_t>(totalG) < need) useEarlyStop = false;
                }
            }
            m_impl->api.pfnReleaseMemObject(gpHits);
            m_impl->api.pfnReleaseMemObject(gpCounts);
            m_impl->api.pfnReleaseMemObject(gpCounter);
        } else {
            if (gpHits) m_impl->api.pfnReleaseMemObject(gpHits);
            if (gpCounts) m_impl->api.pfnReleaseMemObject(gpCounts);
            if (gpCounter) m_impl->api.pfnReleaseMemObject(gpCounter);
            useEarlyStop = false;
        }
    }
    if (useEarlyStop) {
        const int g2 = gridSize;
        const int tileW2 = 256, tileH2 = 64;
        const int strideX2 = tileW2 - g2 + 1, strideZ2 = tileH2 - g2 + 1;
        const int tilesX2 = (w - g2 + 1 + strideX2 - 1) / strideX2;
        const int anchorRowsZ2 = h - g2 + 1;
        const int totalTilesZ2 = (anchorRowsZ2 + strideZ2 - 1) / strideZ2;
        const int tilesPerBand2 = (std::max)(1, 160 / strideZ2);   // v3.10.11.1：细批次，近处少扫
        const size_t local2 = 256;
        const int64_t targetN = (std::min<int64_t>)(earlyStopTarget, static_cast<int64_t>(maxHits));

        struct Batch {
            int startTileZ = 0;
            int rows = 0;
            int64_t minD2 = 0;
        };
        const auto batchMinD2 = [&](int ozBaseRow, int rows) -> int64_t {
            // 内核里 oz 是「相对区域起点 minZ(=azMin) 的偏移」：绝对锚点行 = azMin + ozBaseRow + 行内偏移
            const int64_t azBase = static_cast<int64_t>(azMin) + static_cast<int64_t>(ozBaseRow);
            const int64_t azLo = (std::max)(azBase, static_cast<int64_t>(azMin));
            const int64_t azHi = (std::min)(azBase + static_cast<int64_t>(rows) * strideZ2 - 1,
                                            static_cast<int64_t>(azMax));
            int64_t dz = 0;
            if (azHi >= azLo) {
                const int64_t lo = 2 * azLo + (g2 - 1) - 2 * startZ;
                const int64_t hi = 2 * azHi + (g2 - 1) - 2 * startZ;
                dz = (lo <= 0 && hi >= 0) ? 0 : (std::min)(std::llabs(lo), std::llabs(hi));
            }
            const int64_t xlo = 2 * static_cast<int64_t>(axMin) + (g2 - 1) - 2 * startX;
            const int64_t xhi = 2 * static_cast<int64_t>(axMax) + (g2 - 1) - 2 * startX;
            const int64_t dx = (xlo <= 0 && xhi >= 0) ? 0 : (std::min)(std::llabs(xlo), std::llabs(xhi));
            return dx * dx + dz * dz;
        };
        std::vector<Batch> batches;
        batches.reserve(static_cast<size_t>(totalTilesZ2 / tilesPerBand2 + 1));
        for (int s = 0; s < totalTilesZ2; s += tilesPerBand2) {
            Batch b;
            b.startTileZ = s;
            b.rows = (std::min)(tilesPerBand2, totalTilesZ2 - s);
            b.minD2 = batchMinD2(s * strideZ2, b.rows);
            batches.push_back(b);
        }
        std::sort(batches.begin(), batches.end(), [](const Batch& a, const Batch& b) {
            if (a.minD2 != b.minD2) return a.minD2 < b.minD2;
            return a.startTileZ < b.startTileZ;
        });

        struct Cand {
            int64_t d2 = 0;
            int64_t rank = 0;
            int x = 0;
            int z = 0;
            int count = 0;
        };
        const auto betterCand = [](const Cand& a, const Cand& b) {
            if (a.d2 != b.d2) return a.d2 < b.d2;
            return a.rank < b.rank;
        };
        std::priority_queue<Cand, std::vector<Cand>, decltype(betterCand)> heap(betterCand);
        const int64_t nAz2 = static_cast<int64_t>(azMax) - azMin + 1;

        std::lock_guard<std::mutex> lock2(m_impl->mu);
        cl_kernel k2 = m_impl->kDensityStream;
        cl_int rc2 = CL_SUCCESS;
        cl_mem dHits2 = m_impl->api.pfnCreateBuffer(m_impl->ctx, CL_MEM_WRITE_ONLY,
                                                    static_cast<size_t>(maxHits) * sizeof(cl_int2), nullptr, &rc2);
        cl_mem dCounts2 = m_impl->api.pfnCreateBuffer(m_impl->ctx, CL_MEM_WRITE_ONLY,
                                                      static_cast<size_t>(maxHits) * sizeof(cl_uchar), nullptr, &rc2);
        cl_mem dCounter2 = m_impl->api.pfnCreateBuffer(m_impl->ctx, CL_MEM_READ_WRITE, sizeof(cl_uint), nullptr, &rc2);
        if (!dHits2 || !dCounts2 || !dCounter2) {
            if (dHits2) m_impl->api.pfnReleaseMemObject(dHits2);
            if (dCounts2) m_impl->api.pfnReleaseMemObject(dCounts2);
            if (dCounter2) m_impl->api.pfnReleaseMemObject(dCounter2);
            if (err) *err = "结果自优化缓冲区创建失败";
            return false;
        }
        const int minX2 = axMin, minZ2 = azMin;
        int mode2 = 0, ozBase2 = 0, bucketShift2 = 0;
        const int bedrockFlag2 = bedrock ? 1 : 0;
        cl_long start2x2 = 0, start2z2 = 0, cutoff2 = 0;
        cl_mem hist2 = nullptr;
        const auto setArg2 = [&](cl_uint idx, size_t sz, const void* v) {
            rc2 = m_impl->api.pfnSetKernelArg(k2, idx, sz, v);
            return rc2 == CL_SUCCESS;
        };
        bool ok2 = setArg2(0, sizeof(worldSeed), &worldSeed) && setArg2(1, sizeof(minX2), &minX2) &&
                   setArg2(2, sizeof(minZ2), &minZ2) && setArg2(3, sizeof(w), &w) && setArg2(4, sizeof(h), &h) &&
                   setArg2(5, sizeof(ozBase2), &ozBase2) && setArg2(6, sizeof(g2), &g2) &&
                   setArg2(7, sizeof(thr), &thr) && setArg2(8, sizeof(axMin), &axMin) &&
                   setArg2(9, sizeof(axMax), &axMax) && setArg2(10, sizeof(azMin), &azMin) &&
                   setArg2(11, sizeof(azMax), &azMax) && setArg2(12, sizeof(bedrockFlag2), &bedrockFlag2) &&
                   setArg2(13, sizeof(maxHits), &maxHits) && setArg2(14, sizeof(cl_mem), &dHits2) &&
                   setArg2(15, sizeof(cl_mem), &dCounts2) && setArg2(16, sizeof(cl_mem), &dCounter2) &&
                   setArg2(17, sizeof(mode2), &mode2) && setArg2(18, sizeof(bucketShift2), &bucketShift2) &&
                   setArg2(19, sizeof(start2x2), &start2x2) && setArg2(20, sizeof(start2z2), &start2z2) &&
                   setArg2(21, sizeof(cutoff2), &cutoff2) && setArg2(22, sizeof(cl_mem), &hist2);

        uint32_t zero2 = 0;
        bool finished = false;
        size_t processed2 = 0;
        for (size_t bi = 0; ok2 && bi < batches.size() && !finished; ++bi) {
            if (cancel.load(std::memory_order_relaxed)) break;
            const Batch& b = batches[bi];
            // 计数清零用异步写（队列有序：写 → 内核读，无需等待），省掉每批一次阻塞往返
            m_impl->api.pfnEnqueueWriteBuffer(m_impl->queue, dCounter2, CL_FALSE, 0, sizeof(zero2), &zero2, 0,
                                              nullptr, nullptr);
            ozBase2 = b.startTileZ * strideZ2;
            if (m_impl->api.pfnSetKernelArg(k2, 5, sizeof(ozBase2), &ozBase2) != CL_SUCCESS) {
                ok2 = false;
                break;
            }
            const size_t global2 = static_cast<size_t>(tilesX2) * static_cast<size_t>(b.rows) * local2;
            if (m_impl->api.pfnEnqueueNDRangeKernel(m_impl->queue, k2, 1, nullptr, &global2, &local2, 0, nullptr,
                                                    nullptr) != CL_SUCCESS) {
                ok2 = false;
                break;
            }
            uint32_t total2 = 0;
            m_impl->api.pfnEnqueueReadBuffer(m_impl->queue, dCounter2, CL_TRUE, 0, sizeof(total2), &total2, 0,
                                              nullptr, nullptr);
            ++processed2;
            if (std::getenv("SLIME_DEBUG")) {
                std::fprintf(stderr, "[gpu] batch bi=%zu minD2=%lld total=%u\n", bi,
                             static_cast<long long>(b.minD2), total2);
            }
            if (total2 > maxHits) {
                ok2 = false;
                break;
            }
            if (total2 > 0) {
                std::vector<cl_int2> pos2(total2);
                std::vector<unsigned char> cnts2(total2);
                m_impl->api.pfnEnqueueReadBuffer(m_impl->queue, dHits2, CL_TRUE, 0, total2 * sizeof(cl_int2),
                                                 pos2.data(), 0, nullptr, nullptr);
                m_impl->api.pfnEnqueueReadBuffer(m_impl->queue, dCounts2, CL_TRUE, 0, total2 * sizeof(cl_uchar),
                                                 cnts2.data(), 0, nullptr, nullptr);
                for (uint32_t i = 0; i < total2; ++i) {
                    const int hx = pos2[i].s[0], hz = pos2[i].s[1];
                    const int64_t off2x = 2 * static_cast<int64_t>(hx) + (g2 - 1) - 2 * startX;
                    const int64_t off2z = 2 * static_cast<int64_t>(hz) + (g2 - 1) - 2 * startZ;
                    Cand c;
                    c.d2 = off2x * off2x + off2z * off2z;
                    c.rank = (static_cast<int64_t>(hx) - axMin) * nAz2 + (static_cast<int64_t>(hz) - azMin);
                    c.x = hx;
                    c.z = hz;
                    c.count = cnts2[i];
                    if (static_cast<int64_t>(heap.size()) < targetN) {
                        heap.push(c);
                    } else if (betterCand(c, heap.top())) {
                        heap.pop();
                        heap.push(c);
                    }
                }
            }
            if (static_cast<int64_t>(heap.size()) >= targetN) {
                const int64_t worstD2 = heap.top().d2;
                if (bi + 1 >= batches.size() || batches[bi + 1].minD2 > worstD2) finished = true;
            }
        }
        // 命中超过单带上限：回退 CPU（与旧行为一致，保证语义）
        if (ok2 && !finished && cancel.load(std::memory_order_relaxed)) {
            m_impl->api.pfnReleaseMemObject(dHits2);
            m_impl->api.pfnReleaseMemObject(dCounts2);
            m_impl->api.pfnReleaseMemObject(dCounter2);
            return false;
        }
        if (!ok2) {
            m_impl->api.pfnReleaseMemObject(dHits2);
            m_impl->api.pfnReleaseMemObject(dCounts2);
            m_impl->api.pfnReleaseMemObject(dCounter2);
            if (err) *err = "结果自优化内核失败或单带命中超过上限";
            return false;
        }
        m_impl->api.pfnReleaseMemObject(dHits2);
        m_impl->api.pfnReleaseMemObject(dCounts2);
        m_impl->api.pfnReleaseMemObject(dCounter2);
        out.clear();
        if (outTotal) *outTotal = static_cast<uint32_t>(heap.size());
        out.reserve(heap.size());
        while (!heap.empty()) {
            const Cand& c = heap.top();
            StreamDensityHit h2;
            h2.x = c.x;
            h2.z = c.z;
            h2.count = c.count;
            out.push_back(h2);
            heap.pop();
        }
        if (std::getenv("SLIME_DEBUG")) {
            std::fprintf(stderr, "[gpu] selfopt processed=%zu/%zu retained=%zu target=%lld\n", processed2,
                         batches.size(), out.size(), static_cast<long long>(targetN));
            std::fprintf(stderr, "[gpu] 结果自优化：%dx%d g=%d 阈值 %d，保留 %zu 个（目标 %lld，批次 %zu）\n", w, h, g2,
                         thr, out.size(), static_cast<long long>(targetN), batches.size());
        }
        return true;
    }

    const int g = gridSize;
    const int tileW = 256, tileH = 64;   // v3.10.12.5：与内核 DENS_TILE_H=64 对齐
    const int strideX = tileW - g + 1, strideZ = tileH - g + 1;
    if (w < g || h < g) {
        if (err) *err = "区域小于窗口";
        return false;
    }
    const int tilesX = (w - g + 1 + strideX - 1) / strideX;

    const auto tAllStart = std::chrono::steady_clock::now();
    std::lock_guard<std::mutex> lock(m_impl->mu);
    cl_kernel k = m_impl->kDensityStream;
    cl_int rc = CL_SUCCESS;
    cl_mem dHits = m_impl->api.pfnCreateBuffer(m_impl->ctx, CL_MEM_WRITE_ONLY,
                                               static_cast<size_t>(maxHits) * sizeof(cl_int2), nullptr, &rc);
    cl_mem dCounts = m_impl->api.pfnCreateBuffer(m_impl->ctx, CL_MEM_WRITE_ONLY,
                                                 static_cast<size_t>(maxHits) * sizeof(cl_uchar), nullptr, &rc);
    cl_mem dCounter = m_impl->api.pfnCreateBuffer(m_impl->ctx, CL_MEM_READ_WRITE, sizeof(cl_uint), nullptr, &rc);
    cl_mem dHist = m_impl->api.pfnCreateBuffer(m_impl->ctx, CL_MEM_READ_WRITE, 1024 * sizeof(cl_uint), nullptr,
                                               &rc);
    if (!dHits || !dCounts || !dCounter || !dHist) {
        if (dHits) m_impl->api.pfnReleaseMemObject(dHits);
        if (dCounts) m_impl->api.pfnReleaseMemObject(dCounts);
        if (dCounter) m_impl->api.pfnReleaseMemObject(dCounter);
        if (dHist) m_impl->api.pfnReleaseMemObject(dHist);
        if (err) *err = "流式匹配缓冲区创建失败";
        return false;
    }
    if (std::getenv("SLIME_DEBUG")) {
        std::fprintf(stderr, "[gpu] alloc %.2f ms\n",
                     std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - tAllStart)
                         .count());
    }
    const int minX = axMin;
    const int minZ = azMin;
    const int64_t spanX = 2LL * w, spanZ = 2LL * h;
    const int64_t maxD2 = spanX * spanX + spanZ * spanZ;
    int bucketShift = 0;
    while ((maxD2 >> bucketShift) > 1023 && bucketShift < 40) ++bucketShift;
    const int64_t start2x = 2 * static_cast<int64_t>(axMin) - 2 * startX + (g - 1);
    const int64_t start2z = 2 * static_cast<int64_t>(azMin) - 2 * startZ + (g - 1);

    const auto setArg = [&](cl_uint idx, size_t sz, const void* v) {
        rc = m_impl->api.pfnSetKernelArg(k, idx, sz, v);
        return rc == CL_SUCCESS;
    };
    int mode = 0, ozBase = 0;
    const int bedrockFlag = bedrock ? 1 : 0;
    cl_mem histNull = nullptr;   // v3.10.12.1：块预留路径不再需要直方图原子
    cl_long start2xCl = static_cast<cl_long>(start2x), start2zCl = static_cast<cl_long>(start2z), cutoffD2 = 0;
    bool ok = setArg(0, sizeof(worldSeed), &worldSeed) && setArg(1, sizeof(minX), &minX) &&
              setArg(2, sizeof(minZ), &minZ) && setArg(3, sizeof(w), &w) && setArg(4, sizeof(h), &h) &&
              setArg(5, sizeof(ozBase), &ozBase) && setArg(6, sizeof(g), &g) && setArg(7, sizeof(thr), &thr) &&
              setArg(8, sizeof(axMin), &axMin) && setArg(9, sizeof(axMax), &axMax) &&
              setArg(10, sizeof(azMin), &azMin) && setArg(11, sizeof(azMax), &azMax) &&
              setArg(12, sizeof(bedrockFlag), &bedrockFlag) && setArg(13, sizeof(maxHits), &maxHits) &&
              setArg(14, sizeof(cl_mem), &dHits) && setArg(15, sizeof(cl_mem), &dCounts) &&
              setArg(16, sizeof(cl_mem), &dCounter) && setArg(17, sizeof(mode), &mode) &&
              setArg(18, sizeof(bucketShift), &bucketShift) && setArg(19, sizeof(start2xCl), &start2xCl) &&
              setArg(20, sizeof(start2zCl), &start2zCl) && setArg(21, sizeof(cutoffD2), &cutoffD2) &&
              setArg(22, sizeof(cl_mem), &histNull);

    const int anchorRowsZ = h - g + 1;
    const int totalTilesZ = (anchorRowsZ + strideZ - 1) / strideZ;
    const int tilesPerBand = (std::max)(1, 1024 / strideZ);
    const size_t local = 256;

    uint32_t zero = 0;
    const auto runPass = [&](int passMode, int64_t passCutoff) -> bool {
        cl_long cut = static_cast<cl_long>(passCutoff);
        if (m_impl->api.pfnSetKernelArg(k, 17, sizeof(passMode), &passMode) != CL_SUCCESS) return false;
        if (m_impl->api.pfnSetKernelArg(k, 21, sizeof(cut), &cut) != CL_SUCCESS) return false;
        m_impl->api.pfnEnqueueWriteBuffer(m_impl->queue, dCounter, CL_TRUE, 0, sizeof(zero), &zero, 0, nullptr,
                                          nullptr);
        for (int startTileZ = 0; startTileZ < totalTilesZ; startTileZ += tilesPerBand) {
            if (cancel.load(std::memory_order_relaxed)) return true;
            const int tilesZBand = (std::min)(tilesPerBand, totalTilesZ - startTileZ);
            ozBase = startTileZ * strideZ;
            if (m_impl->api.pfnSetKernelArg(k, 5, sizeof(ozBase), &ozBase) != CL_SUCCESS) return false;
            const size_t global = static_cast<size_t>(tilesX) * static_cast<size_t>(tilesZBand) * local;
            const cl_int err2 = m_impl->api.pfnEnqueueNDRangeKernel(m_impl->queue, k, 1, nullptr, &global, &local,
                                                                    0, nullptr, nullptr);
            if (err2 != CL_SUCCESS) {
                rc = err2;
                return false;
            }
        }
        return true;
    };

    uint32_t total = 0;
    const auto tDbg0 = std::chrono::steady_clock::now();
    if (ok) {
        ok = runPass(0, 0);
        if (ok) {
            uint32_t hist[1024] = { 0 };
            const double tDispatchMs =
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - tDbg0).count();
            if (std::getenv("SLIME_DEBUG")) {
                std::fprintf(stderr, "[gpu] dispatch+kernel %.2f ms (%dx%d)\n", tDispatchMs, w, h);
            }
            m_impl->api.pfnEnqueueReadBuffer(m_impl->queue, dCounter, CL_TRUE, 0, sizeof(total), &total, 0, nullptr,
                                             nullptr);
            const double tCounterMs =
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - tDbg0).count();
            if (std::getenv("SLIME_DEBUG")) {
                std::fprintf(stderr, "[gpu] counter read %.2f ms (%dx%d total=%u)\n", tCounterMs, w, h, total);
            }
            m_impl->api.pfnEnqueueReadBuffer(m_impl->queue, dHist, CL_TRUE, 0, sizeof(hist), hist, 0, nullptr,
                                             nullptr);
            // v3.9.8（正确性优先）：暂不启用回读裁剪 —— 实测裁剪后与 CPU 结果偶有不一致；
            //   先保证「命中 < 上限时全部回传」，密集超限仍回退 CPU。
            const uint32_t keepTarget = 0xFFFFFFFFu;
            if (total > keepTarget && total <= maxHits * 4u) {
                uint32_t acc = 0;
                int cutBucket = -1;
                for (int b = 0; b < 1024; ++b) {
                    acc += hist[b];
                    if (acc >= keepTarget) {
                        cutBucket = b;
                        break;
                    }
                }
                if (cutBucket >= 0) {
                    const int64_t cutD2 = ((static_cast<int64_t>(cutBucket) + 1) << bucketShift) - 1;
                    ok = runPass(1, cutD2);
                    if (ok) {
                        m_impl->api.pfnEnqueueReadBuffer(m_impl->queue, dCounter, CL_TRUE, 0, sizeof(total), &total,
                                                         0, nullptr, nullptr);
                    }
                }
            }
        }
    }
    if (!ok) {
        m_impl->api.pfnReleaseMemObject(dHits);
        m_impl->api.pfnReleaseMemObject(dCounts);
        m_impl->api.pfnReleaseMemObject(dCounter);
        m_impl->api.pfnReleaseMemObject(dHist);
        if (err) *err = std::string("流式匹配内核失败: ") + clErrorText(rc);
        return false;
    }
    if (std::getenv("SLIME_DEBUG")) {
        std::fprintf(stderr, "[gpu] 流式匹配：%dx%d g=%d 阈值 %d，命中 %u（上限 %u，分桶位移 %d）\n", w, h, g, thr,
                     total, maxHits, bucketShift);
    }
    if (outTotal) *outTotal = total;
    const uint32_t n = (std::min)(total, maxHits);
    if (n > 0) {
        std::vector<cl_int2> pos(n);
        std::vector<unsigned char> cnts(n);
        // v3.10.12.2：两个回读异步入队、只 Finish 一次（小到中等区域能省一次驱动往返）
        m_impl->api.pfnEnqueueReadBuffer(m_impl->queue, dHits, CL_FALSE, 0, n * sizeof(cl_int2), pos.data(), 0,
                                         nullptr, nullptr);
        m_impl->api.pfnEnqueueReadBuffer(m_impl->queue, dCounts, CL_FALSE, 0, n * sizeof(cl_uchar), cnts.data(), 0,
                                         nullptr, nullptr);
        m_impl->api.pfnFinish(m_impl->queue);
        if (std::getenv("SLIME_DEBUG")) {
            const double tReadMs =
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - tDbg0).count();
            std::fprintf(stderr, "[gpu] hits read %.2f ms (%dx%d n=%u)\n", tReadMs, w, h, n);
        }
        // v3.10.12.1：先在会话内筛出最近的 keepTop 个（(d2,rank) 与引擎同口径）——
        //   命中可能上百万，逐条填给上层再截断是本路径最大的宿主开销。
        uint32_t keep = (keepTop == 0 || keepTop >= n) ? n : keepTop;
        out.resize(keep);
        if (keep < n) {
            // v3.10.12.2：先算全部 d2 并做 1024 桶直方图（顺序访问，避免 nth_element 的随机访问）；
            //   找到「第 keep 名」所在桶后，仅收集 ≤ 该桶上界的候选（通常 keep+一个桶的量），
            //   再按 (d2,rank) 精确取前 keep。15 万命中场景从 ~1.9 ms 降到 ~0.4 ms。
            std::vector<int64_t> d2s(n);
            const auto d2Of = [&](uint32_t i) -> int64_t {
                const int hx = pos[i].s[0], hz = pos[i].s[1];
                const int64_t off2x = 2 * static_cast<int64_t>(hx) + (g - 1) - 2 * startX;
                const int64_t off2z = 2 * static_cast<int64_t>(hz) + (g - 1) - 2 * startZ;
                return off2x * off2x + off2z * off2z;
            };
            int64_t maxOff2x = (std::max)(std::llabs(2 * static_cast<int64_t>(axMin) + (g - 1) - 2 * startX),
                                          std::llabs(2 * static_cast<int64_t>(axMax) + (g - 1) - 2 * startX));
            int64_t maxOff2z = (std::max)(std::llabs(2 * static_cast<int64_t>(azMin) + (g - 1) - 2 * startZ),
                                          std::llabs(2 * static_cast<int64_t>(azMax) + (g - 1) - 2 * startZ));
            int64_t maxD2 = maxOff2x * maxOff2x + maxOff2z * maxOff2z;
            int shift = 0;
            while ((maxD2 >> shift) >= 1024 && shift < 40) ++shift;
            const uint32_t kBins = 1024;
            std::vector<uint32_t> hist(kBins, 0);
            for (uint32_t i = 0; i < n; ++i) {
                d2s[i] = d2Of(i);
                uint32_t b = static_cast<uint32_t>(d2s[i] >> shift);
                if (b >= kBins) b = kBins - 1;
                ++hist[b];
            }
            uint32_t acc = 0;
            int cutBin = static_cast<int>(kBins) - 1;
            for (uint32_t b = 0; b < kBins; ++b) {
                acc += hist[b];
                if (acc >= static_cast<uint32_t>(keep)) {
                    cutBin = static_cast<int>(b);
                    break;
                }
            }
            const int64_t cutD2 = (static_cast<int64_t>(cutBin) + 1) * (static_cast<int64_t>(1) << shift) - 1;
            std::vector<uint32_t> cand;
            cand.reserve(static_cast<size_t>(keep) + 64);
            for (uint32_t i = 0; i < n; ++i) {
                if (d2s[i] <= cutD2) cand.push_back(i);
            }
            if (cand.size() > keep) {
                const int64_t nAz = static_cast<int64_t>(azMax) - azMin + 1;
                std::vector<std::pair<int64_t, std::pair<int64_t, uint32_t>>> order;
                order.reserve(cand.size());
                for (uint32_t idx : cand) {
                    const int hx = pos[idx].s[0], hz = pos[idx].s[1];
                    const int64_t rank =
                        (static_cast<int64_t>(hx) - axMin) * nAz + (static_cast<int64_t>(hz) - azMin);
                    order.emplace_back(d2s[idx], std::pair<int64_t, uint32_t>{ rank, idx });
                }
                std::sort(order.begin(), order.end());
                cand.resize(static_cast<size_t>(keep));
                for (uint32_t i = 0; i < keep; ++i) cand[i] = order[i].second.second;
            }
            for (uint32_t i = 0; i < keep; ++i) {
                out[i].x = pos[cand[i]].s[0];
                out[i].z = pos[cand[i]].s[1];
                out[i].count = cnts[cand[i]];
            }
        } else {
            for (uint32_t i = 0; i < n; ++i) {
                out[i].x = pos[i].s[0];
                out[i].z = pos[i].s[1];
                out[i].count = cnts[i];
            }
        }
    }
    const double tBeforeRel =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - tAllStart).count();
    if (std::getenv("SLIME_DEBUG")) {
        std::fprintf(stderr, "[gpu] select/fill done %.2f ms (%dx%d)\n", tBeforeRel, w, h);
    }
    m_impl->api.pfnReleaseMemObject(dHits);
    m_impl->api.pfnReleaseMemObject(dCounts);
    m_impl->api.pfnReleaseMemObject(dCounter);
    m_impl->api.pfnReleaseMemObject(dHist);
    if (std::getenv("SLIME_DEBUG")) {
        std::fprintf(stderr, "[gpu] before-release %.2f ms (rel %.2f ms)\n", tBeforeRel,
                     std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - tAllStart)
                             .count() -
                         tBeforeRel);
        const double totalMs =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - tAllStart).count();
        const double afterReadMs =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - tAllStart).count();
        (void)afterReadMs;
        std::fprintf(stderr, "[gpu] matchDensity total %.2f ms (%dx%d n=%u)\n", totalMs, w, h, total);
    }
    if (total > maxHits) {
        if (err) *err = "命中数超过上限";
        return false;
    }
    return true;
}


bool GpuSession::annulusCoverage(const int* pref2d, int w, int h, int originX, int originZ,
                                 int step, int nx, int nz, int firstX, int firstZ, int innerRadius,
                                 int outerRadius, std::vector<int>& out, std::string* err)
{
    if (!m_ready || !m_impl) {
        m_lastError = "GPU 会话未就绪";
        if (err) *err = m_lastError;
        return false;
    }
    if (!m_impl->kAnnulus) {
        m_lastError = "环形覆盖内核不可用";
        if (err) *err = m_lastError;
        return false;
    }
    if (!pref2d || w <= 0 || h <= 0 || nx <= 0 || nz <= 0) {
        m_lastError = "环形覆盖参数非法";
        if (err) *err = m_lastError;
        return false;
    }
    const size_t prefCount = static_cast<size_t>(w + 1) * static_cast<size_t>(h + 1);
    const size_t prefBytes = prefCount * sizeof(cl_int);
    const size_t outCount = static_cast<size_t>(nx) * static_cast<size_t>(nz);
    const size_t outBytes = outCount * sizeof(cl_int);

    std::lock_guard<std::mutex> lock(m_impl->mu);
    cl_kernel k = m_impl->kAnnulus;
    cl_int rc = CL_SUCCESS;

    // v3.7.7：前缀表缓冲常驻（容量不够才重建），之后只用 enqueueWriteBuffer 覆盖内容
    if (m_impl->prefCap < prefBytes) {
        if (m_impl->prefBuf) {
            m_impl->api.pfnReleaseMemObject(m_impl->prefBuf);
            m_impl->prefBuf = nullptr;
            m_impl->prefCap = 0;
        }
        m_impl->prefBuf = m_impl->api.pfnCreateBuffer(m_impl->ctx, CL_MEM_READ_ONLY, prefBytes, nullptr, &rc);
        if (!m_impl->prefBuf || rc != CL_SUCCESS) {
            m_impl->prefBuf = nullptr;
            m_lastError = std::string("环形覆盖前缀表缓冲创建失败: ") + clErrorText(rc);
            if (err) *err = m_lastError;
            return false;
        }
        m_impl->prefCap = prefBytes;
    }
    rc = m_impl->api.pfnEnqueueWriteBuffer(m_impl->queue, m_impl->prefBuf, CL_FALSE, 0, prefBytes,
                                           const_cast<int*>(pref2d), 0, nullptr, nullptr);
    if (rc != CL_SUCCESS) {
        m_lastError = std::string("环形覆盖前缀表上传失败: ") + clErrorText(rc);
        if (err) *err = m_lastError;
        return false;
    }
    cl_mem dPref = m_impl->prefBuf;
    cl_mem dOut = m_impl->api.pfnCreateBuffer(m_impl->ctx, CL_MEM_WRITE_ONLY, outBytes, nullptr, &rc);
    if (!dOut || rc != CL_SUCCESS) {
        m_impl->api.pfnReleaseMemObject(dPref);
        m_lastError = std::string("环形覆盖输出缓冲创建失败: ") + clErrorText(rc);
        if (err) *err = m_lastError;
        return false;
    }

    const auto setArg = [&](cl_uint idx, size_t sz, const void* v) {
        rc = m_impl->api.pfnSetKernelArg(k, idx, sz, v);
        return rc == CL_SUCCESS;
    };
    bool ok = setArg(0, sizeof(cl_mem), &dPref) && setArg(1, sizeof(int), &w) &&
              setArg(2, sizeof(int), &h) && setArg(3, sizeof(int), &originX) &&
              setArg(4, sizeof(int), &originZ) && setArg(5, sizeof(int), &step) &&
              setArg(6, sizeof(int), &nx) && setArg(7, sizeof(int), &nz) &&
              setArg(8, sizeof(int), &firstX) && setArg(9, sizeof(int), &firstZ) &&
              setArg(10, sizeof(int), &innerRadius) && setArg(11, sizeof(int), &outerRadius) &&
              setArg(12, sizeof(cl_mem), &dOut);
    if (ok) {
        const size_t global = outCount;
        rc = m_impl->api.pfnEnqueueNDRangeKernel(m_impl->queue, k, 1, nullptr, &global, nullptr, 0,
                                                 nullptr, nullptr);
        ok = (rc == CL_SUCCESS);
    }
    if (ok) {
        out.resize(outCount);
        rc = m_impl->api.pfnEnqueueReadBuffer(m_impl->queue, dOut, CL_TRUE, 0, outBytes, out.data(), 0,
                                              nullptr, nullptr);
        ok = (rc == CL_SUCCESS);
    }
    m_impl->api.pfnReleaseMemObject(dOut);   // 前缀表缓冲常驻，不释放
    if (!ok) {
        m_lastError = std::string("环形覆盖计算失败: ") + clErrorText(rc);
        if (err) *err = m_lastError;
        return false;
    }
    return true;
}

bool GpuSession::scanBedrockWords(int minX, int minZ, int w, int h,
                                  std::vector<uint64_t>& out, std::string* err)
{
    if (!m_ready || !m_impl) {
        m_lastError = "GPU 会话未就绪";
        if (err) *err = m_lastError;
        return false;
    }
    if (!m_impl->kBedrockWords) {
        m_lastError = "基岩打包字内核不可用";
        if (err) *err = m_lastError;
        return false;
    }
    if (w <= 0 || h <= 0) {
        m_lastError = "非法扫描尺寸";
        if (err) *err = m_lastError;
        return false;
    }
    const int wpr = (w + 63) / 64;
    const size_t words = static_cast<size_t>(wpr) * static_cast<size_t>(h);
    const size_t bytes = words * sizeof(cl_ulong);
    std::lock_guard<std::mutex> lock(m_impl->mu);
    if (!ensureOutBuffer(bytes, err)) return false;

    cl_kernel k = m_impl->kBedrockWords;
    cl_int rc = CL_SUCCESS;
    const auto setArg = [&](cl_uint idx, size_t sz, const void* v) {
        rc = m_impl->api.pfnSetKernelArg(k, idx, sz, v);
        return rc == CL_SUCCESS;
    };
    if (!setArg(0, sizeof(minX), &minX)) goto fail;
    if (!setArg(1, sizeof(minZ), &minZ)) goto fail;
    if (!setArg(2, sizeof(w), &w)) goto fail;
    if (!setArg(3, sizeof(h), &h)) goto fail;
    if (!setArg(4, sizeof(wpr), &wpr)) goto fail;
    if (!setArg(5, sizeof(cl_mem), &m_impl->outBuf)) goto fail;
    {
        size_t global = words;
        const size_t local = wordsLocalSize();
        rc = m_impl->api.pfnEnqueueNDRangeKernel(m_impl->queue, k, 1, nullptr, &global,
                                                 (local > 0 && global % local == 0) ? &local : nullptr,
                                                 0, nullptr, nullptr);
        if (rc != CL_SUCCESS) goto fail;
        out.resize(words);
        rc = m_impl->api.pfnEnqueueReadBuffer(m_impl->queue, m_impl->outBuf, CL_TRUE, 0, bytes,
                                              out.data(), 0, nullptr, nullptr);
        if (rc != CL_SUCCESS) goto fail;
    }
    return true;
fail:
    m_lastError = std::string("基岩打包字扫描失败: ") + clErrorText(rc);
    if (err) *err = m_lastError;
    return false;
}

bool GpuSession::scanBedrock(int minX, int minZ, int w, int h,
                             std::vector<uint8_t>& out, std::string* err)
{
    return runScan(1, 0, minX, minZ, w, h, out, err);
}

} // namespace slimegpu

