# SlimeFinder v3.4.12 验证报告（六项实测反馈 + 交付审计缺陷）

- 交付物：`dist/SlimeFinder-3.4.12-win64.zip`（**83,355,754 B**，
  SHA256 `B69455E3E76EDA94250D25BA26505998FEC9214F31C882D90F4DA3405A35DFBF`）
- 等价命名副本：`dist/SlimeFinder-3.4.1-win64.zip`（字节完全相同）；回滚包：`dist/SlimeFinder-3.4.0-rollback-win64.zip`
- 校验：`dist/SHA256SUMS-3.4.12.txt` / `dist/SHA256SUMS-3.4.1.txt`
- 截图：`docs/screenshots/v3412/`（102 张：浅/深色 + GPU 关/开，含候选框两张新证据）
- 证据：`docs/evidence_v3412/`（自检关/开/Windows 平台 + verify + 打包 + 单元/参考/内核 + 编码对照实验）
- 进度：`docs/v3412_progress.md`

## 一、用户六条实测反馈逐条

| # | 反馈（原话要点） | 做法 | 证据 |
|---|---|---|---|
| 1 | 单位从输入框里拿出来（预算的 MB、日志保留时间的天） | 新增 `withUnit(控件, 单位)`：单位是输入框**后面**的独立标签；全部 `setSuffix` 清零 | 自检：`仍带 suffix 的数值框 0 个；框外单位标签 13 个`；截图 `v3412_settings-tab4.png`（100 %、自动 MB、30 天） |
| 2 | 构建预估弹窗信息未同步参数 | 预估改为构建页**常驻卡片**，直接读当前控件值实时重算；不再弹窗 | 自检：`实时预估：范围：X(-256 ~ 255)　Z(-256 ~ 255) ⇒ 262144 区块`，改预算后卡片同步；截图 `v3412_page-1-build.png` |
| 3 | 缓存构建时仍被要求设置搜索模式 | 构建模式跳过「搜索模式 / 绘制图案」校验，并把引擎参数就地归一（`Density` + 空图案 + `Exact`，先归一后 `validate`） | 自检：`构建模式（严格模式 + 未画图案）startSearch ⇒ 已开始=1（运行中=1 已完成=0）` |
| 4 | 左侧灰色「重置设置」文字与下方按钮重复 | 删除高级页那行灰字（设置页底部操作栏已有「恢复所有设置」；页内「重置所有设置」按钮在设置页中本就隐藏） | 自检：设置页范围内该灰字标签 **0 个** |
| 5 | 取消红框内容 | 移除构建页「预估 / 开始构建 / 中断」整行按钮与提示（开始/中断走工具栏主按钮） | 自检：三个按钮对象均为 `nullptr`；截图 `v3412_page-1-build.png` |
| 6 | 黄框内容单独列出、实时更新、不再弹窗 | 变成独立卡片「构建预估（随范围与预算实时更新）」：范围 / 预估体积 / 当前预算结论 | 同上截图 |
| 附 | Tab 候选框没有出现 | ① 空输入原先直接返回（不显示列表）② `Qt::ToolTip` 顶层窗口在部分平台不显示 ⇒ 改为终端**子控件浮层**（`#cmdPopup`、`Qt::NoFocus`、`WA_ShowWithoutActivating`）+ 空输入列出全部命令 | 自检：`空输入 Tab：候选 27 个（全部命令 27 条）`、`显示=10/10 次`、`位置在输入行上方=1`；截图 `v3412_cmd-tab-popup.png`、`v3412_cmd-tab-popup-all.png`；Windows 平台焦点实测 `保持 10/10 次，被其它控件抢走 0 次（可获焦=1）` |

## 二、交付审计抓出的缺陷（本轮最重要的一条）

`scripts/audit_corruption.py` 报出 `build/config.json` **不再是合法 JSON**：

```
"device": "gfx1031锛圓dvanced Micro Devices, Inc.锛?,     ← 收尾引号被吃掉
文件开头 EF BB BF 7B 0D 0A                                ← 多了 UTF-8 BOM
期望：   "device": "gfx1031（Advanced Micro Devices, Inc.）",
```

**两条根因（都已修）**

1. 辅助脚本用 PowerShell 5.1 的默认编码做「读改写」往返：`gui_selftest.ps1` 与
   `verify_release.ps1` 里的 `Get-Content -Raw`（按 ANSI/GBK 解码）+ `Set-Content -Encoding UTF8`（写 BOM）。
   设备名里的全角括号是 UTF-8 `EF BC 88` / `EF BC 89`，被当 GBK 解码后变成 `锛` + `圓`（吃掉 `Advanced` 的 `A`），
   第二处再吃掉收尾引号 ⇒ 文件非法。**对照实验复现了与审计文件逐字节相同的损坏**（证据
   `docs/evidence_v3412/enc_roundtrip_proof.txt`）。改为 `[System.IO.File]::ReadAllText/WriteAllText`
   + `UTF8Encoding($false)`（无 BOM）；`package_release.ps1` 随包默认 config 同样改为无 BOM 写出。
   同一类 BOM 缺陷还在 `dist/SHA256SUMS-*.txt` 上（历史版本都带 BOM，严格校验工具会把 BOM 当成
   第一行内容）—— 本轮起校验和文件一并改为无 BOM。
2. 应用自身写入端不做 JSON 转义（真缺陷，用户可触发）：`writeConfigFile()` 手工拼 JSON，
   种子 / 缓存目录 / 日志子目录 / 设备名里的引号与反斜杠原样写入。Windows 上 `cacheDir = C:\Temp\cache`
   会产出非法转义 `\T` ⇒ 外部解析器读不回整份设置。新增 `jsonEscapeUtf8()`（引号 / 反斜杠 / 控制字符转义，
   非法 UTF-8 字节替换为 U+FFFD）并让 8 处字符串值全部走它；读回端新增对称的 `jsonUnescape()`
   （兼容旧文件：未知转义原样保留，反转义后出现控制字符而原文没有 ⇒ 判定为旧文件按原样返回）。
   自检新增：`config.json 无 BOM`、`config.json 是合法 JSON（QJsonDocument 复核）`，
   并把 `cacheDir / logSubDir` 设为含反斜杠 + 中文的值断言全链往返一致。

## 三、回归（本轮最终构建）

| 项目 | 结果 |
|---|---|
| 编译 | exit 0，**零 error / 零 warning** |
| 单元测试 | **26 组全部通过** |
| 参考对照 | **111/111** |
| SIMD 内核自检 | **19 项 ALL PASS** |
| GUI 自检（GPU 关 / 开） | **exit 0 / exit 0，零 FAIL** |
| GUI 自检（Windows 平台，焦点实测） | Tab 候选 `显示=10/10`、`焦点保持 10/10`、`当前位置在输入行上方`；该轮另有 1 项**原生文件对话框**断言失败（与 v3.4.11 完全相同、offscreen 口径专有） |
| 发布包自检 | `verify_release.ps1` ⇒ **16/16 PASS** |
| 打包内部校验 | **PACKAGE OK**（包内 GUI 自检 exit=0 + 单元 + 内核 + 哈希三方一致） |
| 污染审计 | **CLEAN**（245 文件，0 项发现） |

```
SHA256  SlimeFinder.exe              = F9CE42CD075B81109DEC1D51831E25E5F8667F49E589D0B8820CAB44CDBBEB0F
SHA256  SlimeFinder-3.4.12-win64.zip = B69455E3E76EDA94250D25BA26505998FEC9214F31C882D90F4DA3405A35DFBF
SHA256  SlimeFinder-3.4.1-win64.zip  = B69455E3E76EDA94250D25BA26505998FEC9214F31C882D90F4DA3405A35DFBF（同字节）
```

## 四、自检新增/加固

1. **GUI 第 27 组**：单位外置、构建页结构（无三按钮 + 实时卡片随预算变化）、构建模式免检查、
   设置页灰字删除、候选框两张截图。
2. **config.json 合法性**（第 14.10 组内）：无 BOM + `QJsonDocument` 可解析 + 反斜杠/中文路径往返一致。
3. **灰字检查范围收敛到设置页**：v3.4.12 教训 —— 按整窗查会被更新历史正文里的同一句话误报。
4. **取消压测加入热身**：OpenMP 线程池首次建立（负载高时数百毫秒）会被算进「最差取消响应」，
   打包时曾因此出现一次 1165 ms 的**假失败**（阈值 1 秒未变）。热身实测：`取消 10 次：最差响应 91.9 ms（10 次命中扫描中）`。
5. **编译告警清零**：自检 `fprintf` 的 `%d` 配 `qsizetype`（C4477，6 处）+ 更新历史表头正则里
   `\.` 被当成非法转义（C4129）全部修掉；此前的检查只看 `error C`，告警会积累。

## 五、说明

1. 单位标签只改呈现，控件范围/步长/取值口径未动 ⇒ 配置读写与 `range` / `cache budget` 等命令语义不变。
2. 构建页的「预估」与输入框现在是同一数据源，消除了「弹窗快照 vs 当前参数」两条路径。
3. 更新历史 V3.4.12 共 8 条（含本条配置文件修复），历史共 **46 个版本**（1.0.0 → 3.4.12）。
4. 本报告写在打包之后，zip 内 docs 快照不含本文件；仓库 `docs/` 与 `dist/SlimeFinder/docs/` 两侧内容一致。
