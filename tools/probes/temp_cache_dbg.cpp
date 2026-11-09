// temp_cache_dbg.cpp -- v2 位图缓存的存/取单步诊断（打印每步返回值与 err）
#include "core/CacheManager.h"

#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

using namespace slime;

int main()
{
    const auto dir = std::filesystem::temp_directory_path() / "slime_dbg_v2";
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);
    const auto path = dir / "t.slimecache";

    CacheManager c;
    c.mergeRegion(Rect{ 0, 63, 0, 63 }, { { 1, 2 }, { 3, 4 }, { 5, 6 } });
    std::printf("A) ranges=%zu entries=%lld sizeBytes=%lld bitmapBytes=%lld\n", c.ranges().size(),
                static_cast<long long>(c.entryCount()), static_cast<long long>(c.sizeBytes()),
                static_cast<long long>(c.bitmapBytes()));
    std::printf("   lookup(1,2)=%d lookup(5,5)=%d lookup(100,100)=%d covers(0..63)=%d\n", c.lookup(1, 2),
                c.lookup(5, 5), c.lookup(100, 100), c.covers(Rect{ 0, 63, 0, 63 }) ? 1 : 0);

    std::string err;
    const bool saved = c.saveFile(path, err);
    std::printf("B) saveFile=%d err='%s' 存在=%d\n", saved ? 1 : 0, err.c_str(),
                std::filesystem::exists(path, ec) ? 1 : 0);
    std::printf("   .new 残留=%d\n", std::filesystem::exists(path.string() + ".new", ec) ? 1 : 0);

    CacheManager d;
    const bool loaded = d.loadFile(path, true, err);
    std::printf("C) loadFile=%d err='%s' ranges=%zu entries=%lld\n", loaded ? 1 : 0, err.c_str(),
                d.ranges().size(), static_cast<long long>(d.entryCount()));
    if (loaded) {
        std::printf("   lookup(1,2)=%d lookup(3,4)=%d lookup(5,6)=%d lookup(5,5)=%d\n", d.lookup(1, 2),
                    d.lookup(3, 4), d.lookup(5, 6), d.lookup(5, 5));
    }

    // 空条目的小面积声明（[20]-C 场景）
    CacheManager e;
    e.mergeRegion(Rect{ 0, 19, 0, 19 }, {});
    const bool saved2 = e.saveFile(path, err);
    std::printf("D) 小面积 saveFile=%d err='%s' ranges=%zu\n", saved2 ? 1 : 0, err.c_str(), e.ranges().size());
    CacheManager f;
    const bool loaded2 = f.loadFile(path, true, err);
    std::printf("E) 小面积 loadFile=%d err='%s' ranges=%zu covers=%d\n", loaded2 ? 1 : 0, err.c_str(),
                f.ranges().size(), f.covers(Rect{ 0, 19, 0, 19 }) ? 1 : 0);

    // 大一点的位图（2048×32768，模拟 [12] 的分块缓存）
    CacheManager g;
    std::vector<std::pair<int, int>> cells;
    for (int i = 0; i < 20000; ++i) cells.emplace_back(i % 2048, (i / 2048) % 32768);
    g.mergeRegion(Rect{ 0, 2047, 0, 32767 }, cells);
    const bool saved3 = g.saveFile(path, err);
    std::printf("F) 大位图 saveFile=%d err='%s' sizeBytes=%lld\n", saved3 ? 1 : 0, err.c_str(),
                static_cast<long long>(g.sizeBytes()));
    CacheManager h;
    const bool loaded3 = h.loadFile(path, true, err);
    std::printf("G) 大位图 loadFile=%d err='%s' ranges=%zu entries=%lld lookup(0,0)=%d\n", loaded3 ? 1 : 0,
                err.c_str(), h.ranges().size(), static_cast<long long>(h.entryCount()), h.lookup(0, 0));

    // H) [20]-D 场景：8 行高、两条小面积声明（期望 < 100 条 ⇒ 小面积放行）
    {
        std::filesystem::remove(path, ec);
        CacheManager k;
        const int gapX = 30;
        std::vector<std::pair<int, int>> left, right;
        for (int x = 0; x <= 63; ++x) {
            if (x == gapX) continue;
            for (int z = 0; z <= 7; ++z) {
                if (((x * 31 + z * 17) % 10) == 0) { // 任意稀疏图案
                    if (x < gapX) left.emplace_back(x, z);
                    else right.emplace_back(x, z);
                }
            }
        }
        k.mergeRegion(Rect{ 0, gapX - 1, 0, 7 }, left);
        k.mergeRegion(Rect{ gapX + 1, 63, 0, 7 }, right);
        std::string e4;
        const bool ok4 = k.saveFile(path, e4);
        std::printf("H) 小面积双声明 saveFile=%d err='%s' ranges=%zu entries=%lld sizeBytes=%lld\n", ok4 ? 1 : 0,
                    e4.c_str(), k.ranges().size(), static_cast<long long>(k.entryCount()),
                    static_cast<long long>(k.sizeBytes()));
        CacheManager k2;
        const bool ok5 = k2.loadFile(path, true, e4);
        std::printf("I) 重载 loadFile=%d err='%s' ranges=%zu entries=%lld\n", ok5 ? 1 : 0, e4.c_str(),
                    k2.ranges().size(), static_cast<long long>(k2.entryCount()));
    }
    std::filesystem::remove_all(dir, ec);
    return 0;
}
