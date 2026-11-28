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
    { "1.16", 1160 }, { "1.17", 1170 }, { "1.17.30", 1173 },
    { "1.18", 1180 }, { "1.19", 1190 }, { "1.20", 1200 },
    { "1.21", 1210 }, { "1.21.50", 1215 }, { "1.21.60", 1216 },
    { "26.20", 2620 }, { "26.30", 2630 },
};

inline constexpr std::size_t bedrockBiomeVersionCount() noexcept
{
    return sizeof(kBedrockBiomeVersions) / sizeof(kBedrockBiomeVersions[0]);
}

} // namespace slime

