# 基岩版群系上色：社区逆向成果调研与实现（v3.5.21）

## 一、结论（先看这里）

**同一 64 位种子，Java 版与基岩版的地形与群系完全一致**，只有结构、地物（装饰）、洞穴雕刻与生物生成不同。
这条结论来自官方 Wiki 的「种子」条目（"Overlap between editions"）：

> All Java and Bedrock Edition seeds in the range from −9,223,372,036,854,775,808 to
> 9,223,372,036,854,775,807 (64-bit seeds) generate the same terrain and biomes in both Java and
> Bedrock Edition. However, structures, features, carver caves, and mob spawns will generate differently.
> —— [World seed – Minecraft Wiki](https://minecraft.wiki/w/Seed_(level_generation))

所以基岩版地图上色**不需要另写一套群系算法**：用 Java 的生成器 + 同一个种子即可（1.18 起两版统一为
多噪声生物群系源）。这也正是种子地图网站能给出基岩版群系底图的原因。

## 二、社区/开源侧的证据

| 来源 | 说明 |
|---|---|
| [Minecraft Wiki: World seed](https://minecraft.wiki/w/Seed_(level_generation)) | 官方口径：同种子 → 地形与群系一致（结构/洞穴/生物不同） |
| [cubiomes](https://github.com/Cubitect/cubiomes) | 开源群系库，**只实现 Java**；基岩版工具仍拿它当后端 |
| [MZEEN2424/ChunkBiomesGUI](https://github.com/MZEEN2424/ChunkBiomesGUI) | 基岩版（1.18+）种子查找工具，README 明确「基于 **Cubiomes** 库 + Chunkbase 的筛选逻辑」 |
| [Chunkbase Seed Map](https://chunkbase.com/apps/seed-map) | 闭源，但同时支持 Java/基岩版——依赖的正是上面的群系一致性 |
| [基岩文档: 地形生成](https://miemiemethod.github.io/bedrock-docs/docs/general/terrain-generation/) | 基岩版 1.18+ 的群系分配同样由温度/湿度等噪声参数决定（多噪声），与 Java 同源 |

明确**没有**公开逆向成果的部分：基岩版 **1.17 及更早**用的是完全不同的图层式群系生成（Bedrock 自己的
生物群系地图算法），社区只有闭源实现（Chunkbase）。因此本项目对基岩版群系上色限定在 **1.18+**。

## 三、本项目怎么实现的

1. `MainWindow::updateMapFromForm()`：基岩版不再调用 `setBiomeSource(-1, …)`，改为用**当前 Java 版本**
   （默认 1.21.3，即与基岩版 1.21.x 对应）跑同一份种子；版本口径固定走 1.18+（选到 1.17 及更早会回落到最新）。
2. `MapDialog::initBiomeSource()`：同样去掉「基岩版→不可用」的分支。
3. 日志/状态标注来源：`[地图] 群系源：… → 可用（基岩版：按 Java 同种子群系）`，避免用户误以为读的是存档。
4. 自检新增断言：基岩版地图预览 `biomeAvailable == true`。

## 四、已知差异（不要误判为 bug）

- 结构（村庄/要塞/遗迹…）、地物（树木/花草分布）、洞穴雕刻、生物生成：**两版不同**，所以史莱姆区块
  也按各自规则算（本项目本来就分版本）。
- 基岩版 1.17 及更早：群系算法不同，本项目不提供上色（会显示不可用）。
- 群系颜色只是"地表群系"（y≈63 采样），与游戏内 F3 显示的地下群系可能不同——这是两版都有的采样口径问题。
