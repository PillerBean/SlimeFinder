// v3.5.2（任务 2/3 的公共底座）：群系地图 —— 实现（cubiomes，MIT）
#include "core/BiomeMap.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

extern "C" {
#include "generator.h"
}

namespace slime {
namespace {

struct BiomeColorEntry {
    int id;
    const char* name;
    uint32_t rgb;
};

#include "core/BiomePalette.inc"

// 版本表（cubiomes 枚举值只在 .cpp 内出现，避免把 C 头暴露给 GUI）
struct VersionRow {
    int id;
    const char* label;
    int mc;
};

const VersionRow kVersions[] = {
    // v3.5.22（用户要求）：补全历史版本 —— 部分版本改过世界/群系算法，需要能回看
    { 0,  "1.21.3",  MC_1_21_3 },
    { 1,  "1.21.1",  MC_1_21_1 },
    { 2,  "1.20.6",  MC_1_20_6 },
    { 3,  "1.19.4",  MC_1_19_4 },
    { 4,  "1.19.2",  MC_1_19_2 },
    { 5,  "1.18.2",  MC_1_18_2 },
    { 6,  "1.17.1",  MC_1_17_1 },
    { 7,  "1.16.5",  MC_1_16_5 },
    { 8,  "1.16.1",  MC_1_16_1 },
    { 9,  "1.15.2",  MC_1_15_2 },
    { 10, "1.14.4",  MC_1_14_4 },
    { 11, "1.13.2",  MC_1_13_2 },
    { 12, "1.12.2",  MC_1_12_2 },
    { 13, "1.11.2",  MC_1_11_2 },
    { 14, "1.10.2",  MC_1_10_2 },
    { 15, "1.9.4",   MC_1_9_4 },
    { 16, "1.8.9",   MC_1_8_9 },
    { 17, "1.7.10",  MC_1_7_10 },
    { 18, "1.6.4",   MC_1_6_4 },
    { 19, "1.5.2",   MC_1_5_2 },
    { 20, "1.4.7",   MC_1_4_7 },
    { 21, "1.3.2",   MC_1_3_2 },
    { 22, "1.2.5",   MC_1_2_5 },
    { 23, "1.1",     MC_1_1_0 },
    { 24, "1.0",     MC_1_0_0 },
    { 25, "Beta 1.8", MC_B1_8 },
    { 26, "Beta 1.7", MC_B1_7 },
};const int kVersionCount = static_cast<int>(sizeof(kVersions) / sizeof(kVersions[0]));

const VersionRow* rowForId(int id)
{
    for (int i = 0; i < kVersionCount; ++i)
        if (kVersions[i].id == id) return &kVersions[i];
    return nullptr;
}

constexpr int kSurfaceY = 63; // 地表附近的群系（cubiomes 对 1.18+ 用 y 选择垂直层）

// 方块坐标 → 采样格坐标（scale>1 时 cubiomes 期望 biome/格坐标，y 也是 1:4）
inline int cellCoord(int block, int scale)
{
    if (scale <= 1) return block;
    return block >= 0 ? block / scale : -((-block + scale - 1) / scale);
}
inline int cellY(int scale) { return scale <= 1 ? kSurfaceY : (kSurfaceY >> 2); }


} // namespace

struct BiomeMap::Impl {
    Generator g{};
    bool ready = false;
};

const std::vector<JavaVersionOption>& javaVersionOptions()
{
    static const std::vector<JavaVersionOption> opts = [] {
        std::vector<JavaVersionOption> v;
        for (int i = 0; i < kVersionCount; ++i)
            v.push_back(JavaVersionOption{ kVersions[i].id, kVersions[i].label, kVersions[i].mc });
        return v;
    }();
    return opts;
}

int defaultJavaVersionId()
{
    return kVersions[0].id; // 1.21.3
}

BiomeMap::~BiomeMap()
{
    reset();
}

void BiomeMap::reset()
{
    std::lock_guard<std::mutex> lk(m_mu);
    delete m_impl;
    m_impl = nullptr;
    m_valid = false;
}

bool BiomeMap::init(int versionId, int64_t seed)
{
    reset();
    const VersionRow* row = rowForId(versionId);
    if (!row) {
        std::fprintf(stderr, "[群系] 不支持的版本 id=%d\n", versionId);
        return false;
    }
    std::lock_guard<std::mutex> lk(m_mu);
    m_impl = new Impl();
    setupGenerator(&m_impl->g, row->mc, 0);
    applySeed(&m_impl->g, DIM_OVERWORLD, static_cast<uint64_t>(seed));
    m_impl->ready = true;
    m_valid = true;
    m_versionId = versionId;
    m_seed = seed;
    std::fprintf(stderr, "[群系] 初始化：Java %s（cubiomes mc=%d）种子=%lld\n", row->label, row->mc,
                 static_cast<long long>(seed));
    return true;
}

int BiomeMap::biomeAt(int blockX, int blockZ, int scale) const
{
    if (!m_valid || !m_impl || !m_impl->ready) return -1;
    const int s = (scale == 1 || scale == 4 || scale == 16 || scale == 64) ? scale : 4;
    std::lock_guard<std::mutex> lk(m_mu);
    const int id = getBiomeAt(&m_impl->g, s, cellCoord(blockX, s), cellY(s), cellCoord(blockZ, s));
    return id;
}

bool BiomeMap::genArea(int scale, int blockX, int blockZ, int wCells, int hCells, std::vector<int>& out) const
{
    out.clear();
    if (wCells <= 0 || hCells <= 0) return false;
    const int s = (scale == 1 || scale == 4 || scale == 16 || scale == 64) ? scale : 4;
    Range r;
    r.scale = s;
    r.x = cellCoord(blockX, s);
    r.z = cellCoord(blockZ, s);
    r.sx = wCells;
    r.sz = hCells;
    r.y = cellY(s);
    r.sy = 1;
    out.assign(static_cast<size_t>(wCells) * hCells, 0);

    // v3.5.12：1.18+ 的群系直接由噪声采样得到，Generator 里没有堆分配，
    // 因此可以每线程各建一个生成器、按行带并行（1:4 的 13 万格从 ~400ms 降到 ~70ms）。
    // 1.17 及更早是图层链，图层自带堆缓冲，不能跨线程复用同一个生成器，保持串行。
    int mc = 0;
    int64_t seed = 0;
    {
        std::lock_guard<std::mutex> lk(m_mu);
        if (!m_valid || !m_impl || !m_impl->ready) return false;
        mc = m_impl->g.mc;
        seed = m_seed;
    }
    int bands = 1;
    const long long cells = static_cast<long long>(wCells) * hCells;
    if (mc >= MC_1_18 && cells >= 1024 && hCells >= 32) bands = (std::min)(16, (std::max)(2, hCells / 12));
    if (bands <= 1) {
        std::lock_guard<std::mutex> lk(m_mu);
        if (!m_valid || !m_impl || !m_impl->ready) return false;
        int* cache = allocCache(&m_impl->g, r);
        if (!cache) return false;
        const int err = genBiomes(&m_impl->g, cache, r);
        if (err != 0) {
            free(cache);
            return false;
        }
        std::copy(cache, cache + static_cast<size_t>(wCells) * hCells, out.begin());
        free(cache);
        return true;
    }
    bool ok = true;
#pragma omp parallel for schedule(static) num_threads(bands)
    for (int b = 0; b < bands; ++b) {
        const int z0 = static_cast<int>(static_cast<long long>(hCells) * b / bands);
        const int z1 = static_cast<int>(static_cast<long long>(hCells) * (b + 1) / bands);
        if (z1 <= z0) continue;
        Generator local;
        setupGenerator(&local, mc, 0);
        applySeed(&local, DIM_OVERWORLD, static_cast<uint64_t>(seed));
        Range br = r;
        br.z = r.z + z0;
        br.sz = z1 - z0;
        int* cache = allocCache(&local, br);
        if (!cache) {
            ok = false;
            continue;
        }
        if (genBiomes(&local, cache, br) != 0) {
            free(cache);
            ok = false;
            continue;
        }
        for (int zz = 0; zz < br.sz; ++zz)
            std::copy(cache + static_cast<size_t>(zz) * wCells, cache + static_cast<size_t>(zz + 1) * wCells,
                      out.begin() + static_cast<size_t>(z0 + zz) * wCells);
        free(cache);
    }
    return ok;
}

uint32_t BiomeMap::colorFor(int biomeId)
{
    for (int i = 0; i < kBiomePaletteSize; ++i)
        if (kBiomePalette[i].id == biomeId) return kBiomePalette[i].rgb;
    // v3.6.7：未收录的 ID 用「按 id 散列」的稳定配色 —— 不同群系颜色不同，
    //   不再全部糊成同一种灰（用户反馈：部分群系显示 unknown、颜色也分不出）。
    if (biomeId < 0) return 0x2A2A2A;   // 未知/越界
    const uint32_t h = static_cast<uint32_t>(biomeId) * 2654435761u;
    const int r = 70 + static_cast<int>((h >> 8) % 150);
    const int g = 90 + static_cast<int>((h >> 16) % 130);
    const int b = 70 + static_cast<int>((h >> 24) % 150);
    return (static_cast<uint32_t>(r) << 16) | (static_cast<uint32_t>(g) << 8) | static_cast<uint32_t>(b);
}

const char* BiomeMap::nameFor(int biomeId)
{
    for (int i = 0; i < kBiomePaletteSize; ++i)
        if (kBiomePalette[i].id == biomeId) return kBiomePalette[i].name;
    return "unknown";
}

const char* BiomeMap::labelForVersionId(int versionId)
{
    const VersionRow* row = rowForId(versionId);
    return row ? row->label : "?";
}

// ---------------------------------------------------------------------------
// v3.5.13：按需采样的群系权重网格（严格 1 格统计用）
// ---------------------------------------------------------------------------

bool BiomeWeightGrid::init(const BiomeMap* map, Platform platform, int scale, int blockX, int blockZ,
                           int wCells, int hCells)
{
    m_ok = false;
    m_map = nullptr;
    m_gw = m_gh = 0;
    m_samples = 0;
    m_w.clear();
    if (!map || !map->valid() || wCells <= 0 || hCells <= 0) return false;
    m_scale = (scale == 1 || scale == 4 || scale == 16 || scale == 64) ? scale : 4;
    m_platform = platform;
    m_cx0 = cellCoord(blockX, m_scale);
    m_cz0 = cellCoord(blockZ, m_scale);
    m_gw = wCells;
    m_gh = hCells;
    m_w.assign(static_cast<size_t>(wCells) * hCells, -1.0f);
    m_map = map;
    m_ok = true;
    return true;
}

double BiomeWeightGrid::weightAt(int blockX, int blockZ) const
{
    if (!m_ok || !m_map) return 1.0;
    const int ix = cellCoord(blockX, m_scale) - m_cx0;
    const int iz = cellCoord(blockZ, m_scale) - m_cz0;
    if (ix < 0 || iz < 0 || ix >= m_gw || iz >= m_gh) return 1.0;
    float& slot = m_w[static_cast<size_t>(iz) * m_gw + ix];
    if (slot < 0.0f) {
        // 与 genArea 完全同一坐标：格子 (cx,cz) 的采样点就是 (cx*scale, cz*scale)
        const int id = m_map->biomeAt((m_cx0 + ix) * m_scale, (m_cz0 + iz) * m_scale, m_scale);
        slot = static_cast<float>(slime::spawnWeight(id, m_platform));
        ++m_samples;
    }
    return static_cast<double>(slot);
}

} // namespace slime
