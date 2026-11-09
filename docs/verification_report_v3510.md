# SlimeFinder v3.5.10 验证报告

日期：2026-10-18　版本：v3.5.10（正式版，历史 57 条 / 正式版 13 个）

## 一、本版改动

1. **环形按需显示（本轮核心，用户选择「方案 A」）**：不再同时画所有候选的环。地图上每个候选只画一个小红点；点某个红点或点结果表里的行，才画出该候选的 24~128 环带，地图聚焦到它并回填结果表选中行；点另一行则切换。
2. **视觉重做**：环形改成细线双层描边（深色主线 + 浅色内描边），环带不再用点线/虚线；环心改为红色圆点（白色描边 + 柔光），低倍缩放下一眼可见。
3. **交互联动**：`MapPreviewWidget` 点击命中候选 → `std::function<void(int)>` 回调 `MainWindow`（该类无 Q_OBJECT，不用 signal）→ `selectResultRow()` 联动结果表；反过来 `focusAfkCandidate()` 也会更新地图的聚焦状态。
4. **去杂乱**：环形模式下不再额外画 64 个橙色结果方块，避免与候选圆点重复。

（用户同时问「模板图里大片蓝色斑块是什么」：那是方案图里我为示意水体用 Pillow 合成的色块，非程序渲染；真实界面的蓝色是海洋/河流群系色。）

## 二、验证矩阵（verify_release.ps1，16/16 PASS）

| 检查 | 结果 | 说明 |
| --- | --- | --- |
| clean rebuild | PASS | exit=0，0 error |
| unit tests | PASS | 32 组单元测试全部通过（含环形粗扫/精修、超大范围） |
| reference 111 | PASS | 与参考实现 111/111 |
| ocl probe | PASS | 失败 0 项 |
| SIMD kernel self-test | PASS | 19 项检查，RESULT: ALL PASS |
| gui selftest (gpu=off) | PASS | exit=0，log 765 行 / 170 条自检，FAIL 0 |
| gui selftest (gpu=on) | PASS | exit=0 |
| benchmark | PASS | 与 v3.2.1 基线持平 |
| dist self-test | PASS | exit=0 |
| hash build==dist（4 个 exe） | PASS | 主程序 `3F362C920AD8F56510B98AC07D164BD320E773E610B45AC67164BB21E26E08FB` |
| hash zip==dist | PASS | — |
| zip self-test | PASS | exit=0 |

## 三、交付物

| 文件 | 大小（B） | SHA-256 |
| --- | --- | --- |
| `dist/SlimeFinder-3.5.10-win64.zip` | 15,281,259 | `624B755625867AD1BB846B035289C696347D951998C1D2B95D0BA15FC1132324` |
| `dist/SlimeFinder-3.5.10-Setup.exe` | 10,666,326 | `3307D4B334A12C89E62E0D9A522C8489D995905D3C5FC26C1DCCCB858416A65D` |

## 四、安装包冒烟

- 静默安装 `/S /D=C:\Temp\sf_install_3510` → `SlimeFinder.exe` 版本显示 **3.5.10 / 3.5.10**；
- 安装副本跑 `-Selftest`：exit=0，766 行日志，**FAIL=0**，更新历史 57 条 / 正式版 13 个；
- 静默卸载后安装目录清空（`SlimeFinder.exe` 不存在）。

## 五、审计与一致性

- `scripts/audit_corruption.py`：**CLEAN**（283 个文件，无编码缺陷、无版本漂移、docs 与包内副本一致）；
- `dist/SlimeFinder` 与 zip 逐文件一致（32 个条目，0 处不一致）；
- 修了两个审计发现：① `C:\Temp\slimecache\sync_dist_from_zip.py` 原先硬编码 `SlimeFinder-3.5.9-win64.zip`，会把旧包解回 dist，改为自动选择**数字版本最大**的 zip；② `docs/build_instructions.md` 里遗留的 `dist/SlimeFinder-3.5.9-win64.zip` 路径改为 3.5.10。

## 六、视觉证据（打包后渲染）

- 聚焦环（点结果行后）：`r3_ring-focused.png` —— 单一 24~128 双描边蓝环 + 红心 + 其余候选小红点，无橙色方块；
- 改前对比：`r1_ring-focused.png` —— 环与 64 个橙色结果方块叠在一起；
- 方案对比图：`ring_style_options2.png`（A 细环双层描边 / B 半透明环带 / C 环带+角括号）。

## 七、结论

**PASS**：功能、视觉、回归、安装包、审计全部通过；历史条目与正式版计数同步更新。
