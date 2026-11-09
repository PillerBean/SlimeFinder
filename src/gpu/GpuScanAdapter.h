// GpuScanAdapter.h - 面向 SlimeEngine 的 GPU 扫描提供者（v3.2.1；自 v3.2.0 引入）
//
// 引擎（src/core/SlimeEngine.h 声明的 slime::GpuScan 接口）不依赖本模块：
// 本适配器位于 slime_gpu，把 GpuSession 包装为引擎可注入的实现。
// 引擎在“可用 + 超过阈值”时自动使用；失败自动回退 CPU，两者结果逐位一致。
#pragma once

#include <memory>
#include <string>

namespace slime {
class GpuScan;
}

namespace slimegpu {

// 创建 GPU 扫描提供者（内部初始化 OpenCL 会话）。
// 无可用设备时返回 nullptr（err 可空，非空时含说明）。
std::unique_ptr<slime::GpuScan> createGpuScanAdapter(std::string* err = nullptr);

} // namespace slimegpu
