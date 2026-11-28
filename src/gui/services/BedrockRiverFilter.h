#pragma once

#include <QString>
#include <cstdint>
#include <vector>

#include "core/SlimeEngine.h"

namespace slime {

struct BedrockRiverFilterResult {
    bool executed = false;
    int removed = 0;
    QString error;
};

// Filters Bedrock results whose chunk is in river/frozen_river.
// The biome query is performed by an isolated helper executable.
class BedrockRiverFilter {
public:
    static BedrockRiverFilterResult filter(std::vector<MatchResult>& results,
                                           int64_t seed,
                                           const QString& version,
                                           const QString& helperPath);
};

} // namespace slime
