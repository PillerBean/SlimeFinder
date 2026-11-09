// GpuSession.h - GPU 加速会话（v3.2.1；自 v3.2.0 引入）
//
// 职责：
//   1. 扫描候选 OpenCL 模块，选取含可用 GPU 设备的最优平台/设备
//      （无可用 GPU 时回退 CPU 设备；全部失败则 available()==false，由调用方回退纯 CPU）
//   2. 建立 context + command queue，编译并缓存内核（Java LCG / 基岩 MT19937）
//   3. 提供矩形区域扫描接口（结果 1 字节/区块，行主序，与 CPU 判定逐位一致）
//
// v3.3.1：GPU「密度匹配」内核的开发已暂停并移至 src/gpu/experimental/（作为 v3.4.0 起点），
//         本文件不再包含 matchDensity / DensityMatch* —— 密度匹配恒走 CPU 路径。
//
// 本类 Qt 无关、无全局状态；线程模型：同一会话的调用由内部互斥量串行化。
#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

namespace slimegpu {

// 设备信息（用于展示/选优，均为普通类型，便于跨模块使用）
struct GpuDeviceInfo {
    bool isGpu = false;        // false = CPU/其他类型设备
    std::string platformName;  // 平台名（如 "AMD Accelerated Parallel Processing"）
    std::string platformVendor;
    std::string platformVersion;
    std::string name;          // 设备名（如 "AMD Radeon RX 6750 GRE"）
    std::string vendor;
    std::string driverVersion; // 驱动版本
    std::string deviceVersion; // 设备支持的 OpenCL 版本
    std::string extensions;    // 空格分隔
    uint64_t maxComputeUnits = 0;  // 计算单元数
    uint32_t maxClockMhz = 0;      // 最大时钟（MHz）
    uint64_t globalMemBytes = 0;   // 全局显存
    uint64_t localMemBytes = 0;    // 工作组本地内存
    uint64_t maxWorkGroupSize = 0; // 工作组最大规模
    uint32_t addressBits = 0;
    uint64_t maxMemAllocBytes = 0; // 单次分配上限（CL_DEVICE_MAX_MEM_ALLOC_SIZE）
    uint64_t maxWorkItemSizes[3] = { 0, 0, 0 }; // 各维度工作组上限
};

class GpuSession {
public:
    GpuSession() = default;
    ~GpuSession(); // 释放全部 OpenCL 资源
    GpuSession(const GpuSession&) = delete;
    GpuSession& operator=(const GpuSession&) = delete;

    // 初始化：选取设备、创建上下文/队列、编译内核。
    // err 非空时输出错误描述（中文）。返回是否就绪（available()）。
    // v3.2.1：外层异常兜底——任何 C++ 异常都转换为失败返回，绝不外抛（不崩溃）。
    bool init(std::string* err = nullptr);

    bool available() const noexcept { return m_ready; }
    const GpuDeviceInfo& device() const noexcept { return m_device; }
    const std::string& modulePath() const noexcept { return m_modulePath; }
    const std::string& lastError() const noexcept { return m_lastError; }

    // 人类可读摘要（含模块来源、设备参数）
    std::string describe() const;

    // 扫描矩形区域（区块坐标，含边界），结果写入 out（尺寸恰为 w*h，行主序）。
    // 与 CPU 判定（SlimeEngine::isSlimeChunk*）逐位一致。
    bool scanJava(int64_t worldSeed, int minX, int minZ, int w, int h,
                  std::vector<uint8_t>& out, std::string* err = nullptr);
    // v3.7.3：Java 版打包字扫描（每 64 个区块一个 64 位字，行主序；wpr = ceil(w/64)）
    bool scanJavaWords(int64_t worldSeed, int minX, int minZ, int w, int h,
                       std::vector<uint64_t>& out, std::string* err);

    // v3.7.4：基岩版打包字扫描（回读量 1/8）
    bool scanBedrockWords(int minX, int minZ, int w, int h, std::vector<uint64_t>& out,
                          std::string* err);

    // v3.8.1（流程优化）：全 GPU 流式密度匹配（判定 + 窗口统计都在 GPU，只回传命中）
    struct StreamDensityHit {
        int x = 0;
        int z = 0;
        unsigned char count = 0;   // 窗口内史莱姆区块数（≤ 255；gridSize > 15 时调用方不用流式）
    };
    // v3.8.4：严格图案流式匹配（一个变体一次调用）
    bool patternStream(int64_t worldSeed, bool bedrock, int rows, int cols, int w, int h, int axMin,
                       int axMax, int azMin, int azMax, const uint32_t* enableMasks,
                       const uint32_t* greenMasks, uint32_t maxHits, const std::atomic<bool>& cancel,
                       std::vector<StreamDensityHit>& out, std::string* err);

    bool matchDensityStream(int64_t worldSeed, bool bedrock, int gridSize, int thr, int w, int h, int axMin,
                            int axMax, int azMin, int azMax, uint32_t maxHits, int64_t startX, int64_t startZ,
                            const std::atomic<bool>& cancel, std::vector<StreamDensityHit>& out,
                            std::string* err, uint32_t* outTotal = nullptr,
                            bool earlyStop = false, int64_t earlyStopTarget = 0,
                            uint32_t keepTop = 0);   // v3.10.12.1：只回传最近的 keepTop 个（0=全部）

    // v3.7.6：环形覆盖（占用图逐行前缀和 → 每个候选中心的环带格数）
    bool annulusCoverage(const int* pref2d, int w, int h, int originX, int originZ, int step, int nx,
                         int nz, int firstX, int firstZ, int innerRadius, int outerRadius,
                         std::vector<int>& out, std::string* err);

    bool scanBedrock(int minX, int minZ, int w, int h,
                     std::vector<uint8_t>& out, std::string* err = nullptr);

private:
    struct Impl; // OpenCL 对象（避免本头文件依赖 CL 类型）
    Impl* m_impl = nullptr;
    bool m_ready = false;
    GpuDeviceInfo m_device;
    std::string m_modulePath;
    std::string m_lastError;
    std::string m_kernelBuildLog; // 内核构建输出（编译失败时并入错误信息）

    bool initImpl(std::string* err); // init() 的内部实现（由 init 的异常兜底包裹）

    bool buildProgram(std::string* err);
    bool ensureOutBuffer(size_t bytes, std::string* err);
    bool runScan(int version, int64_t seed, int minX, int minZ, int w, int h,
                 std::vector<uint8_t>& out, std::string* err);
};

} // namespace slimegpu
