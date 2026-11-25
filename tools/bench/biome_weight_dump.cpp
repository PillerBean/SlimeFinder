// Dump a real Java biome weight grid for the height-region prototype.
#include "core/BiomeMap.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>

int main(int argc, char** argv)
{
    const int64_t seed = argc > 1 ? std::strtoll(argv[1], nullptr, 10) : 12345;
    const int x0 = argc > 2 ? std::atoi(argv[2]) : -192;
    const int z0 = argc > 3 ? std::atoi(argv[3]) : -192;
    const int size = argc > 4 ? std::atoi(argv[4]) : 384;
    const char* outPath = argc > 5 ? argv[5] : "biome_weights.tsv";

    slime::BiomeMap map;
    if (!map.init(slime::defaultJavaVersionId(), seed)) {
        std::fprintf(stderr, "BiomeMap init failed\n");
        return 1;
    }
    slime::BiomeWeightGrid grid;
    if (!grid.init(&map, slime::Platform::Java, 4, x0, z0, (size + 3) / 4, (size + 3) / 4)) {
        std::fprintf(stderr, "BiomeWeightGrid init failed\n");
        return 1;
    }
    std::ofstream out(outPath);
    if (!out) {
        std::fprintf(stderr, "cannot open %s\n", outPath);
        return 1;
    }
    out << "# seed=" << seed << " x0=" << x0 << " z0=" << z0
        << " size=" << size << " version=" << slime::BiomeMap::labelForVersionId(slime::defaultJavaVersionId())
        << "\n";
    for (int z = 0; z < size; ++z) {
        for (int x = 0; x < size; ++x) {
            if (x) out << '\t';
            out << grid.weightAt(x0 + x, z0 + z);
        }
        out << '\n';
    }
    std::fprintf(stderr, "wrote %s samples=%zu/%zu\n", outPath, grid.samples(), grid.cells());
    return 0;
}
