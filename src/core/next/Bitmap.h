// 新计算框架的最小原型（分支 refactor-compute-core）
// 只实现 Bitmap + Determiner；不接入 GUI/旧引擎。
#pragma once

#include "core/SlimeEngine.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace slime::next {

class Bitmap {
public:
    bool init(int x0, int z0, int w, int h)
    {
        if (w <= 0 || h <= 0) return false;
        m_x0 = x0;
        m_z0 = z0;
        m_w = w;
        m_h = h;
        m_wpr = (w + 63) / 64;
        m_words.assign(static_cast<size_t>(m_wpr) * static_cast<size_t>(h), 0);
        return true;
    }

    int x0() const noexcept { return m_x0; }
    int z0() const noexcept { return m_z0; }
    int w() const noexcept { return m_w; }
    int h() const noexcept { return m_h; }
    int wordsPerRow() const noexcept { return m_wpr; }
    size_t wordCount() const noexcept { return m_words.size(); }

    uint64_t* row(int z) noexcept
    {
        return m_words.data() + static_cast<size_t>((z - m_z0) * m_wpr);
    }
    const uint64_t* row(int z) const noexcept
    {
        return m_words.data() + static_cast<size_t>((z - m_z0) * m_wpr);
    }
    std::vector<uint64_t>& words() noexcept { return m_words; }
    const std::vector<uint64_t>& words() const noexcept { return m_words; }

    void setSlime(int x, int z) noexcept
    {
        const int rx = x - m_x0;
        const int rz = z - m_z0;
        m_words[static_cast<size_t>(rz * m_wpr + (rx >> 6))] |= 1ULL << (rx & 63);
    }
    bool isSlime(int x, int z) const noexcept
    {
        const int rx = x - m_x0;
        const int rz = z - m_z0;
        return ((m_words[static_cast<size_t>(rz * m_wpr + (rx >> 6))] >> (rx & 63)) & 1ULL) != 0;
    }

private:
    int m_x0 = 0, m_z0 = 0, m_w = 0, m_h = 0, m_wpr = 0;
    std::vector<uint64_t> m_words;
};

struct ScanStats {
    double ms = 0.0;
    int64_t chunks = 0;
    bool gpuUsed = false;
    std::string backend;
};

class Determiner {
public:
    virtual ~Determiner() = default;
    virtual const char* name() const noexcept = 0;
    virtual bool run(const slime::SearchOptions& opt, const std::atomic<bool>& cancel, Bitmap& out,
                     ScanStats& stats, std::string* err) const = 0;
};

class CpuDeterminer final : public Determiner {
public:
    const char* name() const noexcept override { return "cpu"; }
    bool run(const slime::SearchOptions& opt, const std::atomic<bool>& cancel, Bitmap& out,
             ScanStats& stats, std::string* err) const override;
};

class GpuDeterminer final : public Determiner {
public:
    explicit GpuDeterminer(const slime::GpuScan* gpu) : m_gpu(gpu) {}
    const char* name() const noexcept override { return "gpu"; }
    bool run(const slime::SearchOptions& opt, const std::atomic<bool>& cancel, Bitmap& out,
             ScanStats& stats, std::string* err) const override;

private:
    const slime::GpuScan* m_gpu = nullptr;
};

} // namespace slime::next
