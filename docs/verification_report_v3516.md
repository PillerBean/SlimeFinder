# SlimeFinder v3.5.16 验证报告（测试版）

- 版本：v3.5.16（`kVersion`、`VERSIONINFO` 3.5.16.0、安装包与 zip 同名）
- 版本策略（用户要求）：**3.5 之后的所有版本一律按测试版（◆测试版）**，正式版只有 2.8.4 / 3.1.1 / 3.3.0 / 3.5.0
- 主题：地图缩放不同步修复 · 单一半径范围 · 环形范围解除 4096 限制 · 地图中心不再被范围拖动 · 最小密度复核
- 交付物：`dist/SlimeFinder-3.5.16-win64.zip`、`dist/SlimeFinder-3.5.16-Setup.exe`

---

## 一、修了什么

| # | 用户反馈 | 原因 | 处理 |
|---|---|---|---|
| 1 | **缩放时鬼畜**：群系底色与网格不同步（只平移正常） | 缓存的合成底图按「生成时的比例」1:1 贴图；缩放后网格/环/结果是按新比例实时画的，两份不同步 | 缓存底图改为**按当前缩放拉伸**到对应方块矩形（`drawLayerPixmap`），旧图/新图都走同一条路径 |
| 2 | 环形搜索范围仍限制在 4096 内 | 规划器有 `spanX*spanZ > 4096²` 的硬上限，且占用图一次进内存 | 上限改为「4 亿区块」兜底；**粗扫改为按 z 带流式处理**（内存与范围无关），精修/抛光改为按需取 (2k+1)² 局部窗口 |
| 3 | 调整搜索范围会让地图中心「同步」而错位 | `updateMapFromForm` 每次范围变化都把视图移到范围中心 | 只在**首次**显示地图时按范围定位，之后改范围不再移动视图（新增 `m_mapViewInitialised`） |
| 4 | 部分结果密度低于设定的最小密度 | `plan()` 用近似覆盖率筛，严格重算后的真实密度可能低于阈值 | 严格统计后**按最小密度重新复核**，并按目标数量截断 |
| 5 | 范围要由 4 个数改成一个半径（正方形边长 = 2×半径） | 原界面是 minX/maxX/minZ/maxZ 四个输入框 | 搜索页与缓存构建页都改为**只填半径**（`setSearchRadius`），半径 r ⇒ X/Z 各 ±r；快速预设同步；四个旧输入框保留为内部状态（引擎/预估/构建仍读它们） |

## 二、环形 vs 正方形：慢多少倍

同一片区、同一种子（12345）、同样只找 8 个结果（`--bench-square` 新增基准 vs `--bench-ring`）：

| 范围 | 正方形（密度网格 g=5） | 环形（R=128，内 24） | 倍数 |
|---|---|---|---|
| 400×400 区块（16 万） | **4.0 ms** | **18.9 ms** | **≈4.7×** |
| 1000×1000 区块（100 万） | **8.6 ms** | **38.7 ms** | **≈4.5×** |

结论：**环形比正方形慢约 4.5~4.7 倍**。差距来自环形多做的事：每个候选都要 256 个 1 格站位的环带交集（16x16 子偏移核）、前 8 名 3×3 区块抛光、以及 8 个候选的 1:4 群系权重逐格严格统计；正方形只是按密度网格数窗口。

## 三、速度与内存（v3.5.16）

| 场景 | 结果 |
|---|---|
| 400×400 区块环形（GUI 端到端） | 0.008 ~ 0.013 s |
| 1000×1000 区块环形 | 38 ~ 41 ms |
| 2000×2000 区块环形 | 68 ~ 108 ms |
| 环形大范围内存 | 常数级（每个 z 带 ~24MB，与范围无关） |

## 四、验证矩阵

| 项目 | 结果 |
|---|---|
| 单元测试（34 组） | **全部通过 ✔** |
| 参考实现对照 | **111 / 111** |
| SIMD 内核自检 | **19 项全过（ALL PASS）** |
| GPU 探测 | PASS（失败 0 项） |
| GUI 自检（GPU 关 / 开） | exit 0，**172 条断言 0 FAIL** |
| 更新历史正式版 | **4 个**（2.8.4 / 3.1.1 / 3.3.0 / 3.5.0）；3.5 之后一律测试版 |
| 打包 | `PACKAGE OK`（包内 GUI/单元/内核自检 exit=0） |
| 发布验证 | **14 / 14 PASS**（`-SkipBench`；含 clean 重建、单元、参考、ocl、内核、GUI×2、dist 自检、4 组哈希、zip 自检） |
| 安装包冒烟 | 静默装 → 3.5.16 → 安装副本自检 exit=0 → 卸载清空 |
| 污染审计 | **CLEAN**（288 文件，exit 0） |
| dist 与 zip | 32 条目逐文件一致 |

### 交付物校验

| 文件 | 大小 | SHA256 |
|---|---|---|
| `SlimeFinder-3.5.16-win64.zip` | 15,832,610 B | `72970E97BBE63F0D7F3D799329E3E837036BF0DA3EF9C39FFE9D3110E8B447EA` |
| `SlimeFinder-3.5.16-Setup.exe` | 10,693,535 B | `970256DC1F99955F5CD050694A398D273F2F39712096279B29444BAA19351A5A` |
| `SlimeFinder.exe`（包内） | — | `2CA419EF4266FD857244CEFFF6D2242EDF7C71D3109CAE4F639074E60A21CAD1` |

### 更新历史

62 → **63 条**；正式版恒为 **4 个**（3.5 之后全部标记为测试版）。

---

## 五、复现命令

```powershell
build\slime_tests.exe --bench-ring 400        # 环形（16 万区块）
build\slime_tests.exe --bench-square 400      # 正方形（同片区，密度 g=5）
build\slime_tests.exe --bench-ring 1000
build\slime_tests.exe --bench-square 1000
build\slime_tests.exe --unit                   # 34 组
build\slime_kernel_tests.exe
py -3 tests\verify_against_reference.py         # 111/111
powershell -ExecutionPolicy Bypass -File scripts\gui_selftest.ps1
powershell -ExecutionPolicy Bypass -File scripts\package_release.ps1 -Version 3.5.16
powershell -ExecutionPolicy Bypass -File scripts\build_installer.ps1 -Version 3.5.16
powershell -ExecutionPolicy Bypass -File scripts\verify_release.ps1 -Zip dist\SlimeFinder-3.5.16-win64.zip
py -3 scripts\audit_corruption.py
```
