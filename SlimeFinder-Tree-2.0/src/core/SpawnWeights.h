// v3.5.2（任务 3）：群系 → 史莱姆刷怪效率权重
//
// 依据（Minecraft Wiki「Slime → Spawning」，抓取于 2026-10）：
//   * 史莱姆区块：Y < 40 生成，**与亮度/天气无关**（Java 每组 4 只，基岩版单只）；
//   * 各群系的「生成权重表」给出该群系里史莱姆占全部敌对生物刷怪权重的比例
//     （Java：slime 权重恒为 100，但各群系其它生物的权重不同 ⇒ 占比不同）；
//     例如竹林 100/515 = 19.42%、平原 100/520 = 19.23%、河流 100/615 = 16.26%、
//     溶洞 100/610 = 16.39%、硫磺洞 25/315 = 7.94%；
//   * 蘑菇岛/蘑菇岛岸**不在表内**（不刷敌对生物）⇒ 权重 0；深暗域同样不在表内 ⇒ 权重 0；
//   * 沼泽/红树林沼泽在 Java 额外有一段**地表**生成（Y 51~69，亮度 ≤7，满月最多），
//     且它们被「算两次」（区块 + 地表）⇒ 记为附加加成；
//   * 基岩版各群系的 slime 权重都是 100（组队规模 1）⇒ 群系对效率没有区分度，统一 1.0；
//     但**中文 Wiki** 明确补充：史莱姆区块生成「除蘑菇岛、深暗之域和**河流（仅基岩版）**」——
//     因此基岩版河流/冻河权重为 0（英文 Wiki 的表格没有区分这一点，属两版本差异，已按中文版修正）。
//
// 权重归一化：以表内最高占比 19.42% 作为 1.0。
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

// 表内基准（最高占比），用于 UI 展示「相对最高档 %」
double spawnWeightBaseline();

} // namespace slime
