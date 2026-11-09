# ④′ 修复（方案 B）代码改动范围 —— 待用户确认后才动代码

根因（已坐实，见 `docs/v338_progress.md` 三-c 与逐 range 双峰数据）：
`pruneToFit` 整 range 丢条目 ⇒ 之后**跨运行**再次登记同一个 `sub.region()`（该块本身没有新增条目，因为其行大多已命中缓存）
⇒ `m_ranges` 里留下**没有条目支撑的声明** ⇒ `covers()` 为 true、`lookup()` 一律返回 0（「已知非史莱姆」）
⇒ 命中区被判为非史莱姆 ⇒ **漏 34% 且 0 假阳性**（与实测形态一致）。

方案 B = **range 声明与条目同步裁剪**（用户已选）。**不改格式、不改 pruneToFit 淘汰策略、不改收集逻辑。**

---

## 改动 1：登记前先确认「有新增条目」（引擎分块路径）

**文件**：`src/core/SlimeEngine.cpp`
**位置**：`runChunkBody` 内的缓存归并块（现为 `pendingRanges.push_back(sub.region());` 无条件登记）

**改法**：
```cpp
// 现在
pendingRanges.push_back(sub.region());
if (!so.newCacheChunks.empty()) { pendingEntries.insert(...); }

// 改为（方案 B）
if (!so.newCacheChunks.empty()) {
    pendingRanges.push_back(sub.region());   // 有新增条目 → 登记（声明有支撑）
    pendingEntries.insert(pendingEntries.end(), so.newCacheChunks.begin(), so.newCacheChunks.end());
} else {
    ++perfSkippedHollowRanges;               // 无新增条目 → 不登记（旧声明若已空壳，由加载期清扫兜底）
}
```
**语义影响**：一个块若 0 新增条目，说明其扫描行**已经被保留的 range 覆盖**（否则会有新增条目，762 行 × 209715 列
在基岩 ~10% 概率下期望约 1600 万条）⇒ 不登记**不损失覆盖信息**，只可能让未来重扫（正确性优先）。
**已知极小概率例外**：某块 762×209715 个位置**全部非史莱姆**（概率 ≈ (0.9)^1.6e8 ≈ 0）⇒ 可忽略，写进注释。

## 改动 2：登记前先确认「有新增条目」（GUI 经典路径）

**文件**：`src/gui/MainWindow.cpp`
**位置**：`onSearchFinished` 内的整区合并（`m_cache->mergeRegion(info.region, out.newCacheChunks);`）

**改法**：同一规则——`out.newCacheChunks` 为空时**不登记**该区域（`mergeRegion` 内部本就会加 range，需改为条件调用），
并在日志里记一行「本次范围无新增条目，未登记覆盖声明（避免空壳声明）」。

## 改动 3：加载期清扫空壳 range（跨运行兜底）

**文件**：`src/core/CacheManager.h` / `.cpp`
**新增**：`int64_t dropHollowRanges();`（返回剔除的 range 数）
**实现**：对 `m_entries` 做**一次**遍历，按「第一个包含该条目的 range」计数（`m_entries` 已按打包坐标排序，
每条目只需在 `m_ranges` 上线性查找并早退）；遍历完成后，把所有**计数为 0 的 range** 从 `m_ranges` 中删除
⇒ `m_ranges` 与 `m_entries` 严格同步。复杂度 O(N × R̄)（R=12 时 ≈1.5e8 次包含判定，实测 1~2 s 级）。
**调用点**：`loadFile()` 末尾（replace 与 merge 两种模式后都调用）+ `mergeRaw()` 之后（导入路径）。
**日志**：`IncidentLog`/`Logger` 写「缓存加载：剔除空壳 range N 个（声明保留 k 个，条目 M 条）」。
**注意**：不删除条目、不改文件、不改格式；纯内存修正；保存时按修正后的 `m_ranges` 落盘（自然收敛）。

## 改动 4：验证用单元用例（不是产品逻辑）

**文件**：`tests/test_slime.cpp`（新增断言，挂在现有 `[10] 缓存容量剪枝` 或新组 `[20]`）
1. **复现 ④′**（旧行为）：merge range A（带条目）→ `pruneToFit` 到丢掉 A → 再 `mergeRegion(A, {})`（0 条目）
   ⇒ 断言旧代码下 `covers(A)==true`（这条在修好后应变为 false，作为回归锚点）；
2. **修复后**：同上构造 → 断言 `covers(A)==false`、`lookup(A 内任一点)==-1`；
3. **加载期清扫**：构造带有空壳 range 的缓存 → `saveFile` → `loadFile` → 断言空壳 range 被剔除、条目数不变。

---

## 验收（三条，照任务书）

| 项 | 当前 | 目标 |
|---|---|---|
| `[20]` 用例一致率 | 100%（200×200，未复现） | **100%**（含改动 4 的新断言） |
| E2 真值比对 1 万点 agree | 96.67% | **100%** |
| 111/111（严格模式逐位一致） | 通过 | **通过** |

## 影响面与风险

- **改动 1/2** 只影响「是否登记声明」，不影响任何计算结果 ⇒ 对 111/111 无影响（预期）；
- **改动 3** 会在加载时剔除空壳声明 ⇒ 那些区域下次搜索会**重新扫描**（正确性恢复，代价是重扫）；
  对用户当前 1.02 GB 缓存：预计剔除 **4 个空壳 range**（#4/#5/#6/#7），剩余 8 个 range 的 1.28 亿条目仍可用；
- **不改格式**（v1 不变）⇒ 与任务 2（历史缓存方案 A：改名 `.legacy` + 提示 + 默认不用于跳过判定）**互不冲突**，
  可先做任务 1（本方案），再做任务 2；两者都在同一版本发布。
- 不触碰：`pruneToFit` 淘汰策略、收集逻辑、缓存文件格式、位图化（v2）。

## 待确认的两个细节（请你拍板）

1. **改动 1 的替代方案**：是否允许「有新增条目**或**该块被保留 range 完全覆盖」才登记？
   （更精确，但需要在引擎里对每个块做一次 `covers()` 查询；当前建议**先用最简单的「有新增条目才登记」**。）
2. **改动 3 的执行时机**：只在 `loadFile` 时清扫（省时间），还是每次保存前也清扫（更早收敛）？
   建议：**加载时清扫 + 保存前再清扫一次**（保存前清扫可让 `.legacy` 之后的首次落盘即干净）。

确认后我按 1 → 2 → 3 → 4 顺序实施，每改一处立即用 `[20]` + E2 验证，再进任务 2。
