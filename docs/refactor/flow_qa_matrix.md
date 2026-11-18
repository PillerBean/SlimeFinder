# 流程集成功能搭配测试

基线：`flow-integration` @ b7e2f26

环境：

```text
Windows / MSVC Release / Qt 6.11.2
GPU: gfx1031 (AMD)
```

## 覆盖矩阵

| 范围 | 结果 |
|---|---|
| 单元测试 | 通过 |
| SIMD 内核 19 项 | ALL PASS |
| 参考实现 117 项 | 通过 117，失败 0 |
| GUI 自检 GPU off | exit=0 |
| GUI 自检 GPU on | exit=0 |
| GUI 自检低 RAM（128MB） | exit=0 |
| 环形搜索（GUI 自检内） | 通过，含大范围/无果/模拟距离 |
| 缓存 store / pack / band | OK |
| 缓存 ZIP 导入 | 16 packs imported |
| manifest 流式解析 | 2/2 packets |
| SearchJob | ALL PASS |
| RAM 预算 | ALL PASS |
| 命中估算 | HIT-ESTIMATE OK |
| GPU cap 截断（fake） | GPU-TRUNCATION OK |
| GPU cap 截断（真实 GPU，cap=256） | GPU-CAP-REAL OK，total=39542，fallback 空 |
| GPU 扫描续行 | GPU-RESUME OK |
| 模式基准（两种版本 × 四种算法） | 完成，结果符合预期 |
| CLI 组合（版本/智能/提前停止/线程） | 全部 rc=0 |

## 发现并修复的问题

### 1. `check.ps1` PowerShell 5.1 解析失败

原因：UTF-8 无 BOM，Windows PowerShell 5.1 按 ANSI 读取。

修复：保存为 UTF-8 BOM，`check.ps1 -Level standard -NoGui` 现在可运行。

### 2. `slime_tests` 非法版本参数直接崩溃

原因：`std::stoi` 异常未捕获。

修复：非法数字参数返回 usage / exit 2，不再 `0xC0000409`。

## 运行环境问题（非产品缺陷）

### 参考实现默认路径找不到 SlimeChecker.exe

集成 worktree 没有外部参考程序；显式传入主仓库的
`tests/reference/SlimeChecker.exe` 后 117 项全通过。

### `cache_stream_test` 需要正式流式 manifest

`cache-build/*/index.json` 是本地索引，不是流式 manifest（缺 `totalPacks`）。
用带 `totalPacks + packs` 的 manifest 验证通过。

## 本轮追加修复

```text
环形搜索完全异步化：runRingSearch 立即返回，finishRingSearch 回 GUI 线程收尾
高命中模态警告：真实 QMessageBox + QTimer 自动确认，selftest 断言
超大构建 RAM 守卫：预计位图 > 构建工作集预算时拒绝启动
```

合并后验证：

```text
GUI off      exit=0
GUI on       exit=0
GUI 低 RAM   exit=0
SearchJob    ALL PASS
unit         通过
```

## 仍未覆盖

```text
高命中截断后的“质量”仍以距离为主
真实数十 GB 构建未执行；现在会被 RAM 守卫拒绝
```

环形规划已增加取消检查点：

```text
AfkPlanner::plan / planChunkCoarse / measureExactRows / measureStrictForCandidates
```

回归：

```text
AFK-CANCEL OK
cancel_response=1.0ms total=37.7ms results=0
```

## 结论

已测试的 CPU/GPU、Java/基岩、严格/密度、四种算法、
缓存开关/构建/ZIP、GUI、环形、取消、低 RAM 组合中，
没有发现新的明显产品级功能失败。

主要问题集中在测试基础设施，并已修复两项；本轮 3 个高风险项
均已补上代码/自动化或守卫。
