// temp_owned.cpp -- 独立解析 .slimecache 文件（不链接产品代码），给出逐 range 的
// 「首个包含它的 range」归属计数（= 空壳判据 used by dropHollowRanges）与「任意包含」计数。
// 用途：核对 loadFile 的空壳清扫到底会剔除哪几个 range，以及为什么。
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>
#include <algorithm>

struct R { int minX, maxX, minZ, maxZ; };

int main(int argc, char** argv)
{
    if (argc < 2) { std::printf("usage: temp_owned <cache>\n"); return 1; }
    FILE* f = std::fopen(argv[1], "rb");
    if (!f) { std::printf("open fail\n"); return 2; }
    uint32_t magic = 0, ver = 0, rangeCount = 0;
    uint64_t entryCount = 0;
    std::fread(&magic, 4, 1, f);
    std::fread(&ver, 4, 1, f);
    std::fread(&rangeCount, 4, 1, f);
    std::fread(&entryCount, 8, 1, f);
    std::printf("magic=%08X ver=%u rangeCount=%u entryCount=%llu\n", magic, ver, rangeCount,
                static_cast<unsigned long long>(entryCount));
    std::vector<R> rs(rangeCount);
    for (uint32_t i = 0; i < rangeCount; ++i) {
        int32_t v[4];
        if (std::fread(v, 16, 1, f) != 1) { std::printf("range read fail\n"); return 3; }
        rs[i] = R{ v[0], v[1], v[2], v[3] };
    }
    std::vector<int64_t> owned(rangeCount, 0), anyCnt(rangeCount, 0);
    int64_t notCovered = 0, total = 0;
    const size_t kBuf = 1 << 20;
    std::vector<uint64_t> buf(kBuf);
    for (;;) {
        const size_t n = std::fread(buf.data(), 8, kBuf, f);
        if (n == 0) break;
        for (size_t k = 0; k < n; ++k) {
            const uint64_t e = buf[k];
            const int x = static_cast<int>(static_cast<int32_t>(e >> 32));
            const int z = static_cast<int>(static_cast<int32_t>(e & 0xFFFFFFFFULL));
            ++total;
            bool any = false;
            for (uint32_t i = 0; i < rangeCount; ++i) {
                if (x >= rs[i].minX && x <= rs[i].maxX && z >= rs[i].minZ && z <= rs[i].maxZ) {
                    if (!any) { ++owned[i]; any = true; }
                    ++anyCnt[i];
                }
            }
            if (!any) ++notCovered;
        }
        if (n < kBuf) break;
    }
    std::fclose(f);
    std::printf("entries read=%lld  notCoveredByAnyRange=%lld\n", static_cast<long long>(total),
                static_cast<long long>(notCovered));
    std::printf("%-3s %-42s %14s %14s %14s %10s %10s\n", "#", "rect", "area", "owned(1st)",
                "anyCnt", "own%", "any%");
    int64_t droppedEntries = 0;
    for (uint32_t i = 0; i < rangeCount; ++i) {
        const R& r = rs[i];
        const int64_t area = static_cast<int64_t>(r.maxX - r.minX + 1) * (r.maxZ - r.minZ + 1);
        char rect[128];
        std::snprintf(rect, sizeof(rect), "%d..%d x %d..%d", r.minX, r.maxX, r.minZ, r.maxZ);
        std::printf("%-3u %-42s %14lld %14lld %14lld %9.3f%% %9.3f%%%s\n", i, rect,
                    static_cast<long long>(area), static_cast<long long>(owned[i]),
                    static_cast<long long>(anyCnt[i]),
                    100.0 * static_cast<double>(owned[i]) / static_cast<double>(area),
                    100.0 * static_cast<double>(anyCnt[i]) / static_cast<double>(area),
                    owned[i] == 0 ? "   <== 空壳(owned=0)：会被 dropHollowRanges 剔除" : "");
        if (owned[i] == 0) droppedEntries += anyCnt[i];
    }
    std::printf("owned=0 的 range 数=%d ；这些 range 的 anyCnt 合计=%lld（剔除后条目仍全部保留）\n",
                static_cast<int>(std::count(owned.begin(), owned.end(), int64_t{ 0 })),
                static_cast<long long>(droppedEntries));
    return 0;
}
