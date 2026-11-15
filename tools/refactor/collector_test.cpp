// Phase 1 Collector 策略自检。
#include "core/next/Collector.h"

#include <cstdio>
#include <vector>

using slime::next::Candidate;
using slime::next::CollectorKind;
using slime::next::ResultCollector;

static Candidate c(int id, int64_t d2, int64_t rank, double density = 0.0)
{
    Candidate v;
    v.result.minX = id;
    v.result.maxX = id;
    v.d2 = d2;
    v.rank = rank;
    v.density = density;
    return v;
}

int main()
{
    int fail = 0;
    {
        ResultCollector rc(CollectorKind::DistanceTopN, 2);
        rc.add(c(10, 4, 2));
        rc.add(c(11, 4, 1));
        rc.add(c(12, 1, 3));
        const auto r = rc.results();
        if (rc.totalFound() != 3 || r.size() != 2 || r[0].minX != 12 || r[1].minX != 11) {
            std::printf("FAIL distance topN\n");
            ++fail;
        }
    }
    {
        ResultCollector rc(CollectorKind::DensityTopN, 2);
        rc.add(c(20, 100, 1, 0.5));
        rc.add(c(21, 1, 2, 0.5));
        rc.add(c(22, 0, 3, 0.7));
        const auto r = rc.results();
        if (r.size() != 2 || r[0].minX != 22 || r[1].minX != 21) {
            std::printf("FAIL density topN\n");
            ++fail;
        }
    }
    {
        ResultCollector rc(CollectorKind::CountOnly, 0);
        rc.add(c(30, 0, 0));
        rc.add(c(31, 0, 0));
        if (rc.totalFound() != 2 || !rc.results().empty()) {
            std::printf("FAIL count only\n");
            ++fail;
        }
    }
    {
        ResultCollector a(CollectorKind::DistanceTopN, 2);
        ResultCollector b(CollectorKind::DistanceTopN, 2);
        a.add(c(40, 9, 0));
        b.add(c(41, 2, 0));
        b.add(c(42, 3, 0));
        a.merge(b);
        const auto r = a.results();
        if (a.totalFound() != 3 || r.size() != 2 || r[0].minX != 41 || r[1].minX != 42) {
            std::printf("FAIL merge\n");
            ++fail;
        }
    }
    if (fail == 0) std::printf("Collector ALL PASS\n");
    return fail == 0 ? 0 : 1;
}
