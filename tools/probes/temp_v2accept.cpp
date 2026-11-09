// temp_v2accept.cpp -- v2 位图缓存格式的验收探针（真实 1.02 GB 缓存）
//   usage: temp_v2accept <inCache(v1)> <outCache(v2)>
// 输出：v1 加载耗时 / 声明数 / 条目数 / 位图字节；v2 保存耗时与文件体积（验收 ≤250 MiB）；
//       v2 加载耗时（验收 ≤1.5 s）；两次的一万点真值比对（应 100%）。
#include "core/CacheManager.h"
#include "core/SlimeEngine.h"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <random>
#include <string>

using namespace slime;
using Clock = std::chrono::steady_clock;
static double msSince(Clock::time_point a, Clock::time_point b)
{
    return std::chrono::duration<double, std::milli>(b - a).count();
}

struct E2 {
    long long checked = 0, agree = 0, cacheOnly = 0, truthSlime = 0;
};

static E2 e2(CacheManager& c)
{
    E2 m;
    int minX = 0, maxX = 0, minZ = 0, maxZ = 0;
    bool first = true;
    for (const Rect& r : c.ranges()) {
        if (first) { minX = r.minX; maxX = r.maxX; minZ = r.minZ; maxZ = r.maxZ; first = false; }
        else {
            if (r.minX < minX) minX = r.minX;
            if (r.maxX > maxX) maxX = r.maxX;
            if (r.minZ < minZ) minZ = r.minZ;
            if (r.maxZ > maxZ) maxZ = r.maxZ;
        }
    }
    if (first) return m;
    std::mt19937_64 rng(7);
    while (m.checked < 10000) {
        const int x = minX + static_cast<int>(rng() % static_cast<unsigned>(maxX - minX + 1));
        const int z = minZ + static_cast<int>(rng() % static_cast<unsigned>(maxZ - minZ + 1));
        const int lk = c.lookup(x, z);
        if (lk < 0) continue;
        const bool truth = SlimeEngine::isSlimeChunk(Version::Bedrock, 0, x, z);
        ++m.checked;
        if (truth) ++m.truthSlime;
        if ((lk == 1) == truth) ++m.agree;
        else if (lk == 1) ++m.cacheOnly;
    }
    return m;
}

int main(int argc, char** argv)
{
    if (argc < 3) { std::printf("usage: temp_v2accept <inCache> <outCache>\n"); return 1; }
    const std::string inPath = argv[1], outPath = argv[2];

    CacheManager a;
    std::string err;
    const auto t0 = Clock::now();
    if (!a.loadFile(inPath, true, err)) { std::printf("v1 load fail: %s\n", err.c_str()); return 2; }
    const auto t1 = Clock::now();
    const E2 m1 = e2(a);
    std::printf("v1 加载=%.0fms 声明=%zu 条目=%lld 位图=%.1f MiB v2口径体积=%.1f MiB\n", msSince(t0, t1),
                a.ranges().size(), static_cast<long long>(a.entryCount()),
                a.bitmapBytes() / 1048576.0, a.sizeBytes() / 1048576.0);
    std::printf("   一万点真值比对：agree=%lld/%lld (%.2f%%) cacheOnlySlime=%lld\n", m1.agree, m1.checked,
                100.0 * static_cast<double>(m1.agree) / static_cast<double>(m1.checked), m1.cacheOnly);

    std::error_code ec;
    std::filesystem::remove(outPath, ec);
    const auto t2 = Clock::now();
    const bool saved = a.saveFile(outPath, err);
    const auto t3 = Clock::now();
    const auto sz = std::filesystem::file_size(outPath, ec);
    std::printf("v2 保存=%d 耗时=%.0fms 文件=%.1f MiB（%lld B）%s\n", saved ? 1 : 0, msSince(t2, t3),
                static_cast<double>(sz) / 1048576.0, static_cast<long long>(sz),
                (sz <= 250ULL * 1024 * 1024) ? "✅ ≤250 MiB" : "❌ >250 MiB");
    if (!saved) { std::printf("   err=%s\n", err.c_str()); return 3; }

    CacheManager b;
    const auto t4 = Clock::now();
    if (!b.loadFile(outPath, true, err)) { std::printf("v2 load fail: %s\n", err.c_str()); return 4; }
    const auto t5 = Clock::now();
    const double v2load = msSince(t4, t5);
    const E2 m2 = e2(b);
    std::printf("v2 加载=%.0fms %s 声明=%zu 条目=%lld 位图=%.1f MiB\n", v2load,
                (v2load <= 1500.0) ? "✅ ≤1.5s" : "❌ >1.5s", b.ranges().size(),
                static_cast<long long>(b.entryCount()), b.bitmapBytes() / 1048576.0);
    std::printf("   一万点真值比对：agree=%lld/%lld (%.2f%%) cacheOnlySlime=%lld\n", m2.agree, m2.checked,
                100.0 * static_cast<double>(m2.agree) / static_cast<double>(m2.checked), m2.cacheOnly);
    return 0;
}
