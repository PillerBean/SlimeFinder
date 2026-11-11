# SlimeFinder v3.4.11 验证报告（八项修正 + 五问落地）

- 交付物：`dist/SlimeFinder-3.4.11-win64.zip`（**67,465,129 B**，
  SHA256 `241A01C12CA0EC32B41AD1F0794CAAD28FA3D270CF9324370C85A302093D79D1`）
- 等价命名副本：`dist/SlimeFinder-3.4.1-win64.zip`（字节完全相同）；回滚包：`dist/SlimeFinder-3.4.0-rollback-win64.zip`
- 校验：`dist/SHA256SUMS-3.4.11.txt` / `dist/SHA256SUMS-3.4.1.txt`
- 截图：`docs/screenshots/v3411/`（98 张）；证据：`docs/evidence_v3411/`（自检两轮 + Windows 平台焦点验证 + verify/单元/参考/内核）
- 流程答复：**`docs/v3411_early_stop_flow_v2.md`**（五问逐条 + v2 图的三处修正）

## 一、前置硬条件（先做、先验）

| 项 | 结果 |
|---|---|
| 分块判据 `> worst` → `>= worst`（等距也剪，与经典统一） | 已改（`SlimeEngine.cpp:2435`） |
| **改后 111/111 逐位一致** | ✅ **通过**（`通过 111 项，失败 0 项`），单元 26 组、内核 19 项同时全绿 |

## 二、八项修正逐条

| # | 项 | 做法 | 证据 |
|---|---|---|---|
| 1 | 五问中的第 1、2 条 | 判据统一为「等距也剪/也停」；等距取舍规则（`dist` → `rank`）写进注释与文档；新增**等距单元组 [26]** | 组 [26]：完整/提前停止结果一致；四种分块大小一致；剪枝对照一致 |
| 2 | 五问第 3 条：分块 `stoppedEarly` 漏 `cacheFirstStopped` | `out.stoppedEarly = (chunksPruned > 0) \|\| cacheFirstStopped` | GUI 自检：`提前停止=1 缓存内即停=1 界面显示[提前停止]=1` |
| 3 | 五问第 5 条：64 行常量 | 集中为 `kStopBlockRowsDefault` + `stopBlockRows()`，`SLIME_STOP_BLOCK_ROWS` 可覆盖（1~4096） | 组 [26]：`1/7/64/4096` 结果逐位一致 |
| 4 | 全局约束：设置变更不重放 QSS（音量条卡顿根因） | `applyAppSettings` 只在**主题真的变了**时才重放样式表；新增 `ThemeManager::applyCount()` 供断言 | 自检：`QSS 应用次数：基线=17 非主题变更后=17 主题变更后=18 事件泵后=18` |
| 5 | 写盘合并（拖动结束立即写 + 空闲 500ms） | 新增 `scheduleConfigWrite()` / `flushPendingConfigWrite()`；滑块 `sliderReleased` 立即 flush；`applyAppSettings` 末尾不再直接写盘 | 自检：`写盘合并：基线=38 连续 5 次改动后=38 flush 后=39` |
| 6 | 设置在重启后生效但设置项状态不对 | 根因：`buildUi()` 先按默认值建设置页，`loadAppSettings()` 之后没人把值推回页面（更糟：页面是自动生效的取值来源，之后任何改动都会把过期值写回）。新增 `syncSettingsPageFromState()`，在加载配置后与 `applyAppSettings` 末尾各收敛一次，并在同步期间抑制自动生效 | 自检：`设置收敛：m_settings{…} 页面{…} 一致=1` |
| 7 | 更新历史默认折叠 | `setHistoryRowExpanded(row,false)`；截图流程收尾同样还原为折叠 | 自检：`默认展开的版本行 = 0`；`折叠箭头：正常 44 个 / 异常 0 个` |
| 8 | 命令终端：扩展 / clear / 拒无效 / 同步设置页 / Tab 候选框 / 命令自检 | 命令表 14 → **27 条**（新增 theme/stop/smart/lock/grid/density/threads/build/cache/copy/open/help <cmd>/about/clear）；`clear` 由终端内置实现、宿主也给出应答；构建模式下参数命令**拒绝并说明**；`theme`/`log` 命令同步设置页；`copy <n>` 越界明确报错；`cache build` = 切到构建模式并开始构建（`build off` 退出）；Tab 候选框按 Minecraft 风格（输入行上方、Tab 循环、Esc 关闭、**不抢焦点**） | 自检：`命令 27 条：缺帮助 0、未实现 0、无输出 0`；`Tab 候选：候选=5 个 显示=10/10 次 焦点保持=10/10 位置在输入行上方=1`；`构建模式拒绝：grid → 当前是缓存构建模式…`；`theme dark ⇒ 设置页主题=dark`；`log detailed ⇒ 设置页{起始=1 级别=1} LogBus=详细`；`clear 命令清空终端输出` |

### 用户额外提醒的落实

1. **音量条根因是全局性的** ⇒ 已加全局约束（任何设置变更不重放 QSS，除非主题变更）+ 写盘合并，并用 `applyCount()` 计数断言（不只是音量条）。
2. **stoppedEarly 必须连 UI 一起测** ⇒ 已加断言（字段 + 结果区文本）。
3. **每条命令必须可观测** ⇒ 自检对全部 27 条命令逐一执行查询态，断言「有输出且不是未知命令」，并单独断言 stop/grid/density/threads/smart/lock 真正改状态。
4. **Tab 候选框焦点必须实测** ⇒ 离屏平台无法真正获焦，故另跑一轮 **Windows 平台**自检：`焦点：保持 10/10 次，被其它控件抢走 0 次（可获焦=1）`（证据 `docs/evidence_v3411/gui_selftest_windows_stderr.log`）。
5. **theme 命令也要同步设置页** ⇒ 已做（`theme dark ⇒ 设置页主题=dark`）。
6. **cache build 语义** ⇒ 明确为「切到构建模式并立即开始构建」，离开用 `build off`；构建模式下的参数命令被拒绝，两者不冲突。
7. **copy <n> 越界** ⇒ 明确报错：`没有第 n 条结果（当前 M 条，先执行 search）`。

## 三、回归（本轮最终构建）

| 项目 | 结果 |
|---|---|
| 编译 | exit 0，无 `error C` / `error LNK` |
| 单元测试 | **26 组全部通过**（新增组 [26]） |
| 参考对照 | **111/111** |
| SIMD 内核自检 | **19 项 ALL PASS** |
| GUI 自检（GPU 关 / 开） | **exit 0 / exit 0，两轮零 FAIL** |
| GUI 自检（Windows 平台，焦点实测） | Tab 焦点保持 10/10（该轮另有 1 项**原生文件对话框**相关断言失败，属 offscreen 专有口径，与本轮改动无关） |
| 发布包自检 | `verify_release.ps1` ⇒ **16/16 PASS** |
| 打包内部校验 | **PACKAGE OK**（含包内 GUI 自检/单元/内核） |
| 污染审计 | **CLEAN** |

```
SHA256  SlimeFinder.exe              = D3EA575010D6C007D660CE4B5F53E243F8CB079B67459B9EC6FD7225AB3070E8
SHA256  SlimeFinder-3.4.11-win64.zip = 241A01C12CA0EC32B41AD1F0794CAAD28FA3D270CF9324370C85A302093D79D1
```

## 四、顺带修掉的真实缺陷（都是自检抓出来的）

1. **挂起的「设置自动生效」会把程序化改动改回去**：`applyAppSettings` 不同步设置页时，60ms 后定时器用过期页面值覆盖（自检实测多出一次 QSS 重放并回退主题）⇒ 已由 `syncSettingsPageFromState()`（含停表）修掉。
2. **崩溃转储只在设置值变化时才生效**：外部状态漂移会漏配 ⇒ 改为无条件幂等调用。
3. **日志模式只在设置值变化时才 setMode**：命令/菜单改了 LogBus 而设置值没变时两边漂移（包内自检抓到）⇒ 改为「按设置幂等收敛」。
4. **`clear` 幽灵指令**：help 里有、实现里没有 ⇒ 终端内置实现 + 宿主应答，且自检强制「命令表 ↔ 帮助表 ↔ 实测」三者一致。

## 五、说明

1. 分块路径的 `>=` 与经典路径的 `nth <= L` 现在语义一致（等距不进入前 N）；参考对照 111/111 与单元 26 组共同保证无损。
2. 线程数 ≥ 分块数时不存在「尚未开跑」的块 ⇒ 缓存优先即停不会触发（数学必然）；多线程跨块剪枝由单元组 [26] 覆盖。
3. 本报告写在打包之后，zip 内 docs 快照不含本文件；仓库 `docs/` 与 `dist/SlimeFinder/docs/` 两侧内容一致。
