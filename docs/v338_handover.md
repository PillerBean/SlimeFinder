# v3.3.8 交接文档（冷启动自足版）

> 用途：会话上下文耗尽后的续接。**只读本文件 + `docs/v338_fixB_scope.md`（+ 本文件 §0A 的证据表）即可开工**，无需回溯聊天记录。
> 生成时间：v3.3.8 任务 1 开工前；**更新于任务 1 改动 1/2/3 实施后（触发停手条件 ③，见 §0A）**。

---

## 0. 一句话状态

**v3.3.8 已完整交付并打包**：④′ 方案 B（含判据 B）+ 历史缓存方案 A + 候选 ② + E3 性能分解全部完成，
1.02 GB 缓存**正好剔除 #4/#5/#6/#7 ⇒ 8 range / 127,842,361 条目不变**、**E2 = 100.00%**；
全量回归 **单元 20 组 / 111/111 / 内核 19 项 / GUI 开与关 / 审计 CLEAN**，
发布验证 **16/16 PASS**，包 `dist/SlimeFinder-3.3.8-win64.zip`（SHA256 `9B734185…0DC6`）。
**另已完成 v2 位图缓存格式（任务 3+5 第 2 步）**：真机 1.02 GB 缓存 **975 → 152.4 MiB（6.40× 更小）**、加载 **5,965 → 251 ms（23.8× 更快）**、判定语义不变（一万点 100.00%）、保存改为原子替换（回读校验 + `ReplaceFileW`，失败不损坏旧文件）。
**下一阶段（未开始）**：任务 3+5 第 3 步（引擎命中区直接用缓存位图当 grid，跳过占墙钟 47% 的填充）、任务 4（缓存优先 + 命中即停）、任务 6 达标（≥2 亿/秒，E3 已定方向 B）。

| 项 | 状态 |
|---|---|
| 任务 1（④′ 修复，方案 B） | **完成**（判据 B + 改动 1/2/3/4 + 全部回归，见 §0A） |
| 任务 2（历史缓存方案 A） | **完成**（`.legacy` 改名 + 提示 + 默认不用于跳过判定 + 7 天自动清理 + 导入过滤器可回滚，见 §0B） |
| 任务 3（验证实验一组） | **完成**：线性对应成立（面积口径 69.8% → 命中 69.86%；假阳性 0；清扫后 100%），见 §0B |
| 候选 ②（762 vs 758） | **完成**：登记域 = 758 锚点行（相邻块声明不重叠），见 §6 与验证报告 §三 |
| 任务 6 / E3（填充 vs 滑窗分解） | **完成（判据明确）**：填充 47% / 滑窗 53%；位图缩小无收益 ⇒ **方向 B 优先**，见 §0B |
| **打包** | **已完成**：`dist/SlimeFinder-3.3.8-win64.zip`（SHA256 `9B734185…0DC6`）+ `SHA256SUMS-3.3.8.txt`；全量验证 **16/16 PASS**，见 §8 |
| **文件夹整理** | **已完成**：工作区根目录只剩 3 项；工具入仓、证据归档、草稿清理，见 §9 |
| **v2 位图缓存格式**（任务 3+5 第 2 步） | **已完成**：v1 兼容读 / 体积 152.4 MiB ≤250 MiB / 加载 251 ms ≤1.5 s / 原子替换+回读校验（单元 [21]），见验证报告 §五-b |
| **缓存位图直接当 grid**（任务 3+5 第 3 步） | **已完成**：整行命中即 memcpy 跳过填充；**A/B 223→22 ms（10.22×）**、fill 201→2 ms、结果逐位一致（单元 `[22]`），见验证报告 §五-c |
| **任务 4：缓存优先 + 命中即停** | **已完成**：缓存覆盖块优先处理 + 堆满即剪；单元 `[23]`（剪枝 2 块 / 扫描量 31.8% / 结果逐位一致），见验证报告 §五-d |
| **任务 6 达标（≥2 亿/秒）** | ❌ **未达标（如实声明，已端到端复测）**：439.8 亿 CPU-only 444.8 s、GPU 标志开启 **449.5 s（98.4M/秒）**，目标 ≤221 s；缓存侧开销 13.3 s → **1.9 s**。最高杠杆 = 提前停止场景接入 O(1) 滑窗（当前该场景逐窗计数），见验证报告 §五-e-1 / backlog B-12 |
| 发布包 | **当前版本 v3.3.8**：`dist/SlimeFinder-3.3.8-win64.zip`（SHA256 `9B734185…0DC6`）；上一版 `dist/SlimeFinder-3.3.7.2-win64.zip`（SHA256 `C1A877E9…B94C`） |
| **旧缓存处置（已确认）** | 用户 2026-09-21 确认：**保持方案 A（整体停用 + 强制重扫）** —— 尽管任务 1 已能「修复后继续使用」，仍按已定方案执行（本轮实现即为最终形态） |

**最终回归（本轮收尾，全部绿色）**：单元 **20 组** ✔ / **111/111** ✔ / GUI 自检 exit 0（16.3 s）✔ /
`[20]` 一致率 100%（经典 + 强制分块）✔ / **E2 = 100.00%** ✔ / 污染审计 **CLEAN**（175 文件，exit 0）✔ /
1.02 GB 缓存加载后 **8 range / 127,842,361 条目不变** ✔。

---

## 0A. 判据 B 与实测结果（任务 1 收尾）

**过程**：首版按「空壳 = 0 条目」实施 → 1.02 GB 缓存只剔 1 个而非 4 个 ⇒ 触发停手条件 ③ ⇒ 取证发现真实形态是
**「声明矩形的绝大部分没有条目支撑」**（#5/#6/#7 各有 ~84k 条 = 与相邻声明重叠的 4 行带，见下表）⇒ **用户拍板选 B**。

**最终判据（`CacheManager::dropHollowRanges`，用户 2026-09-21 拍板）**：
声明可信 ⟺ 声明内条目数 ≥ 期望条目数 × **50%**，期望 = 面积 × 基准密度 **10%**；期望 < 100 条（面积 < 1000 区块）
的小面积声明不做判断、一律保留。两趟扫描：① `owned`（首个包含，早退）达阈值者直接判可信；② 仅对未达标者精确统计 `anyCnt`。

| # | Z 带 | owned | anyCnt | 密度 | 判据 B 结果 |
|---|---|---|---|---|---|
| 0~3、8~11 | 8 个健康带 | 1.589e7~1.599e7 | 同 owned | 9.95~10.01% | **保留** |
| 4 | -98035..-97274 | 0 | 84,192 | 0.000% / 0.053% | **剔除** |
| 5 | -101825..-101064 | 84,095 | 84,095 | 0.053% | **剔除** |
| 6 | -104099..-103338 | 83,939 | 167,831 | 0.053% / 0.105% | **剔除** |
| 7 | -97277..-96516 | 83,736 | 83,736 | 0.052% | **剔除** |

**复验结果（判据 B 之后）**：
```
temp_band（逐 range 真值比对）：8 个声明命中率 100.00%（旧#3 为 99.49%）；合计漏判 0.06%、一致率 99.99%
temp_e2（一万点）：agree = 10000/10000 = 100.00%（cacheSlime=967 truthSlime=967 cacheOnlySlime=0）✓
temp_range_rate：ranges=8（清扫后；文件头仍 12 = 原文件未被改动），entries=127,842,361 不变
单元 20 组全绿（新增 [20]）/ 111/111 全绿 / GUI 自检 exit 0 / [20] 缓存完整性一致率 100%
1.02 GB 加载 7,227~8,267 ms（清扫增量 ≈ +0.9~2.5 s，可接受）
```
**残留（已记录，不属本轮）**：旧#3 与其下方声明重叠的 4 行带（≈4/762 行）条目在剪枝中丢失、两条声明都还在 ⇒
该带漏判 0.51%（全缓存摊薄 0.06%）；彻底消除需按行/位图记录支撑（v2 格式范畴）。

**用户 1.02 GB 缓存未被改动**（mtime 19:05:40；SHA256 `B27140107BF5D42F5AA0994772C296EFA606ADAE725461D721B3CF603E47B93F`），
备份：`C:\Temp\slimecache\backup\real_cache_20260921.slimecache`（同 SHA256）。

---

## 0B. 任务 2 / 任务 3 结果（均已完成）

### 任务 2：历史缓存方案 A（`src/gui/MainWindow.cpp`，纯文件操作、无 UI 依赖）

| 项 | 内容 |
|---|---|
| 常量 | `kCacheLegacySuffix=".legacy"`、`kCacheMigratedMarker="slime_cache.migrated"`、`kCacheLegacyKeepDays=7` |
| 函数 | `LegacyCacheMigration migrateLegacyCachesOnce(dir, keepDays)`：① 到期清理（只删自己产生的 `*.legacy`，按最后写入时间 > 7 天）② 一次性迁移（标记不存在时把 `slime_cache.slimecache` / `slime_cache.cache` 改名 `*.legacy`；rename 失败退回复制+删除）③ 落标记 |
| 启动接线 | 构造函数**加载缓存之前**调用；有文件被改名 ⇒ 本次**不加载**缓存 + 反馈栏（警告级）+ 状态栏 + `Logger::warning` + `IncidentLog::note`；交互模式再 `QTimer::singleShot(0,…)` 弹一次说明框（**自检必须跳过** —— 构造期 `m_uiQuiet` 还没置位，本轮踩到并修好） |
| 回滚 | 导入过滤器首组加 `*.legacy`（`*.slimecache *.cache *.pkl *.pickle *.legacy`）；显式导入即视为用户确认使用（导入后仍走支撑度清扫） |
| 一次性语义 | 标记存在即已迁移 ⇒ 7 天后 `.legacy` 被清理也不会把新缓存误判为历史缓存而反复失效 |
| 验证 | GUI 自检新增 **12.8 段**（临时目录 4 组断言：双改名+标记+提示请求 / 再次运行不迁移 / 8 天删 1 天留 / 过滤器含 `*.legacy`）**全绿**；构建目录实测改名 + 标记 + 新缓存未被再次停用 ✓ |

> **一个已澄清的取舍（用户 2026-09-21 确认：保持方案 A）**：任务 1 落地后，那份 1.02 GB 缓存在加载时已能被**修复**
> （剔除 4 个无支撑声明 ⇒ 剩 8 个声明逐点 100% 正确、E2 100.00%）。按已定方案 A，它仍会被**整体停用**并要求重扫
> 439.8 亿（≈7.5 min）—— 用户确认维持该方案（本轮实现即最终形态，无需再改）。

### 任务 3：④′ 尺度验证实验（`probe20\temp_task3.exe`，结果存档 `C:\Temp\slimecache\task3_result.txt`）

尺度：全宽 × 3160 行 = 6.63 亿区块 ⇒ **11 个 range**（每块 319 行）；方法：A) 完整扫描快照（66,305,614 条 / 11.3 s）；
B) 二分预算 `pruneToFit` 保留 k 个 range + 把被丢的 range 用 `mergeRegion(rect,{})` 以 0 条目重新登记（= 旧实现语义，
全程不 load/save ⇒ 清扫不介入）；C) `saveFile`+`loadFile` 复测。

| 组 | 保留 | 命中率实测 | 线性预期（**面积口径**） | 假阳性 | 被重登记带的抽样率 | 修复后 |
|---|---|---|---|---|---|---|
| B0 | 10/11（丢的是 10 行尾块 ⇒ 面积保留 99.68%） | **99.82%** | 99.68% | **0.000%** | 1 带 3.88% | 11 → **10**，条目不变，命中 **100.00%** |
| B1 | 7/11（个数 63.6% ⇒ **面积 69.8%**） | **69.86%** | 69.8% | **0.000%** | 4 带 0.00/0.15/0.24/0.28% | 11 → **7**，条目不变，命中 **100.00%** |

**判定**：线性对应成立 ⇒ ④′ 闭环；假阳性恒 0 ⇒ 与真实缓存形态一致；被重登记带只剩 **0.15~0.28%**（= 4 行重叠带
/319 行 ×10% ≈ 0.13%）与真实缓存 #5/#6/#7 的 0.05% 同源；修复后清扫**恰好**剔掉无支撑声明且条目数一字不变。
**口径提醒（新增 backlog B-10）**：线性口径按**面积/条目比例**，不是 range 个数（B1 个数 63.6% 与实测不符）。

---

## 1. 项目与纪律（不变的前提）

- 仓库：`D:\DeepSeek harness\史莱姆区块查找器\SlimeFinder\`（C++20 / Qt 6.11.2 / MSVC 14.51 / CMake+Ninja）
- 构建：`cmake --build build`（需先 `vcvars64.bat` 导入环境；cmake 在 `D:\Visual Studio\...\CMake\bin`，ninja 在 `C:\Qt\Tools\Ninja`）
- 发布：`scripts\package_release.ps1 -Version <ver>`；验证：`scripts\verify_release.ps1`；审计：`py -3 scripts\audit_corruption.py`
- 长期约束：**一次一动、动完即验**；不做完必须明说；不改验收口径；严格模式 111/111 必须逐位一致；
  `.ps1`/`.cmd` 必须 ASCII（PowerShell 5.1 按 ANSI 读 .ps1）；禁止用 PowerShell `String.Replace` 批量改产品文件；
  **不要**在含 `core/` 子目录的位置编译临时探针（会命中旧头文件副本，踩过 `chunkedPath 不是成员` 的坑）。
- 发布地址：`https://wwben.lanzoul.com/b01d77eebe`，**密码 `3o8i`**（需提醒用户）。

## 2. 用户已拍板的决策（不再讨论）

| 事项 | 决策 |
|---|---|
| ④′ 修复方案 | **B：range 声明与条目同步裁剪**（不改格式、不改 pruneToFit 淘汰策略、不改收集逻辑） |
| 改动 1 的登记条件 | **简单方案**：`so.newCacheChunks` 非空才登记；不上「或已被覆盖」的替代条件 |
| 改动 3 清扫时机 | **加载时 + 保存前各一次**（同一函数两个调用点） |
| 改动 4 追加断言 | **加**「并发收集」断言（锚定 so 块内局部 + per-OpenMP-thread 收集） |
| 历史缓存（任务 2） | **方案 A**：改名 `.legacy`（保留 7 天）→ 加载提示「可能漏存 ~34%，建议重扫」→ **默认不用于跳过判定** → 7 天自动清理 |
| 候选 ②（762 vs 758） | 等任务 3 后再选，**预判倾向「登记域缩到 758」** |
| 位图化（v2） | 降级为「体积 + 加载优化」（覆盖区加速上限 **1.37×**） |
| 性能靶心 | **73% 的密度滑窗**（非 27% 的填充） |

## 3. 问题定性（已坐实，勿重复论证）

**④′ = 跨运行的「range 声明比条目新」**
`pruneToFit` 整 range 丢条目 → 之后**跨运行**再次登记同一个 `sub.region()`（该块 0 新增条目，因为其行大多已命中缓存）
→ `m_ranges` 留下**无条目支撑的声明** → `covers()` 为 true、`lookup()` 恒返回 0（「已知非史莱姆」）
→ 命中区被判为非史莱姆 ⇒ **漏 34%、0 假阳性**。

**实锤证据（逐 range 抽样，`temp_range_rate.exe`）**：12 个 range 全部 `209715×762`、面积各 1.598 亿——
**8 个 ≈10.0%（正常）**、**4 个 ≈0.05~0.10%（空壳：#4/#5/#6/#7）** ⇒ 双峰；
总率 10% × 8/12 = 6.67%，实测 **6.69%** ✓；E2：`agree=96.67%`、`cacheOnlySlime=0`（只漏史莱姆，不产假阳性）✓
小范围 200×200 完全复现不出（无预算压力、无跨运行）——**规模相关**特性。

> **⚠ §0A 修正（本轮实测，务必按此理解）**：那 4 个「空壳」里**只有 #4 是真正的 0 条目**；
> #5/#6/#7 各有 ~84k 条（= 4 行重叠带 × 209715 列 × 10%），**仍被 `covers()` 当覆盖**，其 758 行无支撑 ⇒ 漏判 99%。
> 因此「④′ = 0 新增条目」只是**特例**；一般形态是「**声明矩形的绝大部分没有条目支撑**」，
> 判据已按此升级为「支撑度 ≥ 期望 × 50%」（见 §0A）。

**改动 1 的并发安全性（已取证，成立）**：
`SlimeEngine.cpp:2319` `SearchOutcome so;`（runChunkBody 函数体局部）→ `2467` `runChunkBody(ci)`（每 worker 每块各一份）
→ `855` `out = SearchOutcome();`（重置的是线程私有对象）→ `1251 → 1290`（收集先进 per-OpenMP-thread 的
`threadNew[tid]`，并行区结束后单线程合并进 `out.newCacheChunks`）⇒ 检查读的是私有对象，**无竞态**。

## 4. 执行清单（**任务 1 全部完成**；下表 ✅ = 已完成）

| 步骤 | 动作（含锚点） | 验证 | 状态 |
|---|---|---|---|
| **改动 1** | `src/core/SlimeEngine.cpp`：`runChunkBody` 缓存归并块内 `pendingRanges.push_back(sub.region());` → **仅当 `so.newCacheChunks` 非空才登记**；空则 `perfHollowSkips++`（`search.summary ... hollowSkips=N` + `cache.hollow` 日志） | `temp_case20.exe` + `temp_e2.exe` | ✅ |
| **改动 2** | `src/gui/MainWindow.cpp`：`onSearchFinished` 的 `m_cache->mergeRegion(info.region, out.newCacheChunks);` → 同一规则（空则不登记 + `Logger`/`IncidentLog` 各一行） | 同上 + GUI 自检 | ✅ |
| **改动 3** | `src/core/CacheManager.h/.cpp`：`int64_t dropHollowRanges() noexcept`（**判据 B：声明内条目数 ≥ 期望×50%，期望 = 面积×10%，期望<100 条放行**；两趟扫描）；调用点 = `loadFile()` 末尾 + `saveFile()` 开头；`saveFile` 转非 const | 1.02 GB 缓存剔除检查 + E2 | ✅ 正好剔 4 个 ⇒ 8 range / 127,842,361 条目不变；E2 100.00% |
| **改动 4** | `tests/test_slime.cpp` 新组 `[20] 覆盖声明支撑度 + 并发收集`：A) 旧行为锚点 3 条 + 修复后 `covers()==false`/`lookup()==-1`；B) 真实形态（4 行重叠带声明被剔、密集声明保留、条目不变）；C) 小面积放行；D) **改动 1 的确定性验证**（缝隙列全非史莱姆 ⇒ 0 新增条目 ⇒ 不登记）；E) **并发收集**（threads=4：无重复 + 与真值逐条一致） | `[20]` + E2 + **111/111** | ✅ 20 组全绿 / 111 项全绿 |
| **任务 2** | 历史缓存方案 A：`.legacy` 改名（保留 7 天）+ 加载提示 + 默认**不用于跳过判定** + 到期清理 | 提示出现 / 不用于判定 / `.legacy` 可回滚 | ✅ 见 §0B（自检 12.8 段全绿） |
| **任务 3** | 验证实验一组：全宽 × 3160 行（11 range），预算设「保留 7/11」 | 预期 `agree ≈67%`；**线性对应→闭环；不线性→停下报告** | ✅ 实测 69.86% ≈ 面积口径 69.8% ⇒ **线性闭环**（见 §0B） |
| **v2 格式**（任务 3+5 第 2 步） | CacheManager 位图化：内存 = 每 range 位图 + outlier 坐标；v2 文件（24 B 头 + 每 range 矩形/位图 + outlier 段）；v1 兼容读；.new + 回读校验 + ReplaceFileW 原子替换；dropHollowRanges 改 popcount | 真机：**152.4 MiB ≤250 MiB**、**加载 251 ms ≤1.5 s**、一万点 100.00%；单元 [21] | ✅ 已完成 |

**停手条件（仅这六种）**：① 改动 1/2/3/4 任一验证失败；② 111/111 不一致；③ 1.02 GB 缓存剔除结果不是 #4/#5/#6/#7
（**本轮一度触发 → 已由用户拍板判据 B 解决**）；④ 任务 3 的 agree 与保留比例不线性；⑤ 上下文不足（写交接）；
⑥ 发现改动范围外的严重问题（记录到 backlog，继续）。

## 5. 工具与命令（**已整理入仓**：`SlimeFinder\tools\probes\` + `SlimeFinder\scripts\`）

编译参数（**必须**）：`cl /nologo /std:c++20 /Zc:__cplusplus /O2 /EHsc /utf-8 /permissive- /W3 /DNOMINMAX /MD /openmp /I<repo>\src /Fe:<out> <src.cpp> <repo>\build\slime_core.lib`
（探针编译：`SlimeFinder\tools\probes\temp_build_probe.cmd <name>`，需先设 `REPO_SRC`/`REPO_LIB`；
一键构建：`SlimeFinder\scripts\build_repo.cmd` —— 导入 MSVC 环境 + `cmake --build build`，可带额外参数；
曾经的工作区 `temp_*.ps1`/`temp_*.cmd`/`temp_shots/`/`temp_ab_work/` 等草稿已清理，见 §9）

| 工具 | 作用 | 当前基线数据（本轮实测） |
|---|---|---|
| `temp_case20.exe` | [20] 缓存完整性（200×200，开缓存→存→清→载→逐点真值比对）；需 `SLIME_FORCE_CHUNKED=1` 跑分块路径 | 缺失 0、一致率 **100%**（经典与强制分块均 100%） |
| `temp_e2.exe <cache>` | 一万点真值比对（`SlimeEngine::isSlimeChunk`） | **100.00%**（修复前 96.67%）✓ 目标达成 |
| `temp_range_rate.exe <cache>` | 逐 range 抽样率 | 清扫后 **8 range**（8×~10%），entries 127,842,361 不变 |
| **`temp_owned.exe <cache>`（新）** | **独立解析文件**（不走产品代码）：逐 range owned（首个包含）/ anyCnt（任意包含）/ 密度 | 12 range：8×~10% + **#4 owned=0** + #5/#6/#7 owned≈84k（0.05%） |
| **`temp_band.exe <cache> [N]`（新）** | 逐 range 真值比对：命中率 / 漏判率（定位毒带） | 判据 B 后：8 个声明 **100.00%**（旧#3 99.49%）；合计漏判 0.06%、一致率 99.99% |
| `temp_dbg.exe`（新） | 跨运行 / 预算扫描诊断（每轮 ranges/entries；`SLIME_DEBUG=1` 看每块 newcache） | 用于定位 `SLIME_CHUNK_WORDS` 下限等几何细节 |
| `temp_cache_probe.exe <cache>` | 加载耗时 / 查表 ns / 几何 | 加载 5,291~5,635 ms（旧）、查表 523 ns/点 |
| `scripts\gui_selftest.ps1`（原工作区 `temp_gui_selftest.ps1`） | GUI 自检运行器（`-Shot -FontDir -Platform -GpuOn -LogDir`） | **exit 0（16~20 s）**；12.7 缓存端到端 6811/6811 条目、65536 区块 0 不一致 |
| `scripts\build_repo.cmd`（原工作区 `temp_build.cmd`） | 导入 MSVC 环境 + `cmake --build build`（ASCII，路径用 `%~dp0` 相对） | `build_repo.cmd [额外参数]` |
| `tools\probes\temp_build_probe.cmd`（新） | 探针编译（需环境变量 `REPO_SRC`、`REPO_LIB`） | `temp_build_probe.cmd temp_e2` |
| **`temp_task3.exe`（新）** | **任务 3 尺度实验**：完整快照 → 二分预算剪枝 + 0 条目重登记（预修复态）→ 命中率/假阳性/逐 range 双峰 → 落盘重载复测 | 结果存档 `docs\evidence_v338\task3_result.txt`（B1 命中 69.86% ≈ 面积 69.8%；清扫后 100%） |
| **`temp_e3.exe`（新）** | **任务 6 / E3 四档实验**：只填充 / 经典 / 2 MB 位图 / 20 MB 位图，读 `stats.fillMs/slideWindowMs` 与 `search.summary` 契约 | 结果存档 `docs\evidence_v338\e3_result*.txt`（填充 47% / 滑窗 53%；位图缩小无收益 +3.2%） |
| **`temp_ab_fillskip.exe`（新）** | **填充跳过 A/B**：空缓存 / 整区命中 / 关缓存三场景对比（墙钟 · fill · slide · 跳过点数 · 结果一致性） | 结果存档 `docs\evidence_v338\fillskip_ab_result.txt`（**223 → 22 ms = 10.22×**，fill 201 → 2 ms） |
| **`temp_v2accept.exe`（新）** | **v2 格式验收**：读 v1 → 报加载耗时/声明/条目/位图 → 存 v2（报体积）→ 重载（报耗时）+ 两次一万点真值比对 | 真机：v1 5,965 ms → v2 **152.4 MiB / 加载 251 ms**，两次一致率均 **100.00%** |
| `temp_cache_dbg.exe`（新） | 缓存存/取单步诊断（每步返回值与 err） | 本轮用于定位 v2 读取器「矩形与位图交错」解析缺陷 |

用户真实缓存样本：`SlimeFinder/dist/SlimeFinder/slime_cache.slimecache`（**1,022,739,100 B**，SHA256 `B271401…7B93F`，加载 6,591~7,290 ms）；
打包脚本按设计会清掉 dist 下的 `*.slimecache`，本轮**已从备份还原**（备份：`C:\Temp\slimecache\backup\real_cache_20260921.slimecache`，同 SHA256）。

## 6. 已完成项（v3.3.7.2 已交付 + v3.3.8 已完成部分）

- **v3.3.7.2 已发布**：F-1 截图豆腐块修复 + 断言；UI 四项（单一百分比 / 分母同源 / 状态栏分隔 / 阶段文案按模式分派）；
  P1-D 日志事件（`chunk.*`/`cache.prune`/`cache.admit`/`cpu.sample`/`mem.peak`/`search.summary`）；
  P0-C 摊还归并 + 批量剪枝 + 准入控制；F-4 宽扁范围块内多线程；F-3 完成态。
  **439.8 亿 A/B：墙钟 837.3→444.8 s（1.88×）、归并 521.6→12.3 s、剪枝 294.8→0.96 s、CPU 60→91%、内存峰值 3777→2307 MB**。
  未达标项：速度 99.4M/秒 < 3 亿/秒（已给理论依据：20 MB×12 位图访存局部性）。
  回归：unit 19 组 / 111/111 / 内核 17/17 / GUI 开与关 / 审计 CLEAN(167)。
- **v3.3.8 本轮已完成**：任务 1（ETA）已实施并验证（搜索中 `预计剩余：0:08`、完成后 `--:--`，含两条自检断言），
  **尚未打包**；任务 2 七问审查、任务 3+5 设计文档（三份）、E2 / 微基准 / 逐 range 双峰取证、方案 B 改动范围与四问回答。
- **v3.3.8 本轮（任务 1 方案 B）已完成**：改动 1/2/3/4 代码实施（`SlimeEngine.cpp` 登记门槛 + `MainWindow.cpp` 同一规则 +
  `CacheManager::dropHollowRanges` 支撑度判据与两个调用点 + `PerfLog.h` 契约 + `tests/test_slime.cpp` 新组 `[20]`）；
  回归 **单元 20 组 / 111/111 / GUI 自检 exit 0 / `[20]` 100% / E2 100.00% / 1.02 GB 清扫后 8 range 且条目不变**；
  期间一次停手（条件 ③）由用户拍板判据 B 解决（详见 §0A 与 `docs/v338_progress.md` 三-f）。
- **v3.3.8 任务 2 已完成**：历史缓存方案 A（`MainWindow.cpp` 迁移函数 + 启动接线 + 提示 + `.legacy` 回滚 + 7 天清理 +
  自检 12.8 段；用户手册 §1 同步更新）——见 §0B。
- **v3.3.8 任务 3 已完成**：④′ 尺度验证实验（`probe20\temp_task3.exe`，6.63 亿区块 / 11 range）⇒ **线性对应成立、
  假阳性 0、清扫后 100%** ⇒ ④′ 闭环 —— 见 §0B。
- **新增 backlog**：B-9（重叠带 4 行支撑检测不到，需 v2 按行/位图记录）、B-10（`pruneToFit` 预算-保留量台阶过陡；
  线性口径按面积而非 range 个数）、B-11（`.legacy` 手动导入的二次确认）。

## 8. v3.3.8 已打包（发布收口完成）

| 项 | 值 |
|---|---|
| 发布包 | `dist/SlimeFinder-3.3.8-win64.zip`（**19142980 B**，SHA256 `9B73418538A67D3A29FEF5508F3DE965165B14A124AD9C9DA3E22F3D2FAF0DC6`） |
| 校验文件 | `dist/SHA256SUMS-3.3.8.txt`（4 个 exe + zip） |
| exe 版本资源 | `FileVersion=3.3.8 / ProductVersion=3.3.8`；二进制内含 `v3.3.8` 字符串（打包脚本已断言） |
| 包内自检 | GUI 自检 exit 0 / 单元组 15 exit 0 / 内核自检 exit 0 |
| 版本号同步点 | `src/app.rc`（`APP_VERSION "3.3.8.0"`，4 段式 —— 审计脚本按「去掉最后一段」推导 3.3.8）、`src/main.cpp`、`src/gui/MainWindow.cpp`、`CMakeLists.txt`、`scripts/install.nsi`、`README.md`、`docs/*`、打包/验证脚本 |
| 全量验证 | `scripts\verify_release.ps1 -Zip dist\SlimeFinder-3.3.8-win64.zip` ⇒ **total=16 pass=16 fail=0**（clean 重建 / 单元 / 111/111 / OpenCL / 内核 / GUI 开与关 / 基准 / dist 与 zip 哈希 / 包内自检）；证据 `docs/evidence_v338/verify_release_v338.txt` |
| 污染审计 | `py -3 scripts\audit_corruption.py` ⇒ **CLEAN**（exit 0） |
| 发布地址 | `https://wwben.lanzoul.com/b01d77eebe`，密码 `3o8i`（**需提醒用户**） |

## 9. 文件夹整理（本轮收尾）

**工作区根目录现在只有三项**：`SlimeFinder/`（仓库）、`_v311_ref/`（`scripts/audit_corruption.py` 依赖的 v3.1.1 参考树，**不可删**）、
`模板：原始文件/`（参考实现 `SlimeChecker.exe` 与历史任务书，`tests/verify_against_reference.py` 依赖，**不可删**）。

| 动作 | 内容 |
|---|---|
| 归档证据 | 验证/实验输出 → `docs/evidence_v338/`：`verify_release_v338.txt`、`reference_111_*.txt`（各轮 111/111）、`gui_selftest_v338_stderr.log`、`task3_result.txt`、`e3_result*.txt` |
| 工具入仓 | 探针源码与编译脚本 → `SlimeFinder/tools/probes/`（10 个文件，含 `temp_build_probe.cmd`）；`temp_gui_selftest.ps1` → `SlimeFinder/scripts/gui_selftest.ps1`；一键构建 → `SlimeFinder/scripts/build_repo.cmd` |
| 删除草稿 | 工作区 `temp_*.png`（v3.3.7.2 期中间截图）、`temp_ab_work/`、`temp_shots/`、`temp_ab_*.ps1/txt`、`temp_build_v3372.log`、`temp_cache_bench.cpp`、`temp_cache_run.cmd`、`temp_chunked_bench*`、`progressrow_repro.png` |
| 清理运行环境 | 删除 `C:\Temp\slimecache\core\`（v3.1.1 期的旧头文件副本 —— 曾在其中编译探针踩过「chunkedPath 不是成员」的坑） |
| 数据 | 用户 1.02 GB 缓存：打包脚本按设计清空了 dist 下的 `*.slimecache` ⇒ **已从备份还原**到 `dist/SlimeFinder/slime_cache.slimecache`（v3.3.8 首次启动会把它改名 `.legacy` 并提示，符合方案 A 设计）；备份仍在 `C:\Temp\slimecache\backup\` |

## 10. 相关文档索引

| 文件 | 内容 |
|---|---|
| `docs/v338_progress.md` | **主进度**（三-a ~ 三-i：ETA / 七问 / 方案 B / 判据 B / 任务 2 / 任务 3 / E3 各阶段结论与决策） |
| `docs/verification_report_v338.md` | **v3.3.8 验证报告**（结论速览 + 逐项证据 + 回归表 + 未完成项） |
| `docs/v338_fixB_scope.md` | ④′ 修复方案 B 的完整代码改动范围（含验收与风险） |
| `docs/v338_handover.md` | 本文件（冷启动自足版交接） |
| `docs/v338_eta_and_cache_audit.md` | ETA 根因 + 缓存七问（逐条附行号） |
| `docs/v338_cache_bitmap_design.md` + `_addendum.md` + `_addendum2.md` | v2 位图化设计与两轮修正（含 E3 判据与原子替换流程）—— **下一阶段主线** |
| `docs/v338_backlog.md` | 11 条相邻问题（含本轮新增 B-9~B-11） |
| `docs/evidence_v338/` | 本轮验证与实验证据（verify/111/gui/e3/task3） |
| `docs/perf_compare_v3372.txt`、`docs/verification_report_v3372.md` | v3.3.7.2 性能与验证存档 |
