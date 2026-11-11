# SlimeFinder v3.4.9 验证报告（本轮三项要求）

- 交付物：`dist/SlimeFinder-3.4.9-win64.zip`（**52,412,050 B**，
  SHA256 `58A679AEC77BDC248C55436BA85DFABCE86DB2A196BDA92BFDF5385E7C297768`）
- 任务书等价命名副本：`dist/SlimeFinder-3.4.1-win64.zip`（**字节完全相同**）
- 回滚包：`dist/SlimeFinder-3.4.0-rollback-win64.zip`（SHA256 `2CB879644943826527F28312156830DA71775BFA716EA09E26A691C9007C1B4D`）
- 校验文件：`dist/SHA256SUMS-3.4.9.txt` / `dist/SHA256SUMS-3.4.1.txt`
- 证据：`docs/screenshots/v349/`（86 张）、`docs/evidence_v349/`（自检/verify/单元/参考/内核日志 + 三项证据摘录）
- 配套文档：**`docs/v349_immediate_feedback_flow.md`（及时反馈实现的完整流程图与判据说明）**

## 一、本轮三项要求

### 1) 描述「及时反馈」的实现流程

已单独成文：`docs/v349_immediate_feedback_flow.md`（总流程图 + 判据公式 + 经典/分块两条路径 +
v3.4.9 修掉的三处「不生效」+ 界面反馈位置 + 实测数据表）。要点：

- 判据统一为「剩余窗口距离下界 `L` ≥ 当前第 N 名距离 `nth` ⇒ 可以停」，**无损**（结果仍是全局最近 N）；
- 经典路径：密度滑动每 64 行判一次界、其它模式逐锚点行判界并 `break`；
- 分块路径：块级 `chunkCannotImprove()` + 缓存优先阶段的 `cacheStopFlag`；
- 结果区标注 `[提前停止]` 与「达到目标数量提前停止，共确认 ≥N 个，显示最近 N 个」。

### 2) 缓存构建范围与搜索范围在部分情况下不同步

| 项 | 内容 |
|---|---|
| 根因 | 原来只有**单向**同步（构建页 → 搜索页）。搜索页范围被程序性改动时（快速预设「附近/局部/全局/边界」、命令终端 `range`、整图确认）不会回写构建页 ⇒ 构建页显示旧范围，而引擎实际用搜索页那份 ⇒ 「部分情况下不同步」 |
| 修复 | 改成**双向同步**：两侧 8 个数值框都挂 `valueChanged` → 对侧镜像，用 `m_rangeSyncGuard` 防递归；构建页只是同一份数据的镜像视图，引擎仍只读搜索页那份 |
| 证据 | 自检第 24 组：搜索页改 `-321..123 / -77..456` ⇒ 构建页一致=1；构建页改 `-64..63 / -32..31` ⇒ 搜索页一致=1；`applyPreset(256)`（程序性改动）⇒ 一致=1，且 `buildTotalChunks()` 按同步后的范围计算（128×64 = 8192） |

### 3) 移除搜索界面的缓存构建开关

| 项 | 内容 |
|---|---|
| 做法 | 搜索页不再有「缓存构建模式」勾选框；构建模式改为内部状态 `m_buildModeActive`，由**当前页面**驱动：进「缓存构建」页=开，回「搜索」页=关（`setBuildModeActive()`）。搜索页原位置留一句「缓存构建已移到左侧「缓存构建」页（本页只负责搜索）」 |
| 顺带 | 构建提示行（范围 / 预计缓存体积 / 可中断续传 / 完成条件）随构建内容一起搬到构建页；`updateBuildHintUi()` 不再因提示行缺失而提前返回，且关闭构建模式时只恢复**自己禁用过**的项（不再把版本规则的禁用一起解掉） |
| 证据 | 自检第 24 组：搜索页含「缓存构建」字样的勾选框 = **0 个**；「已移到」说明存在；`buildModeActive()` 与当前页面一致；第 23 组：构建模式下 **13 项禁用 / 0 项漏灰**，回搜索页 13 项全部恢复 |

## 二、回归（本轮最终构建）

| 项目 | 结果 |
|---|---|
| 编译 | exit 0，无 `error C`/`error LNK` |
| 单元测试 | **25 组全部通过** |
| 参考对照 | **111/111** |
| SIMD 内核自检 | **19 项 ALL PASS** |
| GUI 自检（GPU 关 / 开） | **exit 0 / exit 0，两轮均无 FAIL**（24 组，含本轮新增第 24 组） |
| 发布包自检 | `verify_release.ps1 -Zip dist\SlimeFinder-3.4.9-win64.zip` ⇒ **16/16 PASS** |
| 污染审计 | `audit_corruption.py` ⇒ **CLEAN**（版本字面量 3.4.9 全一致；docs 副本 identical） |
| 打包内部校验 | **PACKAGE OK**（build == dist == zip 四个产物哈希逐一相等） |

产物哈希：

```
SHA256  SlimeFinder.exe            = 9592A8728032BEA1F712FFDB11CF29FE652ECCCC98C900C545E892D6DF849CFA
SHA256  SlimeFinder-3.4.9-win64.zip= 58A679AEC77BDC248C55436BA85DFABCE86DB2A196BDA92BFDF5385E7C297768
```

## 三、版本与更新历史

- 版本号全面提升 **3.4.8 → 3.4.9**（`app.rc` / `main.cpp` / `CMakeLists.txt` / `install.nsi` /
  `package_release.ps1` / `verify_release.ps1` / `README.md` / `docs/build_instructions.md` / `kAppVersion`）。
- 更新历史新增 **3.4.9** 条目（面向普通用户：移除搜索页开关、范围同步修复、构建提示随页），
  条目数 43 → **44**，自检断言同步为 44。

## 四、说明

1. 本轮未改变引擎的判据与结果语义：提前停止仍保证「全局最近 N」，参考对照 111/111 未变。
2. `docs/v349_immediate_feedback_flow.md` 里的流程图用 Mermaid 编写（GitHub/VS Code 可直接渲染），
   同时给出等价的文字判据说明，便于不看图也能核对。
3. 本报告写在打包之后，因此 zip 内的 docs 快照不含本文件；仓库 `docs/` 与 `dist/SlimeFinder/docs/`
   两侧内容一致（审计的 docs 副本比对为 identical）。
