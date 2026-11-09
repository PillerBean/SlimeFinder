// SehGuard.h - v3.6.2：把「驱动里抛出的原生异常」挡在应用层之外
//
// 背景：OpenCL 运行时（opencl.dll / 厂商 ICD）在某些驱动组合下会在 clGetPlatformIDs /
//   clCreateContext 里直接访问违规（0xC0000005），C++ 的 try/catch 抓不住，只能靠 Windows
//   结构化异常（SEH）。这里把调用包一层 __try/__except：驱动崩了就当"GPU 不可用"，
//   主程序继续用 CPU 路径，而不是整个进程消失。
//
// 用法：
//   int rc = 0;
//   if (!slime::seh::guard([&] { rc = api->pfnGetPlatformIDs(0, nullptr, &n); })) {
//       // 驱动异常：按 GPU 不可用处理
//   }
#pragma once

#if defined(_MSC_VER) && defined(_WIN32)
#include <windows.h>
#endif

namespace slime {
namespace seh {

// 返回 true = 正常返回；false = 捕获到 SEH 异常（驱动崩溃）
template <typename Fn>
inline bool guard(Fn&& fn) noexcept
{
#if defined(_MSC_VER) && defined(_WIN32)
    __try {
        fn();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    return true;
#else
    fn();
    return true;
#endif
}

} // namespace seh
} // namespace slime
