// v3.5.2（任务 3）：群系刷怪权重表 —— 数据来自 Minecraft Wiki「Slime → Spawning」（Java 列占比）
#include "core/SpawnWeights.h"

#include <cstring>

#include "core/BiomeMap.h" // 名称表（id ↔ name）

namespace slime {
namespace {

constexpr double kBaseline = 19.42; // 表内最高占比（%），= 竹林/森林/平原周边最高档

struct Row {
    const char* name;
    double chance;        // Java 群系内 slime 生成占比（%）
    bool surfaceBonus;    // 沼泽类：Java 额外地表生成
    const char* note;
    bool bedrockNoSpawn = false; // 基岩版该群系不生成史莱姆（中文 Wiki：河流仅基岩版除外）
};

// 数据来源：Minecraft Wiki「Slime」页面 Spawning 表格（Java 列 "Spawn chance"）
const Row kRows[] = {
    // 19.42%
    { "bamboo_jungle", 19.42, false, "" },
    { "mountains", 19.42, false, "Windswept Hills" },
    { "meadow", 19.42, false, "" },
    { "stony_peaks", 19.42, false, "" },
    { "grove", 19.42, false, "" },
    { "jungle", 19.42, false, "" },
    { "wooded_badlands_plateau", 19.42, false, "" },
    { "stone_shore", 19.42, false, "Stony Shore" },
    { "eroded_badlands", 19.42, false, "" },
    { "beach", 19.42, false, "" },
    { "birch_forest", 19.42, false, "" },
    { "modified_jungle", 19.42, false, "Sparse Jungle" },
    { "flower_forest", 19.42, false, "" },
    { "snowy_taiga", 19.42, false, "" },
    { "taiga", 19.42, false, "" },
    { "lush_caves", 19.42, false, "" },
    { "cherry_grove", 19.42, false, "" },
    { "snowy_beach", 19.42, false, "" },
    { "forest", 19.42, false, "" },
    { "badlands", 19.42, false, "" },
    { "gravelly_mountains", 19.42, false, "Windswept Gravelly Hills" },
    { "windswept_forest", 19.42, false, "" },
    { "desert", 19.42, false, "" },
    { "dark_forest", 19.42, false, "" },
    { "giant_spruce_taiga", 19.42, false, "Old Growth Spruce Taiga" },
    { "tall_birch_forest", 19.42, false, "Old Growth Birch Forest" },
    { "pale_garden", 19.42, false, "" },
    // 19.38%（+ 地表生成，Java 双段）
    { "swamp", 19.38, true, "额外地表生成（Y51~69，满月最多）" },
    { "mangrove_swamp", 19.38, true, "额外地表生成（Java 双段）" },
    // 19.23%
    { "cold_ocean", 19.23, false, "" },
    { "ocean", 19.23, false, "" },
    { "savanna_plateau", 19.23, false, "" },
    { "plains", 19.23, false, "" },
    { "deep_cold_ocean", 19.23, false, "" },
    { "lukewarm_ocean", 19.23, false, "" },
    { "sunflower_plains", 19.23, false, "" },
    { "shattered_savanna", 19.23, false, "Windswept Savanna" },
    { "deep_lukewarm_ocean", 19.23, false, "" },
    { "warm_ocean", 19.23, false, "" },
    { "deep_ocean", 19.23, false, "" },
    { "savanna", 19.23, false, "" },
    // 19.05%
    { "jagged_peaks", 19.05, false, "" },
    { "snowy_slopes", 19.05, false, "" },
    // 18.87%
    { "snowy_tundra", 18.87, false, "Snowy Plains" },
    // 18.52%
    { "frozen_peaks", 18.52, false, "" },
    { "giant_tree_taiga", 18.52, false, "Old Growth Pine Taiga" },
    { "ice_spikes", 18.52, false, "" },
    // 18.48%
    { "frozen_river", 18.48, false, "基岩版不生成（中文 Wiki：河流除外）", true },
    // 18.35%
    { "frozen_ocean", 18.35, false, "" },
    { "deep_frozen_ocean", 18.35, false, "" },
    // 16.39%
    { "dripstone_caves", 16.39, false, "" },
    // 16.26%
    { "river", 16.26, false, "基岩版不生成（中文 Wiki：河流除外）", true },
    // 不刷敌对生物 / 未收录
    { "mushroom_fields", 0.0, false, "不刷敌对生物（Wiki 表未收录）" },
    { "mushroom_field_shore", 0.0, false, "不刷敌对生物（Wiki 表未收录）" },
    { "deep_dark", 0.0, false, "Wiki 表未收录" },
    { "the_end", 0.0, false, "非主世界" },
    { "nether_wastes", 0.0, false, "非主世界" },
    { "soul_sand_valley", 0.0, false, "非主世界" },
    { "crimson_forest", 0.0, false, "非主世界" },
    { "warped_forest", 0.0, false, "非主世界" },
    { "basalt_deltas", 0.0, false, "非主世界" },
};
const int kRowCount = static_cast<int>(sizeof(kRows) / sizeof(kRows[0]));

const Row* findRow(const char* name)
{
    if (!name) return nullptr;
    for (int i = 0; i < kRowCount; ++i)
        if (std::strcmp(kRows[i].name, name) == 0) return &kRows[i];
    return nullptr;
}

} // namespace

double spawnWeightBaseline()
{
    return kBaseline;
}

SpawnWeightInfo spawnWeightForName(const char* biomeName)
{
    SpawnWeightInfo info;
    const Row* r = findRow(biomeName);
    if (!r) {
        // 未单独收录：按表内最高档处理（保守，不夸大也不惩罚）
        info.javaWeight = 1.0;
        info.bedrockWeight = 1.0;
        info.known = false;
        info.note = "未单独收录（按最高档）";
        return info;
    }
    info.known = true;
    info.javaWeight = r->chance / kBaseline;
    info.bedrockWeight = (r->chance <= 0.0 || r->bedrockNoSpawn) ? 0.0 : 1.0; // 基岩版权重恒 100，个别群系除外
    info.surfaceBonus = r->surfaceBonus;
    info.note = r->note;
    return info;
}

SpawnWeightInfo spawnWeightFor(int biomeId)
{
    if (biomeId < 0) return SpawnWeightInfo{}; // 无群系数据（基岩版/未初始化）
    return spawnWeightForName(BiomeMap::nameFor(biomeId));
}

double spawnWeight(int biomeId, Platform platform)
{
    if (biomeId < 0) return 1.0; // 无群系数据时不做加权
    const SpawnWeightInfo w = spawnWeightFor(biomeId);
    return (platform == Platform::Bedrock) ? w.bedrockWeight : w.javaWeight;
}

} // namespace slime
