// 将 1024×1024 的 v3 pack 合并为 8192×8192 pack（64 个 1024 block）。
// 仅用于把已有网盘缓存重新打包，不改变 bit 含义。
#include "cache/CachePack.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <map>
#include <string>
#include <tuple>
#include <vector>

namespace fs = std::filesystem;
using namespace slime::cache;

namespace {
constexpr int kOut = 8192;
constexpr int kSrc = 1024;
constexpr int kBlocks = kOut / kSrc;
constexpr size_t kOutBytes = static_cast<size_t>(kOut) * kOut / 8;
constexpr size_t kSrcBytes = static_cast<size_t>(kSrc) * kSrc / 8;
constexpr size_t kRowBytes = static_cast<size_t>(kSrc) / 8;

int floorDiv(int v, int d)
{
    int q = v / d;
    if (v % d < 0) --q;
    return q;
}
} // namespace

int main(int argc, char** argv)
{
    if (argc < 3) {
        std::cerr << "usage: repack_1024_to_8192 <srcRoot> <outRoot> [minX minZ]\n";
        return 2;
    }
    const fs::path srcRoot = fs::path(argv[1]);
    const fs::path outRoot = fs::path(argv[2]);
    bool oneGroup = false;
    int onlyX = 0, onlyZ = 0;
    if (argc >= 5) {
        oneGroup = true;
        onlyX = std::atoi(argv[3]);
        onlyZ = std::atoi(argv[4]);
    }
    std::map<std::pair<int, int>, fs::path> src;
    for (const char* sub : { "local", "downloaded" }) {
        const fs::path dir = srcRoot / sub;
        std::error_code ec;
        if (!fs::is_directory(dir, ec)) continue;
        for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
            if (!it->is_regular_file() || it->path().extension() != ".slmz") continue;
            PackInfo info;
            std::string err;
            if (!peekPack(it->path(), info, &err)) {
                std::cerr << "skip " << it->path().string() << ": " << err << "\n";
                continue;
            }
            if (info.rect.width() != kSrc || info.rect.height() != kSrc) continue;
            src[std::make_pair(info.rect.minX, info.rect.minZ)] = it->path();
        }
    }
    if (src.empty()) { std::cerr << "no source packs\n"; return 1; }
    fs::create_directories(outRoot / "local");

    std::vector<std::pair<int, int>> groups;
    if (oneGroup) {
        groups.emplace_back(onlyX, onlyZ);
    } else {
        int minX = src.begin()->first.first, maxX = minX;
        int minZ = src.begin()->first.second, maxZ = minZ;
        for (const auto& [k, p] : src) {
            minX = std::min(minX, k.first); maxX = std::max(maxX, k.first);
            minZ = std::min(minZ, k.second); maxZ = std::max(maxZ, k.second);
        }
        for (int z = floorDiv(minZ, kOut) * kOut; z <= maxZ; z += kOut)
            for (int x = floorDiv(minX, kOut) * kOut; x <= maxX; x += kOut)
                groups.emplace_back(x, z);
    }

    std::vector<uint8_t> out(kOutBytes);
    for (const auto& [gx, gz] : groups) {
        bool complete = true;
        for (int bz = 0; bz < kBlocks && complete; ++bz)
            for (int bx = 0; bx < kBlocks; ++bx)
                if (src.find({ gx + bx * kSrc, gz + bz * kSrc }) == src.end()) { complete = false; break; }
        fs::path outPath = outRoot / "local" / ("pack--" + std::to_string(gx) + "_" + std::to_string(gz) + ".slmz");
        if (!complete) {
            if (oneGroup) { std::cerr << "incomplete group " << gx << "," << gz << "\n"; return 1; }
            continue;
        }
        std::fill(out.begin(), out.end(), 0);
        for (int bz = 0; bz < kBlocks; ++bz) {
            for (int bx = 0; bx < kBlocks; ++bx) {
                PackInfo info;
                std::vector<uint8_t> bits;
                std::string err;
                const auto& sp = src.at({ gx + bx * kSrc, gz + bz * kSrc });
                if (!readPack(sp, info, bits, &err)) {
                    std::cerr << "read " << sp.string() << ": " << err << "\n";
                    return 1;
                }
                for (int row = 0; row < kSrc; ++row) {
                    const size_t dst = static_cast<size_t>(bz * kSrc + row) * (kOut / 8) + bx * kRowBytes;
                    std::memcpy(out.data() + dst, bits.data() + static_cast<size_t>(row) * kRowBytes, kRowBytes);
                }
            }
        }
        PackRect rect{ gx, gx + kOut - 1, gz, gz + kOut - 1 };
        std::string err;
        if (!writePack(outPath, rect, out.data(), out.size(), &err)) {
            std::cerr << "write " << outPath.string() << ": " << err << "\n";
            return 1;
        }
        std::cout << outPath.string() << "\n";
    }
    return 0;
}
