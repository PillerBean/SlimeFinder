#include "core/next/Bitmap.h"

#include "gpu/GpuScanAdapter.h"

#include <algorithm>
#include <chrono>
#include <vector>

#ifdef _OPENMP
#include <omp.h>
#endif

namespace slime::next {

bool CpuDeterminer::run(const slime::SearchOptions& opt, const std::atomic<bool>& cancel, Bitmap& out,
                        ScanStats& stats, std::string* err) const
{
    const int w = static_cast<int>(opt.maxX - opt.minX + 1);
    const int h = static_cast<int>(opt.maxZ - opt.minZ + 1);
    if (w <= 0 || h <= 0) {
        if (err) *err = "范围非法";
        return false;
    }
    Bitmap bmp;
    if (!bmp.init(static_cast<int>(opt.minX), static_cast<int>(opt.minZ), w, h)) {
        if (err) *err = "位图分配失败";
        return false;
    }
    const auto t0 = std::chrono::steady_clock::now();
#ifdef _OPENMP
#pragma omp parallel
#endif
    {
        std::vector<uint8_t> row(static_cast<size_t>(w));
#ifdef _OPENMP
#pragma omp for schedule(dynamic, 8)
#endif
        for (int rz = 0; rz < h; ++rz) {
            if (cancel.load(std::memory_order_relaxed)) continue;
            const int z = static_cast<int>(opt.minZ) + rz;
            if (opt.version == Version::Java) {
                SlimeEngine::fillSlimeRowJava(opt.worldSeed, z, static_cast<int>(opt.minX), w, row.data());
            } else {
                SlimeEngine::fillSlimeRowBedrock(z, static_cast<int>(opt.minX), w, row.data());
            }
            uint64_t* dst = bmp.row(z);
            for (int x = 0; x < w; ++x) {
                if (row[static_cast<size_t>(x)]) dst[x >> 6] |= 1ULL << (x & 63);
            }
        }
    }
    const auto t1 = std::chrono::steady_clock::now();
    if (cancel.load(std::memory_order_relaxed)) {
        if (err) *err = "cancelled";
        return false;
    }
    stats.ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    stats.chunks = static_cast<int64_t>(w) * h;
    stats.backend = "cpu";
    stats.gpuUsed = false;
    out = std::move(bmp);
    return true;
}

bool GpuDeterminer::run(const slime::SearchOptions& opt, const std::atomic<bool>& cancel, Bitmap& out,
                        ScanStats& stats, std::string* err) const
{
    if (!m_gpu || !m_gpu->available()) {
        if (err) *err = "GPU 不可用";
        return false;
    }
    const int w = static_cast<int>(opt.maxX - opt.minX + 1);
    const int h = static_cast<int>(opt.maxZ - opt.minZ + 1);
    if (w <= 0 || h <= 0) {
        if (err) *err = "范围非法";
        return false;
    }
    const int wpr = (w + 63) / 64;
    Bitmap bmp;
    if (!bmp.init(static_cast<int>(opt.minX), static_cast<int>(opt.minZ), w, h)) {
        if (err) *err = "位图分配失败";
        return false;
    }
    const auto t0 = std::chrono::steady_clock::now();
    std::vector<uint64_t> words;
    bool ok = m_gpu->scanRectWords(opt.version, opt.worldSeed, static_cast<int>(opt.minX),
                                   static_cast<int>(opt.minZ), w, h, words, err);
    if (ok && words.size() == static_cast<size_t>(wpr) * h) {
        bmp.words() = std::move(words);
    } else {
        std::vector<uint8_t> bytes;
        ok = m_gpu->scanRect(opt.version, opt.worldSeed, static_cast<int>(opt.minX),
                             static_cast<int>(opt.minZ), w, h, bytes, err);
        if (!ok || bytes.size() != static_cast<size_t>(w) * h) return false;
        for (int z = 0; z < h; ++z) {
            uint64_t* dst = bmp.row(static_cast<int>(opt.minZ) + z);
            for (int x = 0; x < w; ++x) {
                if (bytes[static_cast<size_t>(z) * w + x]) dst[x >> 6] |= 1ULL << (x & 63);
            }
        }
    }
    const auto t1 = std::chrono::steady_clock::now();
    if (cancel.load(std::memory_order_relaxed)) {
        if (err) *err = "cancelled";
        return false;
    }
    stats.ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    stats.chunks = static_cast<int64_t>(w) * h;
    stats.backend = "gpu";
    stats.gpuUsed = true;
    out = std::move(bmp);
    return true;
}

} // namespace slime::next
