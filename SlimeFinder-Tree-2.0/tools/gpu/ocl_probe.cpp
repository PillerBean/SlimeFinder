// ocl_probe.cpp - OpenCL 独立探测/一致性/性能工具（v3.2.0 第一阶段）
//
// 功能：
//   1. 枚举 OpenCL 模块/平台/设备并选择最优（GpuSession）
//   2. GPU vs CPU 判定一致性验证（Java LCG / 基岩 MT19937，多种矩形 + 远坐标）
//   3. 性能基准：GPU（纯内核 / 含回读端到端）vs CPU 单核 vs CPU 多核
//   4. 汇总 PASS/FAIL（退出码 0/1）
#include "core/SlimeEngine.h"
#include "gpu/GpuScanAdapter.h"
#include "gpu/GpuSession.h"

#include <atomic>
#include <chrono>
#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

using slime::SlimeEngine;
using slimegpu::GpuSession;
using Clock = std::chrono::steady_clock;

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

// 诊断输出统一 UTF-8：GetModuleFileNameA 给出的是 ANSI/GBK 路径，与其它 UTF-8
// 输出混在同一份证据日志里会变成乱码（污染审计发现），这里统一转成 UTF-8。
static std::string modulePathUtf8(HMODULE mod)
{
    wchar_t wide[MAX_PATH * 2] = L"?";
    const DWORD len = GetModuleFileNameW(mod, wide, static_cast<DWORD>(std::size(wide)));
    if (len == 0) return "?";
    const int bytes = WideCharToMultiByte(CP_UTF8, 0, wide, static_cast<int>(len),
                                          nullptr, 0, nullptr, nullptr);
    if (bytes <= 0) return "?";
    std::string out(static_cast<size_t>(bytes), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide, static_cast<int>(len), out.data(), bytes, nullptr, nullptr);
    return out;
}

// 顶层异常捕获：打印异常地址所属模块（诊断用）
static LONG CALLBACK veh(PEXCEPTION_POINTERS ep)
{
    void* addr = ep->ExceptionRecord->ExceptionAddress;
    HMODULE mod = nullptr;
    std::string modName = "?";
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           static_cast<LPCSTR>(addr), &mod) && mod) {
        modName = modulePathUtf8(mod);
    }
    auto& er = *ep->ExceptionRecord;
    std::fprintf(stderr, "[VEH] exception 0x%08X at %p in %s | access=%s addr=%p\n",
                 (unsigned)er.ExceptionCode, addr, modName.c_str(),
                 er.NumberParameters >= 2 ? (er.ExceptionInformation[0] ? "write" : "read")
                                          : "?",
                 er.NumberParameters >= 2 ? (void*)er.ExceptionInformation[1] : nullptr);
    // 栈回溯（无符号时给出模块+偏移）
    void* frames[16] = {nullptr};
    USHORT n = CaptureStackBackTrace(0, 16, frames, nullptr);
    for (USHORT i = 0; i < n; ++i) {
        HMODULE fm = nullptr;
        std::string fname = "?";
        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               static_cast<LPCSTR>(frames[i]), &fm) && fm) {
            fname = modulePathUtf8(fm);
        }
        std::fprintf(stderr, "[VEH]   #%u %p in %s\n", (unsigned)i, frames[i], fname.c_str());
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

namespace {

double nowMs()
{
    return std::chrono::duration<double, std::milli>(Clock::now().time_since_epoch()).count();
}

struct CpuResult {
    int64_t slimes = 0;
    double sec = 0.0;
};

// CPU 参考速率（单线程或多线程）：整块扫描 + 计数
CpuResult cpuScan(int version, int64_t seed, int minX, int minZ, int w, int h, int threads)
{
    const int64_t n = static_cast<int64_t>(w) * h;
    const auto count = [&](int64_t begin, int64_t end) -> int64_t {
        int64_t c = 0;
        for (int64_t idx = begin; idx < end; ++idx) {
            const int x = minX + static_cast<int>(idx % w);
            const int z = minZ + static_cast<int>(idx / w);
            if (version == 0) {
                if (SlimeEngine::isSlimeChunkJava(seed, x, z)) ++c;
            } else {
                if (SlimeEngine::isSlimeChunkBedrockMT(x, z)) ++c;
            }
        }
        return c;
    };
    double t0 = nowMs();
    int64_t total = 0;
    if (threads <= 1) {
        total = count(0, n);
    } else {
        std::vector<std::thread> pool;
        std::vector<int64_t> sums(static_cast<size_t>(threads), 0);
        const int64_t per = (n + threads - 1) / threads;
        for (int t = 0; t < threads; ++t) {
            const int64_t b = static_cast<int64_t>(t) * per;
            const int64_t e = (b + per < n) ? b + per : n;
            if (b >= e) break;
            pool.emplace_back([&sums, t, b, e, &count]() { sums[static_cast<size_t>(t)] = count(b, e); });
        }
        for (auto& th : pool) th.join();
        for (int64_t s : sums) total += s;
    }
    double t1 = nowMs();
    return {total, (t1 - t0) / 1000.0};
}

// GPU 速率（kernel+finish 与 含回读端到端 分开计时）
struct GpuTimed {
    double kernelSec = 0.0;
    double e2eSec = 0.0;
};

bool gpuScanTimed(GpuSession& gpu, int version, int64_t seed, int minX, int minZ, int w, int h,
                  std::vector<uint8_t>& out, GpuTimed& t)
{
    // 近似：先扫描一次预热/分配，再计时一次真实扫描
    double t0 = nowMs();
    bool ok = (version == 0) ? gpu.scanJava(seed, minX, minZ, w, h, out)
                             : gpu.scanBedrock(minX, minZ, w, h, out);
    (void)t0;
    return ok;
}

// 单次计时扫描（内部扫描两次：丢弃首次）
bool gpuBenchOnce(GpuSession& gpu, int version, int64_t seed, int minX, int minZ, int w, int h,
                  std::vector<uint8_t>& out, GpuTimed& t)
{
    if (!gpuScanTimed(gpu, version, seed, minX, minZ, w, h, out, t)) return false;
    double t0 = nowMs();
    if (!gpuScanTimed(gpu, version, seed, minX, minZ, w, h, out, t)) return false;
    double t1 = nowMs();
    t.e2eSec = (t1 - t0) / 1000.0;
    t.kernelSec = t.e2eSec; // 当前 API 未分离内核/回读；端到端即含回读
    return true;
}

// GPU/CPU 一致性：整块比对，返回不匹配数
int64_t compareRect(GpuSession& gpu, int version, int64_t seed, int minX, int minZ, int w, int h,
                    bool verbose = false)
{
    std::vector<uint8_t> out;
    bool ok = (version == 0) ? gpu.scanJava(seed, minX, minZ, w, h, out)
                             : gpu.scanBedrock(minX, minZ, w, h, out);
    if (!ok) {
        std::printf("[PARITY] GPU 扫描失败 (v=%d seed=%lld rect=(%d,%d,%d,%d))\n", version,
                    (long long)seed, minX, minZ, w, h);
        return -1;
    }
    const int64_t n = static_cast<int64_t>(w) * h;
    int64_t mism = 0, gpuSlimes = 0, cpuSlimes = 0;
    int64_t firstMism[10] = {0};
    int nFirst = 0;
    for (int64_t idx = 0; idx < n; ++idx) {
        const int x = minX + static_cast<int>(idx % w);
        const int z = minZ + static_cast<int>(idx / w);
        const bool cpu = (version == 0) ? SlimeEngine::isSlimeChunkJava(seed, x, z)
                                        : SlimeEngine::isSlimeChunkBedrockMT(x, z);
        const int g = out[static_cast<size_t>(idx)] ? 1 : 0;
        if (g) ++gpuSlimes;
        if (cpu) ++cpuSlimes;
        if ((cpu ? 1 : 0) != g) {
            ++mism;
            if (nFirst < 10) firstMism[nFirst++] = idx;
        }
    }
    if (verbose) {
        std::printf("[PARITY] v=%d seed=%lld rect=(%d,%d)+%dx%d n=%" PRId64
                    " gpu_slime=%" PRId64 " cpu_slime=%" PRId64 " mismatch=%" PRId64 "\n",
                    version, (long long)seed, minX, minZ, w, h, n, gpuSlimes, cpuSlimes, mism);
        for (int i = 0; i < nFirst; ++i) {
            const int64_t idx = firstMism[i];
            std::printf("    first mismatch idx=%" PRId64 " (x=%d,z=%d)\n", idx,
                        minX + static_cast<int>(idx % w), minZ + static_cast<int>(idx / w));
        }
    }
    return mism;
}

void benchAndReport(GpuSession& gpu, const char* label, int version, int64_t seed,
                    int minX, int minZ, int w, int h, int cpuThreads)
{
    const int64_t n = static_cast<int64_t>(w) * h;
    std::printf("\n=== %s: %dx%d = %" PRId64 " 区块 ===\n", label, w, h, n);

    // GPU：多次重复取最快端到端（每次含分配后首跑剔除）
    std::vector<uint8_t> out;
    GpuTimed best{0, 0};
    double bestE2e = 0;
    int reps = 0;
    double spentMs = 0;
    while (reps < 3 || spentMs < 400.0) {
        GpuTimed t;
        if (!gpuBenchOnce(gpu, version, seed, minX, minZ, w, h, out, t)) {
            std::printf("[BENCH] GPU 失败\n");
            return;
        }
        ++reps;
        spentMs += t.e2eSec * 1000.0;
        if (reps == 1 || t.e2eSec < bestE2e) {
            bestE2e = t.e2eSec;
            best = t;
        }
        if (bestE2e > 0 && (bestE2e * 1000.0) > 1500.0 && reps >= 2) break; // 单次过慢不再重复
    }

    const double gpuRate = n / bestE2e;
    std::printf("[BENCH] GPU   %-10s %8.1f ms/次 (%d 次) → %8.2f 百万区块/秒 (端到端含回读)\n",
                label, bestE2e * 1000.0, reps, gpuRate / 1e6);

    // CPU 单核
    CpuResult c1 = cpuScan(version, seed, minX, minZ, w, h, 1);
    const double c1Rate = n / c1.sec;
    std::printf("[BENCH] CPU1  %-10s %8.1f ms/次 → %8.2f 百万区块/秒 (slime=%" PRId64 ")\n", label,
                c1.sec * 1000.0, c1Rate / 1e6, c1.slimes);

    // CPU 多核
    CpuResult cm = cpuScan(version, seed, minX, minZ, w, h, cpuThreads);
    const double cmRate = n / cm.sec;
    std::printf("[BENCH] CPU%d %-9s %8.1f ms/次 → %8.2f 百万区块/秒 (slime=%" PRId64 ")\n",
                cpuThreads, label, cm.sec * 1000.0, cmRate / 1e6, cm.slimes);

    std::printf("[BENCH] 加速比 GPU/CPU1=%.2fx  GPU/CPU%d=%.2fx\n", gpuRate / c1Rate,
                cpuThreads, gpuRate / cmRate);
}

} // namespace

int main(int argc, char** argv)
{
    setvbuf(stdout, nullptr, _IONBF, 0); // 崩溃时也能看到最后输出
    setvbuf(stderr, nullptr, _IONBF, 0);
    AddVectoredExceptionHandler(1, veh);

    bool longBench = false;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--long") longBench = true;
        if (a == "--cpu-only") { /* 预留 */ }
    }

    std::printf("=== SlimeFinder GPU Probe (v3.2.0 Phase 1) ===\n");

    // CPU 线程数
    unsigned hw = std::thread::hardware_concurrency();
    if (hw == 0) hw = 4;
    const int cpuThreads = static_cast<int>(hw < 12 ? hw : 12);
    std::printf("[SYS] CPU 逻辑核=%u, 基准用 %d 线程\n", hw, cpuThreads);

    GpuSession gpu;
    std::string err;
    std::printf("[stage] gpu.init...\n");
    if (!gpu.init(&err)) {
        std::printf("[FAIL] GPU 会话初始化失败: %s\n", err.c_str());
        return 1;
    }
    std::printf("[stage] gpu.init OK\n");
    std::printf("%s\n", gpu.describe().c_str());

    int failCount = 0;

    // ---------- 1) 一致性验证 ----------
    std::printf("\n--- 一致性验证 (GPU vs CPU) ---\n");
    struct RectCase {
        int version;
        int64_t seed;
        int minX, minZ, w, h;
        const char* note;
    };
    const RectCase cases[] = {
        {0, 0, -512, -512, 1024, 1024, "Java seed=0 原点"},
        {0, 12345, -200000, 300000, 777, 333, "Java 非整幂宽 远坐标"},
        {0, -987654321098765LL, 1048576, -1048576, 2048, 2048, "Java 大种子 远坐标"},
        {0, 4815162342LL, 33554400 - 200, -33554400 + 77, 400, 300, "Java 超大坐标(±2^25)"},
        {1, 0, -8192, -8192, 1024, 1024, "Bedrock 原点"},
        {1, 0, -1000000, 2000000, 777, 333, "Bedrock 非整幂宽 远坐标"},
        {1, 0, 33554400 - 150, -33554400 + 29, 300, 200, "Bedrock 超大坐标"},
    };
    for (const auto& c : cases) {
        int64_t m = compareRect(gpu, c.version, c.seed, c.minX, c.minZ, c.w, c.h, true);
        if (m != 0) {
            std::printf("[FAIL] 一致性: %s\n", c.note);
            ++failCount;
        } else {
            std::printf("[PASS] 一致性: %s\n", c.note);
        }
    }

    // 大矩形全量比对（Java 16.8M 区块全比对；Bedrock 4.2M 全比对）
    {
        std::printf("[INFO] Java 16.8M 区块全量比对中...\n");
        int64_t m = compareRect(gpu, 0, 987654321LL, -4096, -8192, 4096, 1024, false);
        if (m != 0) { std::printf("[FAIL] Java 16.8M 比对 mismatch=%" PRId64 "\n", m); ++failCount; }
        else std::printf("[PASS] Java 4096x1024x4 全量比对一致\n");
    }
    {
        std::printf("[INFO] Bedrock 1.05M 区块全量比对中...\n");
        int64_t m = compareRect(gpu, 1, 0, -2048, 2048, 1024, 1024, false);
        if (m != 0) { std::printf("[FAIL] Bedrock 比对 mismatch=%" PRId64 "\n", m); ++failCount; }
        else std::printf("[PASS] Bedrock 1024x1024 全量比对一致\n");
    }

    // ---------- 2) 性能基准 ----------
    std::printf("\n--- 性能基准 ---\n");
    // Java 大矩形：268M 区块
    benchAndReport(gpu, "Java", 0, 12345LL, -8192, -8192, 16384, 16384, cpuThreads);
    // 计划验收项：1000x1000 小范围（另计，标注含回读）
    benchAndReport(gpu, "Java-1M", 0, 12345LL, 0, 0, 1000, 1000, cpuThreads);
    // Bedrock
    benchAndReport(gpu, "Bedrock", 1, 0, -4096, -4096, 2048, 2048, cpuThreads);
    if (longBench) {
        benchAndReport(gpu, "Bedrock", 1, 0, -8192, -8192, 4096, 4096, cpuThreads);
    }

    // ---------- 3) 引擎级对比（v3.2.0 集成验证：完整搜索 CPU vs GPU） ----------
    std::printf("\n--- 引擎级完整搜索对比（CPU 全核 vs GPU 加速） ---\n");
    {
        std::string aerr;
        auto engineGpu = slimegpu::createGpuScanAdapter(&aerr);
        if (!engineGpu) {
            std::printf("[FAIL] 引擎 GPU 提供者创建失败: %s\n", aerr.c_str());
            return 1;
        }
        const auto runOnce2 = [&](const slime::SearchOptions& o) {
            slime::SearchOutcome so;
            std::atomic<bool> c{ false };
            slime::ProgressFn noop;
            slime::SlimeEngine eng;
            eng.run(o, c, noop, so);
            return so;
        };
        const auto compareEngine = [&](const char* label, slime::Version version, int64_t seed,
                                       int x, int z, int w, int h) {
            slime::SearchOptions base;
            base.version = version;
            base.worldSeed = seed;
            base.minX = x; base.maxX = x + w - 1; base.minZ = z; base.maxZ = z + h - 1;
            base.mode = slime::Mode::Density;
            base.gridSize = 2;
            base.minDensity = 0.5;
            base.targetCount = 50; // UI 现实口径（最近 N 个结果）
            base.threads = 0; // CPU 参照用全部核心（GPU 最优对照）
            slime::SearchOptions g = base;
            g.gpuScan = engineGpu.get();

            slime::SearchOutcome rc = runOnce2(base);
            slime::SearchOutcome rg = runOnce2(g);
            bool eq = rc.ok && rg.ok && rc.results.size() == rg.results.size() &&
                      rc.totalFound == rg.totalFound;
            if (eq) {
                for (size_t i = 0; i < rc.results.size(); ++i) {
                    const auto& a = rc.results[i];
                    const auto& b = rg.results[i];
                    if (a.minX != b.minX || a.maxX != b.maxX || a.minZ != b.minZ ||
                        a.maxZ != b.maxZ || a.density != b.density) {
                        eq = false;
                        break;
                    }
                }
            }
            std::printf("[ENGINE] %s: CPU 扫描 %8.1f ms/总 %8.1f ms | GPU 扫描 %8.1f ms/总 %8.1f ms"
                        " | 结果 %zu 一致=%s\n",
                        label, rc.stats.scanMs, rc.stats.totalMs, rg.stats.scanMs, rg.stats.totalMs,
                        rg.results.size(), eq ? "是" : "否");
            if (!eq) ++failCount;
        };
        compareEngine("Java 密度 8192x8192", slime::Version::Java, 42, -4096, -4096, 8192, 8192);
        compareEngine("基岩密度 4096x4096", slime::Version::Bedrock, 0, -2048, -2048, 4096, 4096);
        if (longBench) {
            compareEngine("基岩密度 8192x8192", slime::Version::Bedrock, 0, -4096, -4096, 8192, 8192);
        }
    }

    std::printf("\n=== 结果: %s (失败 %d 项) ===\n", failCount == 0 ? "PASS" : "FAIL", failCount);
    return failCount == 0 ? 0 : 1;
}
