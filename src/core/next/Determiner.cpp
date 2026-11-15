#include "core/next/Bitmap.h"

#include "core/FastJava.h"
#include "gpu/GpuScanAdapter.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <vector>

#if defined(_M_X64) || defined(_M_IX86) || defined(__SSE2__)
#include <emmintrin.h>
#endif

#ifdef _OPENMP
#include <omp.h>
#endif

namespace slime::next {

namespace {

uint64_t javaRowBaseLocal(int64_t worldSeed, int32_t cz) noexcept
{
    const uint32_t t3 = static_cast<uint32_t>(cz) * static_cast<uint32_t>(cz);
    const uint32_t t4 = static_cast<uint32_t>(cz) * 389711u;
    uint64_t b = static_cast<uint64_t>(worldSeed);
    b += static_cast<uint64_t>(static_cast<int64_t>(static_cast<int32_t>(t3)) * 4392871LL);
    b += static_cast<uint64_t>(static_cast<int64_t>(static_cast<int32_t>(t4)));
    return b;
}

} // namespace

bool CpuDeterminer::run(const slime::SearchOptions& opt, const std::atomic<bool>& cancel, Bitmap& out,
                        ScanStats& stats, std::string* err, const slime::ProgressFn& progress) const
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
#ifdef _OPENMP
    if (opt.threads > 0) omp_set_num_threads(opt.threads);
#endif
    if (progress) progress(0.0, true, 0.0, 0.0);
    const auto t0 = std::chrono::steady_clock::now();
    std::atomic<int64_t> rowsDone{ 0 };
    const int reportEvery = (std::max)(1, h / 100);
    const bool javaFast = (opt.version == Version::Java) && fastjava::available();
#ifdef _OPENMP
#pragma omp parallel
#endif
    {
        std::vector<uint8_t> row(static_cast<size_t>((w + 63) / 64 * 64) + 16);
#ifdef _OPENMP
#pragma omp for schedule(dynamic, 8)
#endif
        for (int rz = 0; rz < h; ++rz) {
            if (cancel.load(std::memory_order_relaxed)) continue;
            const int z = static_cast<int>(opt.minZ) + rz;
            uint64_t* dst = bmp.row(z);
            if (javaFast) {
                const uint64_t rowBase = javaRowBaseLocal(opt.worldSeed, static_cast<int32_t>(z));
                fastjava::scanRowJava(rowBase, static_cast<int32_t>(opt.minX), w, dst);
            } else {
                if (opt.version == Version::Java) {
                    SlimeEngine::fillSlimeRowJava(opt.worldSeed, z, static_cast<int>(opt.minX), w, row.data());
                    for (int x = 0; x < w; ++x) {
                        if (row[static_cast<size_t>(x)]) dst[x >> 6] |= 1ULL << (x & 63);
                    }
                } else {
                    SlimeEngine::fillSlimeRowBedrock(z, static_cast<int>(opt.minX), w, row.data());
                    const int wprRow = (w + 63) / 64;
#if defined(_M_X64) || defined(_M_IX86) || defined(__SSE2__)
                    const __m128i zero = _mm_setzero_si128();
                    for (int wi = 0; wi < wprRow; ++wi) {
                        const int base = wi * 64;
                        const int n = (std::min)(64, w - base);
                        uint64_t acc = 0;
                        for (int lane = 0; lane < 4; ++lane) {
                            const __m128i v = _mm_loadu_si128(
                                reinterpret_cast<const __m128i*>(row.data() + base + lane * 16));
                            const uint32_t nonSlime =
                                static_cast<uint32_t>(_mm_movemask_epi8(_mm_cmpeq_epi8(v, zero))) & 0xFFFFu;
                            acc |= static_cast<uint64_t>(~nonSlime & 0xFFFFu) << (lane * 16);
                        }
                        if (n < 64) acc &= ((1ULL << n) - 1ULL);
                        dst[wi] = acc;
                    }
#else
                    for (int x = 0; x < w; ++x) {
                        if (row[static_cast<size_t>(x)]) dst[x >> 6] |= 1ULL << (x & 63);
                    }
#endif
                }
            }
            const int64_t done = rowsDone.fetch_add(1, std::memory_order_relaxed) + 1;
            if (progress && (done % reportEvery == 0 || done == h)) {
                progress(static_cast<double>(done) / static_cast<double>(h), true, 0.0, 0.0);
            }
        }
    }
    const auto t1 = std::chrono::steady_clock::now();
    if (cancel.load(std::memory_order_relaxed)) {
        if (err) *err = "cancelled";
        return false;
    }
    if (progress) progress(1.0, true, 0.0, 0.0);
    stats.ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    stats.chunks = static_cast<int64_t>(w) * h;
    stats.backend = "cpu";
    stats.gpuUsed = false;
    out = std::move(bmp);
    return true;
}

bool GpuDeterminer::run(const slime::SearchOptions& opt, const std::atomic<bool>& cancel, Bitmap& out,
                        ScanStats& stats, std::string* err, const slime::ProgressFn& progress) const
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
    if (progress) progress(0.0, true, 0.0, 0.0);
    const auto t0 = std::chrono::steady_clock::now();
    std::vector<uint64_t> words;
    bool ok = m_gpu->scanRectWords(opt.version, opt.worldSeed, static_cast<int>(opt.minX),
                                   static_cast<int>(opt.minZ), w, h, words, err);
    if (ok && words.size() == static_cast<size_t>(wpr) * h) {
        // GPU 打包字内核按 64 区块整字输出，不会自动清掉最后一字的行尾越界位。
        // 新框架的 Bitmap 合同是“只含 [x0,x0+w)”，这里在边界统一裁剪。
        const int rem = w & 63;
        if (rem != 0) {
            const uint64_t mask = (uint64_t{ 1 } << rem) - 1ULL;
            for (int z = 0; z < h; ++z) {
                words[static_cast<size_t>(z) * wpr + (wpr - 1)] &= mask;
            }
        }
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
    if (progress) progress(1.0, true, 0.0, 0.0);
    stats.ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    stats.chunks = static_cast<int64_t>(w) * h;
    stats.backend = "gpu";
    stats.gpuUsed = true;
    out = std::move(bmp);
    return true;
}

} // namespace slime::next
