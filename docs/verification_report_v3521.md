# v3.5.21 验证报告（测试版）— 基岩版群系上色

- 版本：v3.5.21（测试版）
- 主题：**基岩版地图群系上色**（此前显示「群系不可用」）

## 一、调研结论（社区逆向成果在哪）

官方 Wiki 的「World seed」条目写得很明确：

> All Java and Bedrock Edition seeds in the 64-bit range generate the **same terrain and biomes** in both editions.
> However, **structures, features, carver caves, and mob spawns** will generate differently.

也就是说 **群系不需要单独的基岩版算法**：1.18 起两版统一为多噪声生物群系源，同一种子出同一份群系。
社区工具正是这么做的：

- [cubiomes](https://github.com/Cubitect/cubiomes) 只实现 Java，但基岩版工具照样拿它当后端；
- [MZEEN2424/ChunkBiomesGUI](https://github.com/MZEEN2424/ChunkBiomesGUI)（基岩版 1.18+ 种子查找）README 明说基于 **Cubiomes**；
- [Chunkbase Seed Map](https://chunkbase.com/apps/seed-map) 闭源，同时支持两版，依赖的也是这条一致性；
- [Minecraft Wiki: World seed](https://minecraft.wiki/w/Seed_(level_generation)) 为官方口径来源。

**没有**公开逆向成果的是 **基岩版 1.17 及更早**的图层式群系生成（只有闭源实现），所以本项目对基岩版上色限定 1.18+。

## 二、实现

| 文件 | 改动 |
|---|---|
| `src/gui/MainWindow.cpp` | 基岩版不再传 `versionId = -1`；改用 Java 生成器 + 同一种子（版本口径固定 1.18+，默认 1.21.3） |
| `src/gui/MapDialog.cpp` | 去掉「基岩版 → 群系不可用」分支 |
| `src/gui/MapPreviewWidget.h/.cpp` | 新增 `setBiomeParityNote()`，日志标注「基岩版：按 Java 同种子群系」 |
| 自检 | 新增断言：基岩版地图预览 `biomeAvailable == true` |

## 三、验证

| 项目 | 结果 |
|---|---|
| 单元测试（34 组） | **全部通过 ✔** |
| 参考实现对照 | **111 / 111** |
| SIMD 内核自检 | **19 项全过** |
| GUI 自检（GPU 关 / 开） | exit 0，**173 条断言 0 FAIL** |
| 发布验证 | **14 / 14 PASS** |
| 打包 | `PACKAGE OK` |
| 安装包冒烟 | 静默装 → 3.5.21 → 自检 exit=0 → 卸载清空；日志确认「基岩版：按 Java 同种子群系」 |
| 污染审计 | **CLEAN**（298 文件） |
| dist 与 zip | 32 条目逐文件一致 |

### 交付物

| 文件 | 大小 | SHA256 |
|---|---|---|
| `SlimeFinder-3.5.21-win64.zip` | 15,850,953 B | `D563ED6467D2491A5BA0F659F7FB1930B52D78ABF1708297682ED92E02AD6297` |
| `SlimeFinder-3.5.21-Setup.exe` | 10,703,687 B | `962F30E70BAB4485CCF2E901CDE65095580EAEFAA48F967B837D057F2806BC0A` |
| `SlimeFinder.exe`（包内） | — | `A98FC791DF585E62C3BB165B640E8B58CC4F17DDDF29528F4E78F257FA689374` |

## 四、已知差异（不是 bug）

- 结构 / 地物 / 洞穴雕刻 / 生物生成在两版不同 —— 但这与群系颜色无关；
- 基岩版 1.17 及更早：群系算法不同，仍显示不可用；
- 颜色取的是地表（y≈63）群系，地下 3D 群系不参与上色（两版都如此）。
