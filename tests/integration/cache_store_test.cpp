#include "cache/CachePack.h"
#include "cache/CacheStore.h"

#include "core/CacheManager.h"

#include <cstdio>
#include <filesystem>
#include <random>
#include <string>
#include <vector>

int main()
{
    namespace fs = std::filesystem;
    const auto root = fs::temp_directory_path() / "slime_cache_store_test";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root / "local", ec);
    fs::create_directories(root / "downloaded", ec);

    constexpr int kSide = 1024;
    const size_t chunks = static_cast<size_t>(kSide) * kSide;
    std::vector<uint8_t> bits((chunks + 7) / 8, 0);
    std::mt19937 rng(7);
    for (size_t i = 0; i < bits.size(); ++i) {
        uint8_t v = 0;
        for (int b = 0; b < 8; ++b) {
            if (rng() % 10 == 0) v |= static_cast<uint8_t>(1u << b);
        }
        bits[i] = v;
    }
    const slime::cache::PackRect rect{ 0, kSide - 1, 0, kSide - 1 };
    const auto localPath = root / "local" / "pack-000001.slmz";
    const auto downloadedPath = root / "downloaded" / "pack-000002.slmz";
    std::string err;
    if (!slime::cache::writePack(localPath, rect, bits.data(), bits.size(), &err) ||
        !slime::cache::writePack(downloadedPath, rect, bits.data(), bits.size(), &err)) {
        std::printf("write pack failed: %s\n", err.c_str());
        return 1;
    }

    slime::cache::CacheStore store(root);
    if (!store.load(&err)) {
        std::printf("store load failed: %s\n", err.c_str());
        return 1;
    }
    if (!store.addPack(localPath, "local", &err) ||
        !store.addPack(downloadedPath, "downloaded", &err)) {
        std::printf("add pack failed: %s\n", err.c_str());
        return 1;
    }

    slime::cache::CacheStore reopened(root);
    if (!reopened.load(&err)) {
        std::printf("reload failed: %s\n", err.c_str());
        return 1;
    }
    if (reopened.packs().size() != 2 || reopened.totalCompressedBytes() <= 0) {
        std::printf("store index mismatch size=%zu bytes=%lld\n", reopened.packs().size(),
                    static_cast<long long>(reopened.totalCompressedBytes()));
        return 1;
    }
    if (!reopened.findContaining(100, 100) || reopened.findContaining(kSide + 1, 0)) {
        std::printf("findContaining mismatch\n");
        return 1;
    }
    if (!reopened.removePack("downloaded/pack-000002.slmz", &err) || reopened.packs().size() != 1) {
        std::printf("remove downloaded failed: %s\n", err.c_str());
        return 1;
    }
    reopened.load(&err);
    if (reopened.packs().size() != 1) {
        std::printf("index persistence mismatch\n");
        return 1;
    }
    slime::CacheManager cm;
    if (!reopened.fillCacheManager(cm, &err)) {
        std::printf("fillCacheManager failed: %s\n", err.c_str());
        return 1;
    }
    if (!cm.covers(slime::Rect{ 0, kSide - 1, 0, kSide - 1 })) {
        std::printf("CacheManager coverage mismatch\n");
        return 1;
    }
    // CacheManager -> v3 local packs
    slime::CacheManager srcCm;
    slime::Rect srcRect{ 0, 2047, 0, 2047 };
    std::vector<std::pair<int, int>> srcSlime;
    for (int z = 0; z < 2048; ++z) {
        for (int x = 0; x < 2048; ++x) {
            if ((x + z) % 10 == 0) srcSlime.emplace_back(x, z);
        }
    }
    srcCm.mergeRaw({ srcRect }, srcSlime);
    const auto root2 = fs::temp_directory_path() / "slime_cache_store_test2";
    fs::remove_all(root2, ec);
    slime::cache::CacheStore outStore(root2);
    if (!outStore.load(&err) || !outStore.writeFromCacheManager(srcCm, &err)) {
        std::printf("writeFromCacheManager failed: %s\n", err.c_str());
        return 1;
    }
    slime::cache::CacheStore reloadStore(root2);
    if (!reloadStore.load(&err) || reloadStore.packs().empty()) {
        std::printf("reload written store failed: %s\n", err.c_str());
        return 1;
    }
    slime::CacheManager checkCm;
    if (!reloadStore.fillCacheManager(checkCm, &err)) {
        std::printf("written pack fill failed: %s\n", err.c_str());
        return 1;
    }
    if (!checkCm.covers(srcRect) ||
        checkCm.lookup(0, 0) != 1 || checkCm.lookup(1, 0) != 0 ||
        checkCm.lookup(2040, 2040) != 1 || checkCm.lookup(2041, 2040) != 0) {
        std::printf("written pack bitmap mismatch\n");
        return 1;
    }
    fs::remove_all(root2, ec);
    fs::remove_all(root, ec);
    std::printf("cache store test OK\n");
    return 0;
}
