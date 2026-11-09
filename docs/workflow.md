# 每轮改动的工作流（v3.6.12 起）

## 为什么要改

一轮「改代码 → 验证 → 打包」的实测耗时（本机，6 物理核）：

| 阶段 | 耗时 | 说明 |
|---|---|---|
| 增量构建 | ~2 s | 没改版本号时几乎免费 |
| 单元测试（34 组） | ~48 s | 含搜索回归组 |
| SIMD 内核自检 | ~105 s | 含基准 |
| 参考实现 111 项 | ~51 s | 对比 SlimeChecker |
| GUI 自检（关 GPU） | ~136 s | 173+ 条断言 |
| GUI 自检（开 GPU） | ~136 s | 同上 |
| 打包 package_release | ~4 min | 内含包内自检 + 单测 + 内核 |
| 安装包 build_installer | ~1 min | NSIS |
| verify_release（干净重建 14 项） | ~12 min | 含一次从零构建 + 两轮 GUI 自检 |
| 审计 audit_corruption | ~1 min | 307 文件词法/编码检查 |

→ 全部跑一遍 ≈ **27 分钟**。问题不在某一项慢，而是**每轮都跑全量**。

## 新的四档流程（`scripts/check.ps1`）

```powershell
# 1) 快速闭环：改完先跑这个，几十秒内知道有没有编译错/单元挂
powershell -ExecutionPolicy Bypass -File scripts\check.ps1 -Level fast      # ~50 s

# 2) 每轮默认：加内核 + 参考实现 + GUI 自检（关 GPU）
powershell -ExecutionPolicy Bypass -File scripts\check.ps1 -Level standard  # ~5.7 min

# 3) 要出包时：再加 GUI(开 GPU) + 打包
powershell -ExecutionPolicy Bypass -File scripts\check.ps1 -Level full -Version 3.6.13   # ~12 min

# 4) 转正式版/发版前：再加干净重建验证 + 审计
powershell -ExecutionPolicy Bypass -File scripts\check.ps1 -Level release -Version 3.6.13 # ~27 min
```

## 配套约定

1. **默认档位 = standard**：每轮改动跑完 standard 再汇报，不再每轮打包。
2. **打包只在你要包的时候做**（`-Level full`），避免重复付「包内自检 + NSIS」的成本。
3. **verify_release 只在转正式版或大改后跑**（干净重建能抓出头文件/构建脚本层面的问题，日常改动用增量构建足够）。
4. **GUI 自检只在改动涉及界面/交互时跑**（纯算法/文档改动可用 fast）。
5. 一轮尽量**批量合并**若干小修改，避免「改一行跑一轮」。
