#include "cache/CachePack.h"

#include <cstdio>
#include <cstdint>
#include <filesystem>
#include <random>
#include <string>
#include <vector>

namespace {

bool roundTrip(int side, bool randomBits)
{
    const size_t chunks = static_cast<size_t>(side) * static_cast<size_t>(side);
    const size_t bytes = (chunks + 7) / 8;
    std::vector<uint8_t> bitmap(bytes, 0);
    if (randomBits) {
        std::mt19937 rng(12345);
        for (size_t i = 0; i < bytes; ++i) {
            // 约 10% 密度，接近真实 Bedrock 缓存
            uint8_t v = 0;
            for (int b = 0; b < 8; ++b) {
                if ((rng() % 100) < 10) v |= static_cast<uint8_t>(1u << b);
            }
            bitmap[i] = v;
        }
    } else {
        for (int z = 0; z < side; ++z) {
            for (int x = 0; x < side; ++x) {
                if ((x + z) % 10 == 0) {
                    const size_t bit = static_cast<size_t>(z) * side + x;
                    bitmap[bit / 8] |= static_cast<uint8_t>(1u << (bit % 8));
                }
            }
        }
    }
    const auto path = std::filesystem::temp_directory_path() / "slime_cache_pack_test.slmz";
    const slime::cache::PackRect rect{ 0, side - 1, 0, side - 1 };
    std::string err;
    if (!slime::cache::writePack(path, rect, bitmap.data(), bitmap.size(), &err)) {
        std::printf("writePack failed: %s\n", err.c_str());
        return false;
    }
    slime::cache::PackInfo info;
    std::vector<uint8_t> out;
    if (!slime::cache::readPack(path, info, out, &err)) {
        std::printf("readPack failed: %s\n", err.c_str());
        return false;
    }
    if (out != bitmap || info.rect.width() != side || info.rect.height() != side) {
        std::printf("roundtrip mismatch side=%d\n", side);
        return false;
    }
    std::printf("pack side=%d raw=%llu comp=%llu ratio=%.3f\n", side,
                static_cast<unsigned long long>(info.rawBytes),
                static_cast<unsigned long long>(info.compressedBytes),
                info.compressedBytes ? static_cast<double>(info.rawBytes) / info.compressedBytes : 0.0);
    std::error_code ec;
    std::filesystem::remove(path, ec);
    return true;
}

} // namespace

int main()
{
    bool ok = roundTrip(1024, false);
    ok = roundTrip(4096, true) && ok;
    std::printf("%s\n", ok ? "cache pack test OK" : "cache pack test FAILED");
    return ok ? 0 : 1;
}
