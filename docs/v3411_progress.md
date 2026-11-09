# v3.4.11 进度与交付记录

> 前置硬条件（用户要求）：分块判据 `> worst` → `>= worst` 后**先跑 111/111，绿了再继续** ⇒ 已通过。

## 一、八项改动

| # | 项 | 关键改动 |
|---|---|---|
| 1 | 等距统一（五问 1/2） | 分块判据改 `>= worst`；等距取舍规则（`dist` → `rank`）文档化；新增单元组 [26] |
| 2 | `stoppedEarly` 补 `cacheFirstStopped`（五问 3） | 分块收尾合并两个来源；GUI 断言字段 + 界面文本 |
| 3 | 64 行常量（五问 5） | `kStopBlockRowsDefault` + `stopBlockRows()` + `SLIME_STOP_BLOCK_ROWS` 覆盖 + 单测 |
| 4 | QSS 不重放（全局约束） | `applyAppSettings` 仅主题变化时重放；`ThemeManager::applyCount()` 计数断言 |
| 5 | 写盘合并 | `scheduleConfigWrite()` / `flushPendingConfigWrite()`；滑块释放立即 flush |
| 6 | 设置页状态一致 | `syncSettingsPageFromState()`（加载配置后 + `applyAppSettings` 末尾；同步期间抑制自动生效并停表） |
| 7 | 更新历史默认折叠 | 默认折叠 + 断言 0 + 截图流程还原折叠 |
| 8 | 命令终端 | 命令表 14 → 27 条；`clear` 落地；构建模式拒绝参数命令；`theme`/`log` 同步设置页；`copy` 越界报错；`cache build`/`build off` 语义；**Tab 候选框（Minecraft 风格，不抢焦点）**；命令自检（每条可观测） |

## 二、用户七条提醒的落实

1. 音量条根因全局化 ⇒ 全局约束 + 计数断言（见改动 4/5）。
2. stoppedEarly 连 UI 一起测 ⇒ 已断言「结果区显示 [提前停止]」。
3. 每条命令可观测 ⇒ 27 条命令逐一查询态执行 + 6 条状态类命令改状态断言。
4. Tab 焦点实测 ⇒ **Windows 平台**另跑一轮：`焦点：保持 10/10 次，被其他控件抢走 0 次（可获焦=1）`。
5. `theme` 命令同步设置页 ⇒ 已做（三处一致：生效态 / m_settings / 设置页）。
6. `cache build` 语义 ⇒ 「切到构建模式并立即开始构建」，`build off` 退出；与「构建模式拒绝参数命令」不冲突。
7. `copy <n>` 越界 ⇒ 明确报错（含当前条数与提示先 search）。

## 三、自检新增：GUI 第 25、26 组 + 单元组 [26]

- **单元 [26]**：等距一致性、四种判界分块大小一致、2/4 线程剪枝对照（真实扫描 2097 万 vs 3369 万区块）、`cacheFirstStopped ⇒ stoppedEarly`。
- **GUI 25 组**：设置收敛、QSS 重放计数（非主题 0 次 / 主题 1 次）、写盘合并（5 次改动 0 写盘、flush 1 次）、缓存优先即停的字段与 UI 文本。
- **GUI 26 组**：命令表一致性（27 条：缺帮助 0、未实现 0、无输出 0）、状态类命令真正改状态、`theme`/`log` 同步设置页、构建模式拒绝、`copy` 越界、`cache status`、`open` 用法、**Tab 候选框（候选 5 个 / 显示 10/10 / 焦点保持 10/10 / 在输入行上方 / Esc 关闭 / 唯一候选直接补全）**、`clear` 清空输出。

## 四、交付

| 项 | 值 |
|---|---|
| 发布包 | `dist/SlimeFinder-3.4.11-win64.zip`（67,465,129 B，SHA256 `241A01C1…3D79D1`） |
| 等价命名 | `dist/SlimeFinder-3.4.1-win64.zip`（同字节）+ `dist/SHA256SUMS-3.4.1.txt` |
| 回滚包 | `dist/SlimeFinder-3.4.0-rollback-win64.zip` |
| 校验 | `dist/SHA256SUMS-3.4.11.txt`；`verify_release.ps1` **16/16 PASS** |
| 截图 | `docs/screenshots/v3411/`（98 张） |
| 日志 | `docs/evidence_v3411/`（自检关/开 + Windows 平台焦点验证 + verify + 单元/参考/内核） |
| 文档 | `docs/verification_report_v3411.md`、`docs/v3411_early_stop_flow_v2.md`（五问答复 + v2 图修正）、`docs/update_history.md`（V3.4.11 条目，共 45 条） |

## 五、回归结论

编译 exit 0；单元 **26 组**；参考 **111/111**；内核 **19 项 ALL PASS**；GUI 自检 **GPU 关/开 都 exit 0、零 FAIL**；
`verify_release` **16/16 PASS**；打包 **PACKAGE OK**；污染审计 **CLEAN**。

## 六、说明

- 分块 `>=` 与经典 `nth ≤ L` 语义一致（等距不进入前 N）；无损性由 111/111 + 单元组 [26] 双保险。
- 线程数 ≥ 分块数时无「尚未开跑」的块 ⇒ 缓存优先即停不触发（数学必然）；多线程跨块剪枝由单元组 [26] 覆盖。
- Windows 平台那轮自检另有 1 项**原生文件对话框**断言失败（offscreen 专有口径），与本轮改动无关，仅作焦点实测的辅助证据。
