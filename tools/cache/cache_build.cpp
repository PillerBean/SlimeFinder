// v3.12.3：官方缓存样例构建工具
// 直接按 Bedrock 史莱姆判定生成 1024×1024 对齐的 v3 pack，不依赖 GUI。
// 用法示例：
//   slime_cache_build.exe --out cache-official --min-x -2048 --max-x 2048 --min-z -2048 --max-z 2048
#include "cache/CachePack.h"
#include "cache/CacheStore.h"
#include "core/SlimeEngine.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {
int64_t floorTile(int64_t v)
{
    if (v >= 0) return (v / 1024) * 1024;
    return -(((-v) + 1023) / 1024) * 1024;
}

int64_t ceilTile(int64_t v)
{
    if (v <= 0) return (v / 1024) * 1024;
    return ((v + 1023) / 1024) * 1024;
}
} // namespace

int main(int argc, char** argv)
{
    fs::path out = "cache-official";
    int64_t minX = -2048, maxX = 2048, minZ = -2048, maxZ = 2048;
    int maxPacks = 16;
    bool verify = false;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&](int64_t& v) {
            if (i + 1 < argc) v = std::strtoll(argv[++i], nullptr, 10);
        };
        if (a == "--out" && i + 1 < argc) out = argv[++i];
        else if (a == "--min-x") next(minX);
        else if (a == "--max-x") next(maxX);
        else if (a == "--min-z") next(minZ);
        else if (a == "--max-z") next(maxZ);
        else if (a == "--verify") verify = true;
        else if (a == "--max-packs") {
            int64_t n = 0;
            next(n);
            maxPacks = static_cast<int>(n);
        }
    }
    if (minX > maxX || minZ > maxZ) {
        std::printf("invalid range\n");
        return 2;
    }
    std::error_code ec;
    fs::create_directories(out / "local", ec);
    fs::create_directories(out / "downloaded", ec);
    slime::cache::CacheStore store(out);
    std::string err;
    if (!store.load(&err)) {
        std::printf("load store failed: %s\n", err.c_str());
        return 1;
    }

    std::fprintf(stderr, "store loaded\n");
    const int64_t tx0 = floorTile(minX), tx1 = ceilTile(maxX + 1);
    const int64_t tz0 = floorTile(minZ), tz1 = ceilTile(maxZ + 1);
    int built = 0;
    for (int64_t tz = tz0; tz < tz1 && built < maxPacks; tz += 1024) {
        for (int64_t tx = tx0; tx < tx1 && built < maxPacks; tx += 1024) {
            const slime::cache::PackRect rect{ static_cast<int32_t>(tx), static_cast<int32_t>(tx + 1023),
                                               static_cast<int32_t>(tz), static_cast<int32_t>(tz + 1023) };
            std::vector<uint8_t> bits((1024 * 1024 + 7) / 8, 0);
            std::vector<uint8_t> rowBytes(1024, 0);
            for (int row = 0; row < 1024; ++row) {
                slime::SlimeEngine::fillSlimeRowBedrock(
                    static_cast<int>(tz + row), static_cast<int>(tx), 1024, rowBytes.data());
                const size_t base = static_cast<size_t>(row) * 1024;
                for (int col = 0; col < 1024; ++col) {
                    if (rowBytes[static_cast<size_t>(col)]) {
                        const size_t dst = base + static_cast<size_t>(col);
                        bits[dst / 8] |= static_cast<uint8_t>(1u << (dst % 8));
                    }
                }
            }
            const std::string name = "local/pack-" + std::to_string(tx) + "_" + std::to_string(tz) + ".slmz";
            const auto path = out / name;
            if (!slime::cache::writePack(path, rect, bits.data(), bits.size(), &err)) {
                std::printf("write pack failed (%s): %s\n", name.c_str(), err.c_str());
                return 1;
            }
            if (!store.addPack(path, "local", &err)) {
                std::printf("add pack failed (%s): %s\n", name.c_str(), err.c_str());
                return 1;
            }
            ++built;
            std::printf("built %d/%d: %s [%lld..%lld, %lld..%lld]\n", built, maxPacks, name.c_str(),
                        static_cast<long long>(tx), static_cast<long long>(tx + 1023),
                        static_cast<long long>(tz), static_cast<long long>(tz + 1023));
        }
    }
    if (built == 0) {
        std::printf("no pack built\n");
        return 1;
    }
    if (verify) {
        int checked = 0;
        for (const auto& e : store.packs()) {
            std::vector<uint8_t> bitmap;
            if (!store.readPackBitmap(e, bitmap, &err)) {
                std::printf("verify read failed (%s): %s\n", e.file.c_str(), err.c_str());
                return 1;
            }
            const int64_t w = static_cast<int64_t>(e.rect.maxX) - e.rect.minX + 1;
            for (int64_t z = e.rect.minZ; z <= e.rect.maxZ; z += 127) {
                for (int64_t x = e.rect.minX; x <= e.rect.maxX; x += 131) {
                    const size_t idx = static_cast<size_t>((z - e.rect.minZ) * w + (x - e.rect.minX));
                    const bool got = (bitmap[idx / 8] >> (idx % 8)) & 1u;
                    const bool expect = slime::SlimeEngine::isSlimeChunkBedrockMT(
                        static_cast<int>(x), static_cast<int>(z));
                    if (got != expect) {
                        std::printf("verify mismatch at (%lld,%lld)\n", static_cast<long long>(x),
                                    static_cast<long long>(z));
                        return 1;
                    }
                    ++checked;
                }
            }
        }
        std::printf("verify OK: %d samples\n", checked);
    }
    std::printf("done: %d packs, %.2f MB\n", built,
                static_cast<double>(store.totalCompressedBytes()) / (1024.0 * 1024.0));
    return 0;
}
