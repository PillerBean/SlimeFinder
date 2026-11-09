// temp_makebigcache.cpp — 生成一个「合法的大体积 v2 位图缓存」用于导入进度/不冻结验收。
//   v2 布局：24 B 头（magic/ver=2/rangeCount/reserved/outlierCount）
//            + 每 range：16 B 矩形 + 4 B 字数 + 行对齐位图（ceil(W/64) 字 × H 行）
//            + outlier 坐标段（本工具写 0 条）
//   密度固定 10%（((x*7 + z*13) % 10)==0）——满足 dropHollowRanges 的支撑度判据（≥ 期望值 50%）。
//   用法：temp_makebigcache <out> <ranges> <width> <height> [--verify]
#include "core/CacheManager.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

int main(int argc, char** argv)
{
    if (argc < 5) {
        std::printf("用法: temp_makebigcache <out> <ranges> <width> <height> [--verify]\n");
        return 2;
    }
    const std::filesystem::path out = argv[1];
    const int ranges = std::atoi(argv[2]);
    const int W = std::atoi(argv[3]);
    const int H = std::atoi(argv[4]);
    const bool verify = (argc > 5 && std::strcmp(argv[5], "--verify") == 0);
    if (ranges <= 0 || W <= 0 || H <= 0) {
        std::printf("参数非法\n");
        return 2;
    }

    const uint32_t magic = 0x534C4D45U, ver = 2, reserved = 0;
    const uint64_t outliers = 0;
    const int64_t wordsPerRow = (static_cast<int64_t>(W) + 63) / 64;
    const int64_t totalWords = wordsPerRow * H;

    std::ofstream f(out, std::ios::binary | std::ios::trunc);
    if (!f) {
        std::printf("无法写入 %s\n", out.string().c_str());
        return 1;
    }
    const uint32_t rc = static_cast<uint32_t>(ranges);
    f.write(reinterpret_cast<const char*>(&magic), 4);
    f.write(reinterpret_cast<const char*>(&ver), 4);
    f.write(reinterpret_cast<const char*>(&rc), 4);
    f.write(reinterpret_cast<const char*>(&reserved), 4);
    f.write(reinterpret_cast<const char*>(&outliers), 8);

    int64_t entries = 0;
    std::vector<uint64_t> row(static_cast<size_t>(wordsPerRow));
    for (int k = 0; k < ranges; ++k) {
        const int32_t minX = k * (W + 1000);
        const int32_t maxX = minX + W - 1;
        const int32_t minZ = 0;
        const int32_t maxZ = H - 1;
        const int32_t rect[4] = { minX, maxX, minZ, maxZ };
        const uint32_t words = static_cast<uint32_t>(totalWords);
        f.write(reinterpret_cast<const char*>(rect), 16);
        f.write(reinterpret_cast<const char*>(&words), 4);
        for (int32_t z = minZ; z <= maxZ; ++z) {
            std::memset(row.data(), 0, row.size() * sizeof(uint64_t));
            for (int32_t x = minX; x <= maxX; ++x) {
                if (((static_cast<int64_t>(x) * 7 + static_cast<int64_t>(z) * 13) % 10) != 0) continue;
                const int64_t col = x - minX;
                row[static_cast<size_t>(col >> 6)] |= (1ULL << (col & 63));
                ++entries;
            }
            f.write(reinterpret_cast<const char*>(row.data()),
                    static_cast<std::streamsize>(row.size() * sizeof(uint64_t)));
        }
    }
    f.close();
    const auto bytes = static_cast<int64_t>(std::filesystem::file_size(out));
    std::printf("已写出 %s\n  区域=%d 每区 %dx%d（位图 %.1f MiB/区） 文件=%.1f MiB 期望条目=%lld\n",
                out.string().c_str(), ranges, W, H, totalWords * 8.0 / (1024.0 * 1024.0),
                bytes / (1024.0 * 1024.0), static_cast<long long>(entries));

    if (verify) {
        slime::CacheManager cm;
        std::string err;
        int64_t samples = 0;
        std::pair<int64_t, int64_t> last{ 0, 0 };
        bool monotonic = true, reachedEnd = false;
        const auto t0 = std::chrono::steady_clock::now();
        const bool ok = cm.loadFile(out, true, err, [&](int64_t d, int64_t t) {
            ++samples;
            if (d < last.first) monotonic = false;
            last = { d, t };
            reachedEnd = (d >= t);
        });
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        std::printf("  回读：ok=%d 区域=%zu 条目=%lld（期望 %lld）用时=%.0f ms 进度样本=%lld 单调=%d 到100%%=%d\n",
                    ok ? 1 : 0, cm.ranges().size(), static_cast<long long>(cm.entryCount()),
                    static_cast<long long>(entries), ms, static_cast<long long>(samples),
                    monotonic ? 1 : 0, reachedEnd ? 1 : 0);
        if (!ok) std::printf("  错误：%s\n", err.c_str());
        return (ok && cm.entryCount() == entries) ? 0 : 1;
    }
    return 0;
}
