// PerfLog.h - 引擎侧性能事件（v3.3.7.2 任务书步骤 2 / P1-D）
//
// 目的：任何一次「慢搜索」都能凭日志定位到 扫描 / 归并 / 剪枝 / CPU 趋势 / 内存峰值，
// 而不是靠猜。事件契约（字段固定，便于脚本解析）：
//
//   chunk.begin   ci=<块号> rows=<行数> z0=<起始行> t=<已用时ms>
//   chunk.end     ci=<块号> ms=<本块耗时> scanned=<本块扫描数> results=<本块结果数> t=<已用时ms>
//   chunk.merge   ci=<最后一块> wait=<等锁ms> ms=<归并耗时> entries=<归并前条目> added=<新增条目> batched=<批内块数>
//   cache.prune   ci=<块号> ms=<剪枝耗时> drops=<丢弃range数> entries=<剪枝后条目> bytes=<剪枝后字节>
//   cpu.sample    t=<已用时s> cpu=<相对全核占用%> cores=<核数> all=<各核之和%> ws=<工作集MB> peak=<峰值MB>
//                 （v3.4.13：cpu 恒在 0~100，all 恒在 0~100×cores；此前采样非线程安全，会出现不可能值）
//   mem.peak      t=<已用时s> peak=<峰值MB> ws=<工作集MB>
//   search.summary chunks=<块数> pruned=<剪枝块数> scanned=<已扫描>/<总量> results=<结果数>
//                  total=<总耗时ms> merge=<归并累计ms> prune=<剪枝累计ms> drops=<丢弃range总数>
//                  cpuAvg=<平均CPU%> wsPeak=<峰值MB@时刻s> conc=<并发>
//                  cacheWrite=<是否收集缓存> hollowSkips=<因0新增条目而跳过覆盖声明的块数>（v3.3.8 起）
//                  fill=<填充(扫描/判定)耗时ms> slideWindow=<密度滑窗耗时ms> rowsPerChunk=<每块数据行数>
//                  （v3.3.8 任务 6 / E3：fill 与 slideWindow 分列，用于判据「滑窗耗时可随位图缩小而下降吗」）
//   search.summary classic …（经典路径同名字段；SLIME_FILL_ONLY=1 时只测填充阶段）
//
// 约束：Qt 无关；可在任意线程调用；内部只做计数 + 调用 IncidentLog（其自身加锁），永不抛出。
#pragma once

#include <cstdint>
#include <mutex>
#include <string>

namespace slime {
namespace perf {

// 进程累计 CPU 时间（100ns 单位）；不可用返回 -1
int64_t cpuTime100ns() noexcept;
// 当前进程工作集（MB）；不可用返回 0
double currentWorkingSetMB() noexcept;
// 逻辑核数（至少 1）
int logicalCores() noexcept;

// CPU 采样器：记录上一次采样点，返回两次采样之间的 CPU 占用百分比（相对全部逻辑核）
//
// v3.4.13（任务书第 1 步）：本采样器此前**不是线程安全的** —— m_cpu100ns / m_ms 是普通成员，
//   而分块路径的每个工作线程都会调用它（SlimeEngine 的块收尾处，每 5 秒一次），
//   两个线程同时读写同一对基准值 ⇒ 得到的 dCpu/dMs 组合毫无意义，
//   日志里因此出现过 cpu=18750%（12 核机器的物理上限是 1200%）。
//   现在：① 内部加锁串行化；② 结果钳到物理可能范围；③ 同时给出两个口径。
class CpuSampler {
public:
    // 返回 true 表示本次真的采到了样（调用方据此决定是否写日志）
    //   cpuPercent：相对全部逻辑核的占用（0~100；100 = 所有核跑满）
    //   wsMB      ：当前进程工作集（MB）
    bool sample(double* cpuPercent, double* wsMB) noexcept;

    // v3.4.13：额外给出「各核占用之和」口径（0~100×cores，与任务管理器一致）
    bool sample(double* cpuPercent, double* cpuAllPct, double* wsMB) noexcept;

private:
    mutable std::mutex m_mu; // v3.4.13：采样状态只允许一个线程修改
    int64_t m_cpu100ns = -1;
    int64_t m_ms = -1;
    // v3.4.13：默认 0 = 尚未初始化（首次采样时取逻辑核数）。
    //   此前默认值是 1，而初始化条件是 \if (m_cores <= 0)\ ⇒ **永远不会被赋值**，
    //   于是 cpu= 实际输出的是「各核占用之和」（0~100×核数）而不是「相对全部逻辑核」，
    //   这也是历史日志里 CPU% 远超 100 的原因之一（新单元组 [27] 抓到）。
    int m_cores = 0;
};

// ---- 事件写入（转发到 IncidentLog，未初始化时静默丢弃） ----
void chunkBegin(int64_t ci, int64_t rows, int64_t z0, int64_t elapsedMs) noexcept;
void chunkEnd(int64_t ci, int64_t ms, int64_t scanned, int64_t results, int64_t elapsedMs) noexcept;
void chunkMerge(int64_t ci, int64_t waitMs, int64_t ms, int64_t entriesBefore, int64_t added,
                int64_t batched) noexcept;
void cachePrune(int64_t ci, int64_t ms, int64_t drops, int64_t entries, int64_t bytes) noexcept;
void cpuSample(int64_t elapsedMs, double cpuPercent, double wsMB, double peakMB) noexcept;
void memPeak(int64_t elapsedMs, double peakMB, double wsMB) noexcept;
void searchSummary(const std::string& text) noexcept;

} // namespace perf
} // namespace slime
