# refactor-compute-core 验收状态汇总

日期：2026-11-15
分支：`refactor-compute-core`
默认开关：`SLIME_NEXT_PIPELINE` 未设置/0（旧引擎）

## 1. 已通过

| 项目 | 证据 |
|---|---|
| Phase0 CPU/GPU bitmap | 4 轮 × 4 case，mismatch=0 |
| Materialize CPU | 旧/新结果、totalFound、排序一致 |
| FastMatch | 图案/密度结果一致 |
| 多线程 | 1/4/12 线程结果一致 |
| 进度/取消 | 单调进度、pre/mid cancel |
| 分块 | chunk=37/100，全局 rank 一致 |
| 2D 分带 | 完整/部分/无缓存一致 |
| Banded 语义 | totalFound 对齐旧 runBanded |
| Banded 性能 | 1024² 全缓存约 0.38~0.49× 旧耗时 |
| Fused GPU | 512²/2048² 图案/密度结果一致 |
| Fused 进度/取消 | 2048² SearchTask 中间进度/cancel 通过 |
| 大范围 | 2048²/1500² 结果 SAME |
| SearchTask 实时计数 | liveScanned/liveTotal 同口径 |
| 构建模式 | 回退旧路径，entries=1744=独立判定 |
| 环形/AFK | feature flag 开关结果 SAME |
| SearchTask 随机矩阵 | 24 case，结果/进度/live 全通过 |
| 速度统一采样 | Fused/Banded/Chunked 见 `performance_status.md`；CPU 算法优化见 `algorithm_optimization.md` |
| CPU 算法优化 | Java pattern 1.21ms < 旧1.76ms；Java density 2.22ms ≈ 旧2.29ms |
| GPU 密度优化 | keepCandidates + 原始 totalFound；Java density 0.74ms vs 旧GPU 0.84ms |
| 已知问题 | Banded 取消偶发崩溃已修复（vert 缓冲按 wpr*64 分配），phase8 8/8 通过 |
| 默认行为 | flag 关闭仍走旧引擎 |

## 2. 性能记录

```text
Fused 2048² 预热后：
  Java pattern  0.59×
  Java density  1.05×
  Bedrock density 0.27×
  Chunked 0.24×
```

Java density 在 5% 门槛边界；CPU Materialize/Chunked 仍慢，
因此 SearchTask 只开放 Fused/Banded，CPU 新路径只在测试工具中。

## 3. 剩余缺口

```text
Fused 512² 只有开始/结束进度（调用约 1ms）
Fused 512² 取消只能等 GPU 调用返回
GUI 实时扫描计数在 Fused 上是估算/结束后校正
```

这些不阻塞当前“默认关闭的并行路径”，但阻塞全量切换。

## 4. 结论

```text
不切换默认
不删除旧路径
不打包
不发布
```

新旧并行状态可保留，待决定是否继续攻 512² 细粒度进度或直接进入更长周期灰度。

## 5. 回归命令

```text
build\slime_next_phase2_parity.exe
build\slime_next_phase2_threads.exe
build\slime_next_phase2_progress.exe
build\slime_next_phase3_chunked.exe
build\slime_next_phase3_banded.exe
build\slime_next_phase4_fused.exe
build\slime_next_phase5_feature_flag.exe
build\slime_next_phase5_matrix.exe
build\slime_next_phase6_large.exe
build\slime_next_phase7_pattern_perf.exe
build\slime_next_phase8_compat.exe
build\slime_next_phase9_banded_perf.exe
build\slime_next_parity_rounds.exe
```
