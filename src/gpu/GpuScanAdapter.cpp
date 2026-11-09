// GpuScanAdapter.cpp - 引擎 GPU 扫描/密度匹配提供者实现
#include "gpu/GpuScanAdapter.h"
#include "gpu/GpuSession.h"

#include "core/SlimeEngine.h" // 仅接口声明（slime::GpuScan / slime::Version），无链接依赖

#include <algorithm>
#include <atomic>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace slimegpu {

namespace {

class GpuScanAdapter final : public slime::GpuScan {
public:
    explicit GpuScanAdapter(std::unique_ptr<GpuSession> session)
        : m_session(std::move(session))
    {
    }

    bool available() const noexcept override
    {
        return m_session && m_session->available();
    }

    bool scanRect(slime::Version version, int64_t worldSeed, int minX, int minZ, int w, int h,
                  std::vector<uint8_t>& out, std::string* err) const override
    {
        if (!m_session || !m_session->available()) {
            if (err) *err = "GPU 会话不可用";
            return false;
        }
        if (version == slime::Version::Java) {
            return m_session->scanJava(worldSeed, minX, minZ, w, h, out, err);
        }
        return m_session->scanBedrock(minX, minZ, w, h, out, err);
    }

    // v3.8.4：严格图案流式匹配（一个变体一次调用）
    bool matchPattern(const slime::PatternMatchParams& p, const std::atomic<bool>& cancel,
                      std::vector<slime::MatchResult>& out, std::string* err) const override
    {
        if (!m_session || !m_session->available()) return false;
        if (p.rows < 1 || p.rows > 32 || p.cols < 1 || p.cols > 32) return false;
        if (p.maxResults <= 0 || !p.enableMasks || !p.greenMasks) return false;
        const int w = static_cast<int>(p.axMax - p.axMin + p.cols);
        const int h = static_cast<int>(p.azMax - p.azMin + p.rows);
        if (w < p.cols || h < p.rows) return false;
        std::vector<slimegpu::GpuSession::StreamDensityHit> hits;
        if (!m_session->patternStream(p.worldSeed, p.version == slime::Version::Bedrock, p.rows, p.cols, w, h,
                                      static_cast<int>(p.axMin), static_cast<int>(p.axMax),
                                      static_cast<int>(p.azMin), static_cast<int>(p.azMax), p.enableMasks,
                                      p.greenMasks, static_cast<uint32_t>(p.maxResults), cancel, hits, err)) {
            return false;
        }
        if (cancel.load(std::memory_order_relaxed)) return false;
        out.clear();
        out.reserve(hits.size());
        for (const auto& h2 : hits) {
            slime::MatchResult m;
            m.minX = h2.x;
            m.maxX = h2.x + p.cols - 1;
            m.minZ = h2.z;
            m.maxZ = h2.z + p.rows - 1;
            out.push_back(m);
        }
        return true;
    }

    // v3.8.1（流程优化）：密度匹配走「全 GPU 流式内核」——判定 + 窗口统计都在 GPU 内完成，
    //   只回传命中，不回传位图、不在 CPU 上做窗口统计。
    //   仅在 gridSize ≤ 15（计数能放进 uchar）且未取消时尝试；任何异常都返回 false 让引擎回退 CPU。
    bool matchDensity(const slime::DensityMatchParams& p, const std::atomic<bool>& cancel,
                      std::vector<slime::MatchResult>& out, std::string* err) const override
    {
        if (!m_session || !m_session->available()) return false;
        if (p.gridSize < 1 || p.gridSize > 15) return false;
        if (p.maxResults <= 0) return false;
        const int w = p.axMax - p.axMin + p.gridSize;
        const int h = p.azMax - p.azMin + p.gridSize;
        if (w <= 0 || h <= 0) return false;
        const double area = static_cast<double>(p.gridSize) * p.gridSize;
        const int thr = static_cast<int>(std::ceil(p.minDensity * area - 1e-9));
        std::vector<slimegpu::GpuSession::StreamDensityHit> hits;
        uint32_t streamTotal = 0;
        if (!m_session->matchDensityStream(p.worldSeed, p.version == slime::Version::Bedrock, p.gridSize,
                                           std::max(1, thr), w, h, p.axMin, p.axMax,
                                           p.azMin, p.azMax, static_cast<uint32_t>(p.maxResults), p.startX,
                                           p.startZ, cancel, hits, err, &streamTotal, p.earlyStop, p.earlyStopTarget,
                                           static_cast<uint32_t>(std::min<int64_t>(p.keepCandidates, 4000000)))) {
            return false;
        }
        if (cancel.load(std::memory_order_relaxed)) return false;
        out.clear();
        // v3.8.10：命中可能上百万；先按「整数平方距离」保留最可能进榜的 keepCandidates 条，
        //   避免逐条构造 MatchResult（实测 350 万条构造占 56 ms）。
        if (p.outTotalFound) *p.outTotalFound = static_cast<int64_t>(streamTotal);
        std::vector<std::pair<int64_t, int>> order;   // (d2, 下标)
        const int64_t keep = p.keepCandidates;
        if (keep > 0 && static_cast<int64_t>(hits.size()) > keep) {
            order.reserve(hits.size());
            const int64_t half = (p.gridSize - 1);
            for (size_t i = 0; i < hits.size(); ++i) {
                const int64_t off2x = 2 * static_cast<int64_t>(hits[i].x) + half - 2 * p.startX;
                const int64_t off2z = 2 * static_cast<int64_t>(hits[i].z) + half - 2 * p.startZ;
                order.emplace_back(off2x * off2x + off2z * off2z, static_cast<int>(i));
            }
            std::nth_element(order.begin(), order.begin() + static_cast<size_t>(keep), order.end());
            order.resize(static_cast<size_t>(keep));
        }
        out.reserve(keep > 0 ? static_cast<size_t>(keep) : hits.size());
        const auto emit = [&](const slimegpu::GpuSession::StreamDensityHit& h2) {
            slime::MatchResult m;
            m.minX = h2.x;
            m.maxX = h2.x + p.gridSize - 1;
            m.minZ = h2.z;
            m.maxZ = h2.z + p.gridSize - 1;
            m.density = static_cast<float>(h2.count) / static_cast<float>(area);
            out.push_back(m);
        };
        if (!order.empty()) {
            for (const auto& pr : order) emit(hits[static_cast<size_t>(pr.second)]);
        } else {
            for (const auto& h2 : hits) emit(h2);
        }
        return true;
    }

    // v3.7.6：环形覆盖（GPU）；不支持时返回 false，调用方回退 CPU
    bool annulusCoverage(const slime::AnnulusCoverageRequest& req, std::vector<int>& out,
                         std::string* err) const override
    {
        if (!m_session || !m_session->available()) return false;
        return m_session->annulusCoverage(req.pref2d, req.w, req.h, req.originX, req.originZ, req.step,
                                          req.nx, req.nz, req.firstX, req.firstZ, req.innerRadius,
                                          req.outerRadius, out, err);
    }

    bool scanRectWords(slime::Version version, int64_t worldSeed, int minX, int minZ, int w, int h,
                       std::vector<uint64_t>& out, std::string* err) const override
    {
        if (!m_session || !m_session->available()) return false;
        if (version == slime::Version::Java) {
            return m_session->scanJavaWords(worldSeed, minX, minZ, w, h, out, err);
        }
        return m_session->scanBedrockWords(minX, minZ, w, h, out, err);
    }

    // v3.3.1：GPU 密度匹配内核开发已暂停——相关实现（含未完成调试代码）保留在
    // src/gpu/experimental/，作为 v3.4.0 起点。本适配器**不覆盖** matchDensity，
    // 因此基类默认实现生效（返回 false = 不支持），引擎自动走 CPU 匹配路径。
    // （引擎侧接口与调用点、单元测试 [17]/[18] 均已就绪，v3.4.0 只需在此处覆盖即可。）

    // v3.3.3（任务书 3.1）：向 GUI 暴露设备信息，用于「GPU 加速临界值」静态预判
    std::string deviceName() const override
    {
        if (!m_session) return {};
        const GpuDeviceInfo& d = m_session->device();
        if (d.name.empty()) return {};
        return d.vendor.empty() ? d.name : (d.name + "（" + d.vendor + "）");
    }

    uint64_t globalMemBytes() const noexcept override
    {
        return m_session ? m_session->device().globalMemBytes : 0;
    }

    uint32_t computeUnits() const noexcept override
    {
        return m_session ? static_cast<uint32_t>(m_session->device().maxComputeUnits) : 0;
    }

private:
    mutable std::unique_ptr<GpuSession> m_session;
};

} // namespace

std::unique_ptr<slime::GpuScan> createGpuScanAdapter(std::string* err)
{
    auto session = std::make_unique<GpuSession>();
    if (!session->init(err)) return nullptr;
    return std::unique_ptr<slime::GpuScan>(new GpuScanAdapter(std::move(session)));
}

} // namespace slimegpu
