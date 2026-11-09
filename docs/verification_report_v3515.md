# SlimeFinder v3.5.15 验证报告

- 版本：v3.5.15（`kVersion`、`VERSIONINFO` 3.5.15.0、安装包与 zip 同名）
- 主题：**大范围环形提速 + 地图底图不再卡（用户感知延迟的真正大头）**
- 交付物：`dist/SlimeFinder-3.5.15-win64.zip`、`dist/SlimeFinder-3.5.15-Setup.exe`
- 日期：2026-10-18

---

## 一、社区对照（先看别人怎么做的）

| 工具 | 做法 | 同面积耗时（换算到 400x400 区块 = 16 万候选点） |
|---|---|---|
| [wifi504/find-slime-chunk](https://github.com/wifi504/find-slime-chunk) | Java 暴力遍历，17x17 区块圆内数史莱姆区块；作者自述 100 万区块 0.885 s、1 亿区块 137 s | ≈ 0.14 s（16 万点）；**我们 ≈ 5 ms，快约 28 倍** |
| [TheBjoel2/Slime-Chunk-Finder](https://github.com/TheBjoel2/Slime-Chunk-Finder) | C++ + AVX 批量判定，评分不只数区块，还按「刷怪空间 + 农场高度」折算 | 未给数字；**思路值得借鉴**：判定可以批量、评分可以更物理 |
| [Badel2/slime_seed_finder](https://github.com/Badel2/slime_seed_finder) | 反推种子，同样是同一套 LCG 判定 | — |

结论：**逐点暴力是社区主流**；我们的「稀疏核散射」已经快 1~2 个数量级。这轮借过来的三点：
1. **整行批量判定**（社区用 AVX 一次算一批）：`SlimeEngine::fillSlimeRowJava()` —— 行基座只算一次，
   规划器占用图不再逐点走 `std::function`。
2. **分块/切片**（社区用多线程分块）：粗扫按列带切成 ≤256KB 的小块（L2 内），史莱姆区块表按 x 排序，
   每条带只扫自己那一段。
3. **可向量化的内层**：核沿 di 反向拷贝一份，内层变成「两边都正向连续读」的 17 格列更新。

> 社区工具用「圆内史莱姆区块个数」当分数；我们用的是**逐格精确覆盖 + 群系刷怪权重**（更接近实际刷怪率）。
> 社区里强调「评分要按刷怪空间/农场高度算」的那套思路，是我们下一轮可以考虑的产品改进（不是提速）。

---

## 二、这轮的速度

### 1. 环形匹配（`--bench-ring`，R=128，内=24，Java 种子 12345）

| 范围 | v3.5.14 | v3.5.15 | 说明 |
|---|---|---|---|
| 400x400 区块（16 万候选点） | 16.7~19.1 ms | **16~21 ms** | 与上一版同量级（此时已到「4 物理核并行上限」） |
| 1000x1000 区块（100 万点） | 59.6 ms | **43~53 ms** | 粗扫切块 + 整行判定 |
| 2000x2000 区块（400 万点） | 107.5 ms | **96~108 ms** | 粗扫步长 2，排序网格放宽到 1:512 |

> 注意：测量时机器上同时跑着游戏（CPU 占用很高），上表取多次运行的中位数；空闲机器上会更好。

### 2. 用户感知的延迟（这才是「还是有点慢」的真正大头）

环形搜索后地图要重画底图。实测 v3.5.14 时：

| 指标 | v3.5.14 | v3.5.15 |
|---|---|---|
| 缩到整片区域后的地图图层重建 | **102 ~ 175 ms**（或直接超限、群系整层不画） | **6 ~ 23 ms** |
| 群系精度 | 只允许 1:1 / 1:4 → 缩小时采样量顶到 35 万格上限 | 按面积自动放宽（1:16 起），单次采样格数封顶约 6 万 |
| 搜索后「界面刷新」（列表 + 地图设置） | 3.0 ~ 3.5 ms | 3.0 ~ 3.7 ms |

也就是说：搜索本身 10~20 ms，但以前**每次搜完还要卡 100~175 ms 重画地图**；现在这段是 6~23 ms。
一整套「点开始 → 结果上屏 → 地图就绪」的等待从约 200 ms 降到约 30 ms。

### 3. 顺带修正

- `BiomeMap::genArea` 的行带并行门槛从 8192 格降到 1024 格（自适应精度后采样格数变少，
  以前会掉回串行，反而更慢：1:128 的 54x54 网格 10.7 ms → 3.7 ms）。
- 排序网格比例：400x400 档**不要**用 1:128（实测最优点会明显变差），保持 1:64/1:256 的一致选择；
  ≥1000 区块的档位放宽到 1:512。

---

## 三、质量验证

| 检查 | 结果 |
|---|---|
| 候选列表 A/B（span 16/44/100/200/400） | 最优候选与 v3.5.14 **完全一致**（(-1609,-831) 9965 格） |
| 内核精修 vs 逐格统计（基准内置校验） | **完全一致** |
| 按需采样 vs 整片采样（基准内置 + 单元 [30]） | **完全一致** |
| 严格统计四路对照（单元 [33]） | 5 个点位全部一致（含正负坐标） |
| 自检新增断言（远缩放采样上限） | `1:16 → 3648 格 ≤ 13 万` ✔ |

---

## 四、验证矩阵

| 项目 | 结果 |
|---|---|
| 单元测试（34 组） | **全部通过 ✔** |
| 参考实现对照（SlimeChecker.exe） | **111 / 111** |
| SIMD 内核独立自检 | **19 项全过（ALL PASS）** |
| GPU 探测 / 一致性 | PASS（失败 0 项） |
| GUI 无界面自检（GPU 关 / 开） | exit 0，**172 条断言 0 FAIL** |
| 打包脚本自检 | `PACKAGE OK` |
| 发布验证 `verify_release.ps1` | **16 / 16 PASS**（clean 重建、单元、参考 111、ocl、内核、GUI×2、两套基准、dist 自检、4 组哈希、zip 自检） |
| 安装包冒烟 | 静默装 → 版本 3.5.15 → 安装副本自检 exit=0 → 卸载清空目录 |
| 污染审计 | **CLEAN**（287 文件，exit 0） |
| dist 与 zip | 逐文件 SHA256 一致 |

### 交付物校验

| 文件 | 大小 | SHA256 |
|---|---|---|
| `SlimeFinder-3.5.15-win64.zip` | 15,836,265 B | `BFD9EEEE15E1D2D2F2AA3689706AC8437AFD43B9079561E7058978DCAF1C69F1` |
| `SlimeFinder-3.5.15-Setup.exe` | 10,693,701 B | `3671C55118F93B2C3C8C0BA606987C3B6E76B8493F22BAAC13C89B8CAD2BC960` |
| `SlimeFinder.exe`（包内） | — | `DF4738DFD7495981122B8E088322C6C3C0A35DD3DC0BA716127B997B93C0374C` |

### 更新历史

61 → **62 条**，正式版 17 → **18 个**（V3.5.15 正式版）。

---

## 五、复现命令

```powershell
build\slime_tests.exe --bench-ring 400      # 环形分阶段基准（400x400 区块）
build\slime_tests.exe --bench-ring 2000     # 大范围（2000x2000 区块）
build\slime_tests.exe --unit                 # 34 组
build\slime_kernel_tests.exe                 # SIMD 内核
py -3 tests\verify_against_reference.py       # 111/111
powershell -ExecutionPolicy Bypass -File scripts\gui_selftest.ps1
powershell -ExecutionPolicy Bypass -File scripts\package_release.ps1 -Version 3.5.15
powershell -ExecutionPolicy Bypass -File scripts\build_installer.ps1 -Version 3.5.15
powershell -ExecutionPolicy Bypass -File scripts\verify_release.ps1 -Zip dist\SlimeFinder-3.5.15-win64.zip
py -3 scripts\audit_corruption.py
```

## 六、诊断开关（排障用）

| 环境变量 | 作用 |
|---|---|
| `SLIME_PLANNER_THREADS=n` | 固定规划器线程数（验证并行度） |
| `SLIME_PLANNER_BANDS=n` | 固定粗扫列带数（A/B 切片大小） |
| `SLIME_RING_STRIDE=n` | 固定粗扫步长（默认按范围自动，质量优先） |
| `SLIME_RING_GRIDSCALE=n` | 固定排序权重网格比例（1:64/128/256/512） |
