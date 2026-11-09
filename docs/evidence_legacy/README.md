# 历史证据文件（v3.3.0 ~ v3.3.4）

> 这些是**历史版本**的验证证据原始捕获，仅作可追溯性保留，**不是当前版本的证据**。
> 当前版本（v3.3.5）的证据在上一级目录 `docs/*_v335.txt`。

## 为什么它们看起来是乱码

v3.3.5 的污染审计（`scripts/audit_corruption.py` 第 [7] 类检查）在这些文件里发现了
成片的 `U+FFFD` 替换字符，根因是**诊断输出与采集端编码不一致**，而不是内容写错：

| 现象 | 根因 | 影响文件 |
|---|---|---|
| 中文标签整体乱码（每份 481~503 个 `U+FFFD`） | `tests/verify_against_reference.py` 的 `print()` 在 Windows 重定向时按 ANSI(GBK) 编码输出，而 PowerShell 按 UTF-8 解码 | `verify111_v33x.txt`、`pkgverify_reference111_v334.txt` |
| 部分行乱码 | GUI 自检用 `QString::toLocal8Bit()`（GBK）写 stderr，与同文件其它 UTF-8 行混排 | `gui_selftest_v334_gpu_on_stderr.txt` |
| 单个 `U+FFFD`（每份 1 个） | 打印内核首行时按**字节**截断到 70 字节，切断了多字节 UTF-8 字符 | `kernel_selftest_v33x.txt`、`pkgverify_kernel_selftest_v334.txt` |

被 `U+FFFD` 覆盖的原始字节已经丢失，无法无损还原，因此没有改写这些文件的内容。

## v3.3.5 的处理

1. **产生端统一 UTF-8**：GUI 自检的 `toLocal8Bit()` 全部改为 `toUtf8()`；崩溃/异常报告的模块路径由
   `GetModuleFileNameA`（ANSI）改为宽字符 + UTF-8；Python 脚本显式 `sys.stdout.reconfigure(encoding="utf-8")`；
   内核首行打印退回到 UTF-8 字符边界再截断。
2. **采集端统一 UTF-8**：`scripts/verify_release.ps1` 在开头固定 `[Console]::OutputEncoding` /
   `$OutputEncoding`，并设置 `PYTHONUTF8=1`、`PYTHONIOENCODING=utf-8`。
3. **证据导出可复现**：新增 `scripts/export_evidence.ps1`，按 BOM 解码日志并统一写成带 BOM 的 UTF-8，
   同时统计 `U+FFFD` / NUL（任一非零即退出码 1）。
4. **审计门禁**：`scripts/audit_corruption.py` 第 [7] 类检查对全树做编码体检；本目录作为
   **已登记的历史例外**被排除（排除项写在脚本 `SKIP_LEGACY_EVIDENCE` 常量处，附原因）。

v3.3.5 的证据文件已按上述流程重新生成，编码体检为 0 缺陷。
