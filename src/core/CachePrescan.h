// CachePrescan.h - 缓存位图正方形密度预筛（Qt 无关）
//
// 背景（docs/planning/HANDOFF_2026-11-28.md §5 P1）：
//   环形搜索的「预筛精修(2) / 环带上界复核(3)」档此前只把缓存接进 predicate，
//   而候选种子仍由 GPU 的 prescanDensitySeeds 对整张正方形做一次全图扫描。
//   缓存已完整覆盖该区域时，位图里本来就有每个区块的确定值，直接按行流式统计
//   窗口密度即可得到同样的种子，省掉整次 GPU 扫描。
//
// 本模块与 slime::GpuScan::prescanDensitySeeds 的语义对齐（详见
// docs/planning/k3_cache_prescan.md）：
//   * gridSize / 窗口左上角范围 / 密度阈值 / bandRows 分带 / 每带 topPerBand；
//   * 全局按 count 降序、count 相同按 (cx,cz) 升序排序；
//   * minSeparation 的 3×3 邻格哈希去重、maxSeeds 截断、取消检查；
//   * 种子中心 cx = 窗口x + gridSize/2、cz = 窗口z + gridSize/2。
#pragma once

#include <atomic>
#include <string>
#include <vector>

#include "SlimeEngine.h" // DensityPrescanParams / DensitySeed

namespace slime {

class CacheManager;

// 从 CacheManager 位图生成密度预筛种子。
//
// 前置条件：cache.covers({p.minX, p.maxX, p.minZ, p.maxZ}) 必须成立，
//   否则返回 false 且 *err = "缓存未完整覆盖"（调用方应回退 GPU 预筛）。
// 成功返回 true 且 out 非空；失败返回 false（*err 写入原因；取消时 "已取消"）。
// 参数非法（gridSize ∉ 1..15、窗口范围为空）返回 false。
//
// 实现：按 bandRows 分带、逐行从位图取位并维护列和 + 水平滑动窗口，
//   每个窗口 O(1) 算术、每个区块位约 1 次位测试；不复制整区域位图，
//   峰值内存 O(区域行宽 + topPerBand)。坐标可为负，内部用 int64 计算防溢出。
bool cachePrescanSeeds(const CacheManager& cache, const DensityPrescanParams& p,
                       const std::atomic<bool>* cancel,
                       std::vector<DensitySeed>& out, std::string* err);

} // namespace slime
