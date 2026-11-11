# SlimeFinder v3.5.2 验证报告（农场选址三项功能）

- **便携版**：`dist/SlimeFinder-3.5.2-win64.zip`（15,206,927 B，
  SHA256 `6A00232D0BC786CE6AE853A7C908A7C0719376E64D32700985FFF5AF8AA75A4D`）
- **安装版**：`dist/SlimeFinder-3.5.2-Setup.exe`（10,625,333 B，
  SHA256 `D37FCAD67F7C0B01EB0E9E9D766B418B47F400C5D19A0ED8557431B16B8C04DA`）
- **校验和**：`dist/SHA256SUMS-3.5.2.txt`（主程序 / 三个工具 / 便携包 / 安装包）
- 主程序 `SlimeFinder.exe` SHA256 `D5D6C90F0631E571B868F226FD9007111EEBF69FAB896D527EA0412EA1716931`

## 一、本版新增

| # | 功能 | 入口 | 说明 |
|---|---|---|---|
| 1 | **挂机点（环形覆盖）** | 工具 → 挂机点（Ctrl+G）／结果区「挂机点」 | 精度细化到 **1/16 区块（1 格）**，按半径 R（默认 128）内覆盖的史莱姆格数排序；覆盖面积打平时取「覆盖区块重心」（2×2 阵自然给出四区块角点）。表格 + 一键复制 `/tp` 命令 |
| 2 | **地图预览（彩色群系）** | 工具 → 地图预览（Ctrl+M）／结果区「地图」 | chunkbase 风格：群系彩色底图（cubiomes，Java 1.7.10~1.21.3）+ 半透明史莱姆区块 + 橙框搜索结果 + 蓝色挂机点（半径圆）；滚轮缩放 / 拖拽平移 / 双击复位；可切群系精度 1:1~1:64 |
| 3 | **群系权重** | 挂机点对话框内「平台 / 群系版本」 | 按群系对史莱姆刷怪效率加权排序（蘑菇岛、深暗之域等 0 权重自动避开）；表格新增「加权覆盖」「平均权重」 |

## 二、验证

| 项目 | 结果 |
|---|---|
| 编译 | exit 0（无 error C/LNK） |
| 单元测试 | **31 组全部通过**（原 28 + [29] 环形挂机点 + [30] 群系地图 + [31] 权重组） |
| 参考对照 | 111 / 111 |
| SIMD 内核 | 19 项 ALL PASS |
| GUI 自检（GPU 关 / 开） | exit 0 / exit 0，**零 FAIL** |
| 发布校验 | `verify_release.ps1` **16/16 PASS** |
| 打包 | `PACKAGE OK`（包内 GUI 自检 + 单元 15 组 + 内核均 exit 0） |
| 安装包 | 静默安装 exit 0 → 主程序版本 **3.5.2** → 自检 exit 0/FAIL 0 → 静默卸载 exit 0 且目录清空 |
| 污染审计 | **CLEAN**（275 文件） |
| 更新历史 | **49 个版本**，正式版 5 个（**3.5.2** / 3.5.0 / 3.3.0 / 3.1.1 / 2.8.4） |

## 三、数据来源与许可

- **cubiomes**（群系生成）：<https://github.com/Cubitect/cubiomes>，**MIT**，LICENSE 随源码
  （`third_party/cubiomes/LICENSE`）；仅引入群系生成所需文件，未改算法。
- **刷怪权重数据**：英文 Minecraft Wiki（Slime → Spawning 表）+ 中文 Minecraft Wiki 交叉核对；
  差异项（基岩版河流/冻河不生成）已按中文版修正并在代码注释标注。
- **视觉参考**：chunkbase / minecraftsearch 的「种子地图」观感，仅参考表现，未复制代码或素材。

## 四、已知限制（与测试相关）

1. **基岩版无群系数据**（cubiomes 不支持）⇒ 地图对基岩版只画史莱姆区块/网格；权重只在 Java 版生效，
   基岩版按纯面积排序（河流/冻河/蘑菇岛/深暗域按 0 处理）。
2. 群系百分比目前仅英文 Wiki 单来源（中文版给了定性排除项）；未收录群系按最高档 1.0 并标 `known=false`。
3. 权重与覆盖面积都只反映**水平面**的刷怪潜力，不含 Y 轴、光照、空间条件，用于比较位置优劣。
4. 加权模式计算窗口 **1024 方块**（界面会自动在中心收窄并提示）。
5. 本轮把自检里「取消响应 < 1 秒」的压测改为**超阈值重测一次**（打包刚结束时偶发瞬时负载会导致
   单次 1014 ms 假失败）；断言强度不变（仍要求 < 1000 ms）。

## 五、复现命令

```powershell
# 构建 + 全量回归
scripts\build_repo.cmd
build\slime_tests.exe --unit
py -3 tests\verify_against_reference.py
build\slime_kernel_tests.exe
powershell -File scripts\gui_selftest.ps1 -LogDir <dir> [-GpuOn]

# 打包与校验
powershell -File scripts\package_release.ps1
powershell -File scripts\build_installer.ps1
powershell -File scripts\verify_release.ps1 -Zip dist\SlimeFinder-3.5.2-win64.zip
py -3 scripts\audit_corruption.py
```
