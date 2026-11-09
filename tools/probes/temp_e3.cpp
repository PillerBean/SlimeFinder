// temp_e3.cpp -- v3.3.8 任务 6 / E3：填充 vs 密度滑窗的收益分解实验（不改算法，只读计时与日志）
//
// 四档（同一区域、同一参数，只改「分块位图大小」与「是否跳过匹配阶段」）：
//   E3-1 只填充（SLIME_FILL_ONLY=1，经典路径）                → fill
//   E3-2 填充 + 密度滑窗（经典路径，单张位图）                 → fill / slideWindow / match
//   E3-3 分块 + 位图 2 MB/块（SLIME_CHUNK_WORDS=262144）       → fill / slideWindow / rowsPerChunk
//   E3-4 分块 + 位图 20 MB/块（SLIME_CHUNK_WORDS=2500000）     → 同上（现状档）
// 判据（设计文档 §二）：若 slideWindow 耗时不随位图缩小而下降 ⇒ 任务 6 走「方向 B：改内核访存」；
//   若明显下降 ⇒ 「方向 A：缩小每块位图」有效。
//
// 用法：temp_e3.exe [halfWidth] [halfHeight]
#include "core/SlimeEngine.h"
#include "utils/IncidentLog.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace slime;

static SearchOutcome runOnce(const SearchOptions& opt)
{
    SearchOutcome out;
    std::atomic<bool> cancel{ false };
    SlimeEngine e;
    e.run(opt, cancel, nullptr, out);
    return out;
}

struct Row {
    std::string tag;
    double totalMs = 0, fillMs = 0, matchMs = 0, slideMs = 0;
    int64_t windows = 0, scanned = 0;
    size_t results = 0;
    int threads = 0;
};

static Row run(const char* tag, const SearchOptions& opt, const char* words, bool fillOnly)
{
#ifdef _WIN32
    _putenv_s("SLIME_FORCE_CHUNKED", words ? "1" : "0");
    _putenv_s("SLIME_CHUNK_WORDS", words ? words : "");
    _putenv_s("SLIME_FILL_ONLY", fillOnly ? "1" : "");
#else
    setenv("SLIME_FORCE_CHUNKED", words ? "1" : "0", 1);
    setenv("SLIME_CHUNK_WORDS", words ? words : "", 1);
    setenv("SLIME_FILL_ONLY", fillOnly ? "1" : "", 1);
#endif
    const SearchOutcome r = runOnce(opt);
    Row row;
    row.tag = tag;
    row.totalMs = r.stats.totalMs;
    row.fillMs = r.stats.fillMs;
    row.matchMs = r.stats.matchMs;
    row.slideMs = r.stats.slideWindowMs;
    row.windows = r.stats.windowsEvaluated;
    row.scanned = r.stats.scannedChunks;
    row.results = r.results.size();
    row.threads = r.stats.threadsUsed;
    std::printf("%-34s ok=%d total=%8.0fms fill=%8.0fms match=%8.0fms slideWindow=%8.0fms "
                "windows=%lld scanned=%lld results=%zu conc=%d\n",
                tag, r.ok ? 1 : 0, row.totalMs, row.fillMs, row.matchMs, row.slideMs,
                static_cast<long long>(row.windows), static_cast<long long>(row.scanned), row.results,
                row.threads);
    return row;
}

int main(int argc, char** argv)
{
    const int halfW = (argc > 1) ? std::atoi(argv[1]) : 8192;  // 默认 16384 宽
    const int halfH = (argc > 2) ? std::atoi(argv[2]) : 8192;  // 默认 16384 高
    // 日志目录：用于验证 search.summary 里确实有 fill / slideWindow / rowsPerChunk 字段
    const auto logDir = std::filesystem::temp_directory_path() / "slime_e3_logs";
    std::error_code ec;
    std::filesystem::remove_all(logDir, ec);
    std::filesystem::create_directories(logDir, ec);
    slime::IncidentLog::init(logDir, "e3-probe");

    SearchOptions o;
    o.version = Version::Java; // 与 1.02 GB 缓存无关，纯算法计时（Java 更快，聚焦滑窗）
    o.worldSeed = 0;
    o.mode = Mode::Density;
    o.gridSize = 5;
    o.minDensity = 0.3;
    o.minX = -halfW; o.maxX = halfW - 1;
    o.minZ = -halfH; o.maxZ = halfH - 1;
    o.targetCount = 1000;   // 堆不满（不触发提前停止）⇒ 走滑窗路径
    o.stopAtTarget = false; // 必须：滑窗路径要求未启用提前停止
    o.smartRange = false;   // 必须：滑窗路径要求光栅顺序
    o.threads = 0;
    o.algorithm = Algorithm::Exact;

    std::printf("=== E3：填充 vs 密度滑窗分解（区域 %d×%d = %lld 区块，g=%d thr=%.2f，全核）===\n",
                halfW * 2, halfH * 2, static_cast<long long>(halfW) * 4 * halfH, o.gridSize, o.minDensity);
    const Row r1 = run("E3-1 只填充（经典路径）", o, nullptr, true);
    const Row r2 = run("E3-2 填充+滑窗（经典路径）", o, nullptr, false);
    const Row r3 = run("E3-3 分块+位图 2MB/块", o, "262144", false);
    const Row r4 = run("E3-4 分块+位图 20MB/块", o, "2500000", false);

    // search.summary 契约检查（新字段必须出现）
    const auto lf = slime::IncidentLog::currentFile();
    int fillFields = 0, slideFields = 0, rowsFields = 0;
    std::printf("\n--- search.summary（%s）---\n", lf.string().c_str());
    std::ifstream in(lf);
    std::string line;
    while (std::getline(in, line)) {
        if (line.find("search.summary") == std::string::npos) continue;
        if (line.find("fill=") != std::string::npos) ++fillFields;
        if (line.find("slideWindow=") != std::string::npos) ++slideFields;
        if (line.find("rowsPerChunk=") != std::string::npos) ++rowsFields;
        std::printf("  %s\n", line.substr(0, 240).c_str());
    }
    std::printf("契约：含 fill= 的 summary %d 条 / 含 slideWindow= %d 条 / 含 rowsPerChunk= %d 条（应为 4/4/4）\n",
                fillFields, slideFields, rowsFields);

    std::printf("\n=== 判读 ===\n");
    const double fillShare = (r2.totalMs > 0) ? 100.0 * r2.fillMs / r2.totalMs : 0.0;
    std::printf("· E3-2 分解：fill=%.0fms（占墙钟 %.1f%%）、slideWindow=%.0fms（线程·毫秒，%d 线程）、"
                "match 墙钟=%.0fms\n",
                r2.fillMs, fillShare, r2.slideMs, r2.threads, r2.matchMs);
    std::printf("· 位图缩小（20MB→2MB/块）：slideWindow %.0f → %.0f（%+.1f%%）、墙钟 %.0f → %.0f（%+.1f%%）\n",
                r4.slideMs, r3.slideMs, (r4.slideMs > 0 ? 100.0 * (r3.slideMs - r4.slideMs) / r4.slideMs : 0.0),
                r4.totalMs, r3.totalMs, (r4.totalMs > 0 ? 100.0 * (r3.totalMs - r4.totalMs) / r4.totalMs : 0.0));
    std::printf("  ⇒ 若 slideWindow 基本不变（<10%%）而墙钟变差 ⇒ 方向 B（改内核访存）优先；\n");
    std::printf("     若 slideWindow 明显下降 ⇒ 方向 A（缩小每块位图）有效。\n");
    return 0;
}
