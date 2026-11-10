// v3.5.2（任务 2/3 的公共底座）：群系地图 —— 用 cubiomes 从种子生成群系，供彩色预览与权重算法使用
//
// 说明：
//  * cubiomes（MIT）支持 Java 版 1.0 ~ 1.21 的群系生成；**基岩版暂不支持群系着色**（地图只画史莱姆区块）。
//  * 生成器是有状态的，本类内部用互斥锁保护；查询是廉价的（1.18+ 走 multi-noise 采样）。
#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include "core/SpawnWeights.h"

namespace slime {

// 可选 Java 版本（与 cubiomes 的 MCVersion 数值对应，只在 BiomeMap.cpp 内映射）
struct JavaVersionOption {
    int id;          // 我们自己的编号（UI 用）
    const char* label; // 例如 "1.21.3"
    int cubiomesVersion; // cubiomes MCVersion 数值（内部）
};

// 可用的 Java 版本列表（从新到旧）
const std::vector<JavaVersionOption>& javaVersionOptions();
int defaultJavaVersionId();

class BiomeMap {
public:
    BiomeMap() = default;
    ~BiomeMap();
    BiomeMap(const BiomeMap&) = delete;
    BiomeMap& operator=(const BiomeMap&) = delete;

    // versionId 来自 javaVersionOptions()；失败（不支持的版本）返回 false
    bool init(int versionId, int64_t seed);
    bool valid() const noexcept { return m_valid; }
    void reset();

    // 方块坐标处的群系 id（scale: 1 / 4 / 16 / 64，越小越精细；1 最慢）
    int biomeAt(int blockX, int blockZ, int scale = 4) const;

    // 批量采样（地图渲染用）：以 (blockX, blockZ) 为左上角，取 wCells×hCells 个 scale 网格的群系 id
    bool genArea(int scale, int blockX, int blockZ, int wCells, int hCells, std::vector<int>& out) const;

    // 群系颜色 / 名称（未知 id 返回中性灰与 "unknown"）
    static uint32_t colorFor(int biomeId);
    static const char* nameFor(int biomeId);

    // 版本标签
    static const char* labelForVersionId(int versionId);

private:
    struct Impl;
    Impl* m_impl = nullptr;
    mutable std::mutex m_mu;
    bool m_valid = false;
    int m_versionId = -1;
    int64_t m_seed = 0;
};

// v3.5.13：按需采样的「群系刷怪权重网格」（严格 1 格统计用）
//
// 背景：旧实现给每个候选点调用 genArea(4, ...) 把 69x69 = 4761 个 1:4 格子整片采样，
// 但严格统计只查询「落在史莱姆区块里」的方块 —— 大约只碰到 600~900 个格子，
// 也就是 80% 以上的采样是白做的（1:4 每格约 3~4 µs，整片约 18ms/候选）。
// 这里改成「用到才采样 + 逐格缓存」：采样坐标与 genArea 完全一致
// （同一 scale 同一坐标 ⇒ 同一群系 id），因此统计结果与旧实现逐位相同。
//
// 线程模型：一个实例只给一个线程用（GUI 里每个候选各建一个），内部不加锁。
class BiomeWeightGrid {
public:
    // blockX/blockZ 为窗口左上角的方块坐标（通常已按 scale 对齐）；wCells×hCells 为格子数
    bool init(const BiomeMap* map, Platform platform, int scale, int blockX, int blockZ, int wCells, int hCells);
    bool valid() const noexcept { return m_ok; }
    int cellsX() const noexcept { return m_gw; }
    int cellsZ() const noexcept { return m_gh; }

    // 方块坐标处的权重（按 scale 归属格子）；窗口外返回 1.0（与旧实现的兜底一致）
    double weightAt(int blockX, int blockZ) const;

    // 诊断：已实际采样的格子数 / 窗口总格子数
    size_t samples() const noexcept { return m_samples; }
    size_t cells() const noexcept { return static_cast<size_t>(m_gw) * static_cast<size_t>(m_gh); }

private:
    const BiomeMap* m_map = nullptr;
    Platform m_platform = Platform::Java;
    int m_scale = 4;
    int m_cx0 = 0, m_cz0 = 0, m_gw = 0, m_gh = 0;
    bool m_ok = false;
    mutable std::vector<float> m_w;   // -1 = 尚未采样（权重本身可以为 0，不能拿 0 当哨兵）
    mutable size_t m_samples = 0;
};

} // namespace slime
