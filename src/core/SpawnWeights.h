// v3.5.2（任务 3）：群系 → 史莱姆刷怪效率权重
//
// 依据（Minecraft Wiki「Slime → Spawning」，2026-11 英文版核对；用户提供的折算表交叉验证）：
//   * 史莱姆区块：Y < 40 生成，**与亮度/天气无关**（Java 每组 4 只，基岩版单只）；
//   * 各群系的「生成权重表」给出该群系里史莱姆占全部敌对生物刷怪权重的比例：
//     Java 分数形如 100/515（19.42%）、100/615（16.26%）、25/315（7.94%）；
//     用户提供的“折算系数”= 最高档 100/515 ÷ 本群系分数：
//     河流 515/615、巨型针叶林 515/540、溶洞 515/610、蘑菇岛/深暗之地 = 0；
//     因此本表直接存**分子/分母**，避免百分比四舍五入带来的误差。
//   * 巨型针叶林丘陵（1.17 变体）与主群系同为 100/540；蘑菇岛岸 = 0；
//   * 沼泽/红树林沼泽在 Java 额外有一段**地表**生成（Y 51~69，亮度 ≤7，满月最多），
//     且它们被「算两次」（区块 + 地表）⇒ 记为附加加成；
//   * 硫磺洞穴（26.2+）：普通史莱姆 25/315（只在史莱姆区块、每群 1 只）；另有被动生物
//     “硫方怪”（100/315），只掉落吸收的方块+经验、不掉黏液球 ⇒ 按“凋落物不污染农场”的
//     口径采用 Wiki 折算 (25/315)/(100/515) ≈ 0.4087；用户要求**只对硫磺洞穴**做产量计算：
//     每群 1 只 vs 常规 4 只 ⇒ 等效折算再 ×1/4 ≈ 0.1022（其它群系不受影响）。
//   * 基岩版（英文 Wiki）：表内所有群系 spawn weight 恒 100、每群 1 只 ⇒ **名义上无群系差异**；
//     但**河流/冻河**在基岩版从未加入 monster 生成类别（中文 Wiki 明确写“仅基岩版除外”；
//     Mojira MCPE-161245 确认河流群系下史莱姆区块任何高度都不刷）⇒ 实际折算 0；
//     其它语言 Wiki（日/德/法/俄）未记载该例外（未找到相关资料）。
//     蘑菇岛 / 深暗之地不在表内（不刷敌对生物）⇒ 0。
//
// 权重归一化：以表内最高占比 100/515（19.42%）作为 1.0。
#pragma once

#include <cstdint>

namespace slime {

enum class Platform : uint8_t { Java = 0, Bedrock = 1 };

// 群系刷怪效率（0 = 完全不刷，1 = 表内最高档）
struct SpawnWeightInfo {
    double javaWeight = 1.0;
    double bedrockWeight = 1.0;
    bool surfaceBonus = false;   // 沼泽/红树林沼泽：Java 额外地表生成
    bool known = false;          // 是否在 Wiki 表内收录
    const char* note = "";       // 依据/说明
};

// 按群系 id（cubiomes BiomeID，见 BiomePalette.inc）取权重
SpawnWeightInfo spawnWeightFor(int biomeId);

// 按群系名（cubiomes 名）取权重（供测试与资源表核对）
SpawnWeightInfo spawnWeightForName(const char* biomeName);

// 某一平台的权重（基岩版恒 1.0，除「不刷」群系为 0）
double spawnWeight(int biomeId, Platform platform);

// 表内基准（最高占比 100/515 = 19.417…%），用于 UI 展示「相对最高档 %」
double spawnWeightBaseline();

} // namespace slime
