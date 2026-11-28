// v3.5.2（任务 3）：群系刷怪权重表 —— 数据来自 Minecraft Wiki「Slime → Spawning」。
// v3.15.x（用户表格核对，2026-11-28）：
//   * Java 权重改为**精确分数**表示 —— 折算系数 = (本群系史莱姆占比) / (最高占比)，
//     最高占比 = 100/515；用户提供的折算表（515/615 河流、515/540 巨型针叶林、515/610 溶洞、
//     蘑菇岛/深暗之地 = 0）与 Wiki 分数完全一致。
//   * 硫磺洞穴：英文 Wiki 怪物表里**普通史莱姆**仍是 25/315（只在史莱姆区块生成，每群 1 只），
//     另有一种被动生物“硫方怪 / Sulfur Cube”（100/315）——它只掉落吸收的方块+经验，
//     不掉黏液球、也不属于史莱姆。按用户规则“被处死不掉落（可用）物品 ⇒ 按 Wiki 折算”：
//     Java 折算 = (25/315)/(100/515) ≈ 0.4087（普通史莱姆的刷怪占比）；
//     用户要求“只对硫磺洞穴做产量计算”：该群系每群 1 只（常规 4 只）⇒ 再 ×1/4 ≈ **0.1022**。
//   * 巨型针叶林丘陵（1.17 变体）与主群系同分母 540。
//   * 基岩版：英文 Wiki 表内各群系 spawn weight 恒 100/每群 1（名义上无差异），但**河流**在
//     基岩版从未加入 monster 生成类别（中文 Wiki 明确写“河流[仅基岩版]除外”；Mojira
//     MCPE-161245 确认：河流群系下史莱姆区块任何高度都不刷史莱姆）⇒ 实际折算 0；
//     旧代码的 0 是对的（英文 Wiki 表格只列数据权重、不含该 bug）。
//     日/德/法/俄 Wikipedia 的史莱姆页面未记载该基岩版河流例外（未找到相关资料）。
//     蘑菇岛 / 深暗之地不刷敌对生物 ⇒ 0。
#include "core/SpawnWeights.h"

#include <cstring>

#include "core/BiomeMap.h" // 名称表（id ↔ name）

namespace slime {
namespace {

// 分母基准：最高档 100/515（竹林/森林等），即用户表格里的 515/515 = 1.0。
constexpr double kBaselineNum = 100.0;
constexpr double kBaselineDen = 515.0;

struct Row {
    const char* name;
    int num;              // 该群系史莱姆生成权重（Wiki 分数分子，通常 100）
    int den;              // 该群系全部敌对生物生成权重合计（Wiki 分数分母）
    bool surfaceBonus;    // 沼泽类：Java 额外地表生成
    const char* note;
    bool javaNoSpawn;     // Java 该群系不生成史莱姆（按用户表格 / Wiki 未收录）
    bool bedrockNoSpawn;  // 基岩版不生成（不在英文 Wiki 表内 / 河流怪物类别缺失 bug）
};

// 数据来源：Minecraft Wiki「Slime」Spawning 表格（Java 列 Spawn weight / Bedrock 列 100）。
const Row kRows[] = {
    // ---- 100/515（19.42%，= 1.0）----
    { "bamboo_jungle", 100, 515, false, "", false, false },
    { "mountains", 100, 515, false, "Windswept Hills", false, false },
    { "meadow", 100, 515, false, "", false, false },
    { "stony_peaks", 100, 515, false, "", false, false },
    { "grove", 100, 515, false, "", false, false },
    { "jungle", 100, 515, false, "", false, false },
    { "wooded_badlands_plateau", 100, 515, false, "", false, false },
    { "stone_shore", 100, 515, false, "Stony Shore", false, false },
    { "eroded_badlands", 100, 515, false, "", false, false },
    { "beach", 100, 515, false, "", false, false },
    { "birch_forest", 100, 515, false, "", false, false },
    { "modified_jungle", 100, 515, false, "Sparse Jungle", false, false },
    { "flower_forest", 100, 515, false, "", false, false },
    { "snowy_taiga", 100, 515, false, "", false, false },
    { "snowy_taiga_hills", 100, 515, false, "1.17 丘陵变体", false, false },
    { "taiga", 100, 515, false, "", false, false },
    { "taiga_hills", 100, 515, false, "1.17 丘陵变体", false, false },
    { "lush_caves", 100, 515, false, "", false, false },
    { "cherry_grove", 100, 515, false, "", false, false },
    { "snowy_beach", 100, 515, false, "", false, false },
    { "forest", 100, 515, false, "", false, false },
    { "badlands", 100, 515, false, "", false, false },
    { "gravelly_mountains", 100, 515, false, "Windswept Gravelly Hills", false, false },
    { "windswept_forest", 100, 515, false, "", false, false },
    { "desert", 100, 515, false, "", false, false },
    { "dark_forest", 100, 515, false, "", false, false },
    { "giant_spruce_taiga", 100, 515, false, "Old Growth Spruce Taiga", false, false },
    { "giant_spruce_taiga_hills", 100, 515, false, "1.17 丘陵变体", false, false },
    { "tall_birch_forest", 100, 515, false, "Old Growth Birch Forest", false, false },
    { "pale_garden", 100, 515, false, "", false, false },
    // ---- 100/516（19.38%，+ 地表生成，Java 双段）----
    { "swamp", 100, 516, true, "额外地表生成（Y51~69，满月最多）", false, false },
    { "mangrove_swamp", 100, 516, true, "额外地表生成（Java 双段）", false, false },
    // ---- 100/520（19.23%）----
    { "cold_ocean", 100, 520, false, "", false, false },
    { "ocean", 100, 520, false, "", false, false },
    { "savanna_plateau", 100, 520, false, "", false, false },
    { "plains", 100, 520, false, "", false, false },
    { "deep_cold_ocean", 100, 520, false, "", false, false },
    { "lukewarm_ocean", 100, 520, false, "", false, false },
    { "sunflower_plains", 100, 520, false, "", false, false },
    { "shattered_savanna", 100, 520, false, "Windswept Savanna", false, false },
    { "deep_lukewarm_ocean", 100, 520, false, "", false, false },
    { "warm_ocean", 100, 520, false, "", false, false },
    { "deep_ocean", 100, 520, false, "", false, false },
    { "savanna", 100, 520, false, "", false, false },
    // ---- 100/525（19.05%）----
    { "jagged_peaks", 100, 525, false, "", false, false },
    { "snowy_slopes", 100, 525, false, "", false, false },
    // ---- 100/530（18.87%）----
    { "snowy_tundra", 100, 530, false, "Snowy Plains", false, false },
    // ---- 100/540（18.52%）----
    { "frozen_peaks", 100, 540, false, "", false, false },
    { "giant_tree_taiga", 100, 540, false, "Old Growth Pine Taiga（巨型针叶林，含 1.17 丘陵）", false, false },
    { "giant_tree_taiga_hills", 100, 540, false, "1.17 丘陵变体（用户表格：含丘陵）", false, false },
    { "ice_spikes", 100, 540, false, "", false, false },
    // ---- 100/541（18.48%）----
    // 基岩版：河流/冻河从未加入 monster 生成类别（MCPE-161245；中文 Wiki 明确“仅基岩版除外”）
    { "frozen_river", 100, 541, false, "基岩版河流无 monster 类别（MCPE-161245）⇒实际不刷", false, true },
    // ---- 100/545（18.35%）----
    { "frozen_ocean", 100, 545, false, "", false, false },
    { "deep_frozen_ocean", 100, 545, false, "", false, false },
    // ---- 100/610（16.39%）----
    { "dripstone_caves", 100, 610, false, "溶洞（1.18+）", false, false },
    // ---- 100/615（16.26%）----
    { "river", 100, 615, false, "基岩版河流无 monster 类别（MCPE-161245）⇒实际不刷", false, true },
    // ---- 25/315（7.94%，26.2+ 硫磺洞穴；普通史莱姆只在史莱姆区块生成、每群 1 只）----
    // 硫方怪（Sulfur Cube，100/315）是被动生物：只掉落吸收的方块+经验，不掉黏液球。
    // 按用户规则（凋落物不污染农场）→ 按 Wiki 折算；并且用户要求“只对硫磺洞穴做产量计算”：
    // 每群 1 只 vs 常规 4 只 ⇒ 代码里再 ×1/4（见 spawnWeightForName），等效 ≈0.1022。
    { "sulfur_caves", 25, 315, false, "26.2+：普通史莱姆 25/315（每群 1）；硫方怪不掉黏液球", false, false },
    // ---- 不刷敌对生物 / 非主世界 ----
    { "mushroom_fields", 0, 1, false, "不刷敌对生物（Wiki 表未收录）", true, true },
    { "mushroom_field_shore", 0, 1, false, "不刷敌对生物（Wiki 表未收录）", true, true },
    { "deep_dark", 0, 1, false, "Wiki 表未收录（不刷怪）", true, true },
    { "the_end", 0, 1, false, "非主世界", true, true },
    { "nether_wastes", 0, 1, false, "非主世界", true, true },
    { "soul_sand_valley", 0, 1, false, "非主世界", true, true },
    { "crimson_forest", 0, 1, false, "非主世界", true, true },
    { "warped_forest", 0, 1, false, "非主世界", true, true },
    { "basalt_deltas", 0, 1, false, "非主世界", true, true },
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
    // UI 展示用的是“百分比基准”：最高档 100/515 = 19.417…%，与旧值 19.42 一致。
    return kBaselineNum / kBaselineDen * 100.0;
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
    // 折算系数 = (num/den) / (100/515) —— 与用户表格的 515/615、515/540、515/610 完全一致。
    info.javaWeight =
        r->javaNoSpawn ? 0.0 : ((static_cast<double>(r->num) / r->den) / (kBaselineNum / kBaselineDen));
    // v3.15.x（用户要求：只对硫磺洞穴做产量折算）：硫磺洞穴的普通史莱姆每群 1 只，
    //   其它群系为 4 只；农场产量与该倍数成正比 ⇒ 再乘 1/4，得到“等效折算”。
    if (!r->javaNoSpawn && std::strcmp(r->name, "sulfur_caves") == 0) {
        info.javaWeight *= 0.25;
    }
    // 英文 Wiki 基岩版列：表内群系 spawn weight 恒 100、每群 1 只 ⇒ 无群系差异；
    // 不在表内（蘑菇岛/深暗之地等）⇒ 0。
    info.bedrockWeight = r->bedrockNoSpawn ? 0.0 : 1.0;
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
