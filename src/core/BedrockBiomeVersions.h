#pragma once

#include <cstddef>

namespace slime {

// Bedrock biome-generation versions exposed by cubiomes-bedrock.
// The numeric value is the tool's CLI protocol, not a cubiomes enum constant.
struct BedrockBiomeVersionOption {
    const char* label;
    int code;
};

inline constexpr BedrockBiomeVersionOption kBedrockBiomeVersions[] = {
    { "1.0", 1000 }, { "1.1", 1100 }, { "1.2", 1200 },
    { "1.4", 1400 }, { "1.5", 1500 }, { "1.6", 1600 }, { "1.7", 1700 },
    { "1.8", 1800 }, { "1.9", 1900 }, { "1.10", 11000 },
    { "1.11", 11100 }, { "1.12", 11200 }, { "1.13", 11300 }, { "1.14", 11400 },
    { "1.16", 1160 }, { "1.17", 1170 }, { "1.17.30", 1173 },
    { "1.18", 1180 }, { "1.19", 1190 }, { "1.20", 1200 },
    { "1.21", 1210 }, { "1.21.50", 1215 }, { "1.21.60", 1216 },
    { "26.20", 2620 }, { "26.30", 2630 },
    { "26.40", 2640 }, { "26.50", 2650 }, { "26.60", 2660 },
};

inline constexpr std::size_t bedrockBiomeVersionCount() noexcept
{
    return sizeof(kBedrockBiomeVersions) / sizeof(kBedrockBiomeVersions[0]);
}

} // namespace slime




