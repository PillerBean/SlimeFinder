// ClApi.cpp - OpenCL 运行时动态加载实现
#include "gpu/ClApi.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace slimegpu {

namespace {

template <typename Fn>
bool resolveSym(void* module, const char* name, Fn& out)
{
    FARPROC p = GetProcAddress(static_cast<HMODULE>(module), name);
    if (!p) return false;
    out = reinterpret_cast<Fn>(p);
    return true;
}

inline bool dbgEnv()
{
    const char* e = std::getenv("SLIME_GPU_DEBUG");
    return e && *e == '1';
}

#define STR_(x) #x
#define STR(x) STR_(x)

#define RESOLVE(fn)                                                                      \
    do {                                                                                 \
        const char* sym = STR(cl##fn);                                                   \
        if (!resolveSym(module, sym, api.pfn##fn)) {                                     \
            if (dbgEnv()) std::fprintf(stderr, "[ocl-dbg] symbol missing: %s\n", sym);  \
            return false;                                                                \
        }                                                                                \
    } while (0)

bool tryResolveAll(void* module, ClApi& api)
{
    RESOLVE(GetPlatformIDs);
    RESOLVE(GetPlatformInfo);
    RESOLVE(GetDeviceIDs);
    RESOLVE(GetDeviceInfo);
    RESOLVE(CreateContext);
    RESOLVE(ReleaseContext);
    RESOLVE(CreateCommandQueue);
    RESOLVE(ReleaseCommandQueue);
    RESOLVE(CreateProgramWithSource);
    RESOLVE(BuildProgram);
    RESOLVE(GetProgramBuildInfo);
    RESOLVE(ReleaseProgram);
    RESOLVE(CreateKernel);
    RESOLVE(ReleaseKernel);
    RESOLVE(SetKernelArg);
    RESOLVE(CreateBuffer);
    RESOLVE(ReleaseMemObject);
    RESOLVE(EnqueueWriteBuffer);
    RESOLVE(EnqueueReadBuffer);
    RESOLVE(EnqueueNDRangeKernel);
    RESOLVE(Finish);
    RESOLVE(Flush);
    return true;
}

#undef RESOLVE

// 注册表 Vendors 键中的 ICD 路径
void registryIcdPaths(const char* subKey, std::vector<std::string>& out)
{
    HKEY h = nullptr;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, subKey, 0, KEY_READ, &h) != ERROR_SUCCESS) return;
    for (DWORD i = 0;; ++i) {
        char name[1024] = {0};
        DWORD nameLen = sizeof(name);
        DWORD type = 0;
        LSTATUS st = RegEnumValueA(h, i, name, &nameLen, nullptr, &type, nullptr, nullptr);
        if (st == ERROR_NO_MORE_ITEMS) break;
        if (st != ERROR_SUCCESS) continue;
        if (type != REG_DWORD) continue;
        if (nameLen > 0 && name[0] != '\0') out.emplace_back(name);
    }
    RegCloseKey(h);
}

// DriverStore 中 AMD 运行时（amdocl.inf 目录下的 amdocl64.dll），按版本倒序
void driverStoreAmd(std::vector<std::string>& out)
{
    char sysDir[MAX_PATH] = {0};
    GetSystemDirectoryA(sysDir, sizeof(sysDir));
    std::string root = std::string(sysDir) + "\\DriverStore\\FileRepository";
    WIN32_FIND_DATAA fd = {};
    std::string pattern = root + "\\amdocl.inf*";
    HANDLE hf = FindFirstFileA(pattern.c_str(), &fd);
    if (hf == INVALID_HANDLE_VALUE) return;

    struct Cand {
        std::string dir;
        uint64_t ver = 0;
    };
    std::vector<Cand> dirs;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
        std::string p = root + "\\" + fd.cFileName + "\\amdocl64.dll";
        DWORD ftype = GetFileAttributesA(p.c_str());
        if (ftype == INVALID_FILE_ATTRIBUTES || (ftype & FILE_ATTRIBUTE_DIRECTORY)) continue;
        // 用目录名的 amd64_<hash> 无法比较版本；读文件版本号
        DWORD verHandle = 0;
        DWORD verSize = GetFileVersionInfoSizeA(p.c_str(), &verHandle);
        uint64_t best = 0;
        if (verSize > 0) {
            std::vector<char> data(verSize);
            if (GetFileVersionInfoA(p.c_str(), 0, verSize, data.data())) {
                VS_FIXEDFILEINFO* ffi = nullptr;
                UINT len = 0;
                if (VerQueryValueA(data.data(), "\\", (void**)&ffi, &len) && ffi) {
                    best = ((uint64_t)ffi->dwFileVersionMS << 32) | ffi->dwFileVersionLS;
                }
            }
        }
        dirs.push_back({p, best});
    } while (FindNextFileA(hf, &fd));
    FindClose(hf);

    std::sort(dirs.begin(), dirs.end(),
              [](const Cand& a, const Cand& b) { return a.ver > b.ver; });
    for (const auto& d : dirs) out.push_back(d.dir);
}

void system32Candidates(std::vector<std::string>& out)
{
    static const char* names[] = {
        "nvopencl.dll",      // NVIDIA
        "IntelOpenCL64.dll", // Intel
        "intelocl64.dll",    // Intel (legacy)
    };
    char sysDir[MAX_PATH] = {0};
    GetSystemDirectoryA(sysDir, sizeof(sysDir));
    for (const char* n : names) {
        std::string p = std::string(sysDir) + "\\" + n;
        if (GetFileAttributesA(p.c_str()) != INVALID_FILE_ATTRIBUTES) out.emplace_back(p);
    }
}

void normalize(const std::string& in, std::string& out)
{
    out = in;
    for (auto& ch : out) ch = static_cast<char>(tolower(static_cast<unsigned char>(ch)));
}

} // namespace

void collectCandidateModules(std::vector<std::string>& out)
{
    // 0) 环境变量强指（调试/特殊机器用）
    if (const char* forced = std::getenv("SLIME_GPU_MODULE")) {
        if (*forced) out.emplace_back(forced);
    }
    // 1) ICD 加载器（绝对路径，避免工作目录下同名 DLL 干扰）
    {
        char sysDir[MAX_PATH] = {0};
        GetSystemDirectoryA(sysDir, sizeof(sysDir));
        out.emplace_back(std::string(sysDir) + "\\OpenCL.dll");
    }
    registryIcdPaths("SOFTWARE\\Khronos\\OpenCL\\Vendors", out);          // 2) 注册表 ICD
    registryIcdPaths("SOFTWARE\\WOW6432Node\\Khronos\\OpenCL\\Vendors", out);
    driverStoreAmd(out);           // 3) DriverStore AMD 运行时
    system32Candidates(out);       // 4) System32 已知 ICD

    // 去重（保留首次出现顺序）
    std::vector<std::string> seen, dedup;
    for (const auto& c : out) {
        std::string n;
        normalize(c, n);
        bool dup = false;
        for (const auto& s : seen) {
            if (s == n) { dup = true; break; }
        }
        if (dup) continue;
        seen.push_back(n);
        dedup.push_back(c);
    }
    out.swap(dedup);
}

ClApi::ClApi(ClApi&& o) noexcept
{
    *this = std::move(o);
}

ClApi& ClApi::operator=(ClApi&& o) noexcept
{
    if (this != &o) {
        if (module) FreeLibrary(static_cast<HMODULE>(module)); // 释放旧持有
        module = o.module;
        o.module = nullptr; // 关键：源对象必须让渡所有权
        modulePath = std::move(o.modulePath);
        ok = o.ok;
        o.ok = false;
        pfnGetPlatformIDs = o.pfnGetPlatformIDs;
        pfnGetPlatformInfo = o.pfnGetPlatformInfo;
        pfnGetDeviceIDs = o.pfnGetDeviceIDs;
        pfnGetDeviceInfo = o.pfnGetDeviceInfo;
        pfnCreateContext = o.pfnCreateContext;
        pfnReleaseContext = o.pfnReleaseContext;
        pfnCreateCommandQueue = o.pfnCreateCommandQueue;
        pfnReleaseCommandQueue = o.pfnReleaseCommandQueue;
        pfnCreateProgramWithSource = o.pfnCreateProgramWithSource;
        pfnBuildProgram = o.pfnBuildProgram;
        pfnGetProgramBuildInfo = o.pfnGetProgramBuildInfo;
        pfnReleaseProgram = o.pfnReleaseProgram;
        pfnCreateKernel = o.pfnCreateKernel;
        pfnReleaseKernel = o.pfnReleaseKernel;
        pfnSetKernelArg = o.pfnSetKernelArg;
        pfnCreateBuffer = o.pfnCreateBuffer;
        pfnReleaseMemObject = o.pfnReleaseMemObject;
        pfnEnqueueWriteBuffer = o.pfnEnqueueWriteBuffer;
        pfnEnqueueReadBuffer = o.pfnEnqueueReadBuffer;
        pfnEnqueueNDRangeKernel = o.pfnEnqueueNDRangeKernel;
        pfnFinish = o.pfnFinish;
        pfnFlush = o.pfnFlush;
    }
    return *this;
}

bool ClApi::openModule(const std::string& path)
{
    if (module) closeModule();
    std::wstring wpath(path.begin(), path.end());
    HMODULE m = LoadLibraryW(wpath.c_str());
    if (dbgEnv()) {
        std::fprintf(stderr, "[ocl-dbg] LoadLibrary(%s) -> %p err=%lu\n", path.c_str(), (void*)m,
                     (unsigned long)GetLastError());
    }
    if (!m) return false;
    if (!tryResolveAll(m, *this)) {
        if (dbgEnv()) {
            FARPROC probe = GetProcAddress(m, "clGetPlatformIDs");
            std::fprintf(stderr, "[ocl-dbg]   直接 GetProcAddress(clGetPlatformIDs) -> %p err=%lu\n",
                         (void*)probe, (unsigned long)GetLastError());
        }
        FreeLibrary(m);
        return false;
    }
    module = m;
    modulePath = path;
    ok = true;
    if (dbgEnv()) std::fprintf(stderr, "[ocl-dbg] 解析成功: %s\n", path.c_str());
    return true;
}

void ClApi::closeModule()
{
    if (module) FreeLibrary(static_cast<HMODULE>(module));
    module = nullptr;
    modulePath.clear();
    ok = false;
}

const char* clErrorText(cl_int err)
{
    switch (err) {
    case CL_SUCCESS: return "CL_SUCCESS";
    case CL_DEVICE_NOT_FOUND: return "CL_DEVICE_NOT_FOUND";
    case CL_DEVICE_NOT_AVAILABLE: return "CL_DEVICE_NOT_AVAILABLE";
    case CL_COMPILER_NOT_AVAILABLE: return "CL_COMPILER_NOT_AVAILABLE";
    case CL_MEM_OBJECT_ALLOCATION_FAILURE: return "CL_MEM_OBJECT_ALLOCATION_FAILURE";
    case CL_OUT_OF_RESOURCES: return "CL_OUT_OF_RESOURCES";
    case CL_OUT_OF_HOST_MEMORY: return "CL_OUT_OF_HOST_MEMORY";
    case CL_BUILD_PROGRAM_FAILURE: return "CL_BUILD_PROGRAM_FAILURE";
    case CL_INVALID_VALUE: return "CL_INVALID_VALUE";
    case CL_INVALID_DEVICE_TYPE: return "CL_INVALID_DEVICE_TYPE";
    case CL_INVALID_PLATFORM: return "CL_INVALID_PLATFORM";
    case CL_INVALID_DEVICE: return "CL_INVALID_DEVICE";
    case CL_INVALID_CONTEXT: return "CL_INVALID_CONTEXT";
    case CL_INVALID_QUEUE_PROPERTIES: return "CL_INVALID_QUEUE_PROPERTIES";
    case CL_INVALID_COMMAND_QUEUE: return "CL_INVALID_COMMAND_QUEUE";
    case CL_INVALID_HOST_PTR: return "CL_INVALID_HOST_PTR";
    case CL_INVALID_MEM_OBJECT: return "CL_INVALID_MEM_OBJECT";
    case CL_INVALID_ARG_INDEX: return "CL_INVALID_ARG_INDEX";
    case CL_INVALID_ARG_VALUE: return "CL_INVALID_ARG_VALUE";
    case CL_INVALID_ARG_SIZE: return "CL_INVALID_ARG_SIZE";
    case CL_INVALID_KERNEL_ARGS: return "CL_INVALID_KERNEL_ARGS";
    case CL_INVALID_WORK_DIMENSION: return "CL_INVALID_WORK_DIMENSION";
    case CL_INVALID_WORK_GROUP_SIZE: return "CL_INVALID_WORK_GROUP_SIZE";
    case CL_INVALID_WORK_ITEM_SIZE: return "CL_INVALID_WORK_ITEM_SIZE";
    case CL_INVALID_GLOBAL_OFFSET: return "CL_INVALID_GLOBAL_OFFSET";
    case CL_INVALID_KERNEL: return "CL_INVALID_KERNEL";
    case CL_INVALID_KERNEL_DEFINITION: return "CL_INVALID_KERNEL_DEFINITION";
    case CL_INVALID_BUFFER_SIZE: return "CL_INVALID_BUFFER_SIZE";
    case CL_INVALID_OPERATION: return "CL_INVALID_OPERATION";
    default: return "UNKNOWN_OPENCL_ERROR";
    }
}

} // namespace slimegpu
