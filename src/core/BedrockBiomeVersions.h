#pragma once

#include <cstddef>

namespace slime {

// Bedrock biome-generation versions exposed by cubiomes-bedrock.
// label: UI group; queryLabel: exact version passed to the isolated helper.
struct BedrockBiomeVersionOption {
    const char* label;
    const char* queryLabel;
    int code;
};

inline constexpr BedrockBiomeVersionOption kBedrockBiomeVersions[] = {
    { "1.0", "1.0", 1000 },
    { "1.1", "1.1", 1100 },
    { "1.2", "1.2", 1200 },
    { "1.4-1.6", "1.4", 1400 },
    { "1.7-1.13", "1.7", 1700 },
    { "1.14", "1.14", 1140 },
    { "1.16-1.17.30", "1.16", 1160 },
    { "1.18", "1.18", 1180 },
    { "1.19", "1.19", 1190 },
    { "1.20-1.21", "1.20", 1200 },
    { "1.21.50", "1.21.50", 1215 },
    { "1.21.60", "1.21.60", 1216 },
    { "26.20", "26.20", 2620 },
    { "26.30", "26.30", 2630 },
    { "26.40-26.50", "26.40", 2640 },
    { "26.60", "26.60", 2660 },
};

inline constexpr std::size_t bedrockBiomeVersionCount() noexcept
{
    return sizeof(kBedrockBiomeVersions) / sizeof(kBedrockBiomeVersions[0]);
}

} // namespace slime
