#pragma once

#include <cstdint>

#include "core/SlimeEngine.h"

namespace slime {

// 纯数学命中预测：不采样、不读世界，只按 pSlime=0.1 的独立网格模型计算。
struct HitProbability {
    bool ok = false;
    int64_t candidatePositions = 0;
    double singleWindow = 0.0;       // 单个候选位置命中的概率
    double expectedHits = 0.0;       // 候选位置数 × 单点概率
    double probabilityAtLeastOne = 0.0;
    int green = 0;
    int white = 0;
    int gridSize = 0;
    double minDensity = 0.0;
    bool directionUnion = false;     // 图案四方向联合近似
};

// 精确二项尾概率 P(X >= k), X ~ Binomial(n, p)。
double binomialTail(int n, double p, int k);

// 按 SearchOptions 当前参数计算；不执行搜索、不使用采样。
HitProbability predictHits(const SearchOptions& opt);

} // namespace slime
