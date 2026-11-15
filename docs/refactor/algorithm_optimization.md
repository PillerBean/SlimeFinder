# CPU 算法优化记录

日期：2026-11-15
分支：`refactor-compute-core`
目标：先把 CPU 路径做到不弱于旧 CPU，再谈合作/流程。

## 1. Java 扫描：直接写位图字

旧新路径原来是：

```text
fillSlimeRowJava -> uint8_t 行
再逐 bit 打包 -> uint64_t 位图
```

现在 Java 可用时：

```text
fastjava::scanRowJava(rowBase, x0, w, bitmap.row(z))
```

直接生成 64 位位图字，省掉逐字节/逐 bit 中间步骤。

阶段变化（2048² Java pattern）：

```text
scan: 2.25 ms -> 0.76~0.90 ms
```

非 64 对齐宽度仍由 FastJava 尾部清零保证；Phase0 parity 通过。

## 2. 严格图案：字级候选掩码

旧实现每个锚点行调用 `strictRowAvx2`，得到逐锚点 flags。

新实现：

```text
对每个 64 锚点块，按图案有效格构造候选位掩码
Green: cand &= bits
White: cand &= ~bits
一次枚举候选位
```

2048² Java pattern：

```text
match: 1.54 ms -> 0.26 ms
materialize total: 4.00 ms -> 1.21 ms
```

对照旧 CPU：

```text
旧 CPU     1.76~1.89 ms
新 CPU     1.19~1.22 ms
```

CPU 图案路径现在**快于旧 CPU**。

已覆盖 2×2、3×3、4×4、2×5、3×6、4×2、2×3 及白格/忽略格，
Phase14 全部 SAME。

## 3. 密度：滑动列和 + hit mask + 线程本地 Collector

新实现：

```text
vertSumDiffAvx2 维护 g 行列和
densityHitMaskAvx2 生成命中位掩码
只对命中位计算窗口计数
每个 OpenMP 线程使用本地 Collector，最后 merge
```

2048² Java density：

```text
match: 2.86 ms -> 1.32 ms
materialize total: 3.71 ms -> 2.43 ms
```

对照旧 CPU：

```text
旧 CPU 约 2.17~2.32 ms
新 CPU 约 2.43 ms
```

仍有约 10% 差距，但已从约 1.7× 降到约 1.1×。

## 4. 当前 CPU 状态

| 场景 | 旧 CPU | 新 CPU | 状态 |
|---|---:|---:|---|
| Java pattern 2048² | 1.76~1.89 ms | 1.19~1.22 ms | 新更快 |
| Java density 2048² | 2.17~2.32 ms | 2.43 ms | 新慢约 10% |
| Bedrock pattern 512² | 4.30 ms | 4.22 ms | 基本持平 |
| Bedrock density 512² | 4.41 ms | 4.32~4.53 ms | 基本持平 |

## 5. 验证

```text
PHASE2 ALL PASS
PHASE2-THREADS ALL PASS
PROGRESS-CANCEL ALL PASS
PHASE3-CHUNKED / BANDED ALL PASS
PHASE4-FUSED ALL PASS
PHASE5-PLAN-MATRIX ALL PASS
PHASE6-LARGE ALL PASS
PHASE7-PATTERN-PERF ALL PASS
PHASE8-COMPAT ALL PASS
PHASE9-BANDED-PERF ALL PASS
PHASE11-SEARCHTASK-RANDOM 24 case ALL PASS
PHASE14-PATTERN-VARIANTS ALL PASS
PARITY ALL PASS
```

## 6. 剩余算法工作

```text
Bedrock 行扫描的位图打包
Java density 最后约 10%
CPU 大范围/低线程性能
GPU density 2-band 固定开销
```

流程层保持冻结，不新增 Hybrid 实现。
