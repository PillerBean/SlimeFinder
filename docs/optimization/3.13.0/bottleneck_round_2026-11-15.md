# 2026-11-15 瓶颈轮：CPU 并行 / Java GPU 宿主 / CPU 匹配 / GPU 占用

> 本轮按用户指定顺序调查：
> 1. CPU 并行/SMT 调度
> 2. Java GPU 宿主、回读、结果处理
> 3. CPU 匹配阶段
> 4. GPU 内核硬件占用率
>
> 状态：3.13.7 仍是测试版，正式版为 3.12.3；本轮没有升级/上传正式版。

## 1. CPU 并行 / SMT 调度

Bedrock CPU 4M 扫描，3.13.7 8 链内核：

| 线程 | 时间 | 加速比 | 相对线程数效率 |
|---:|---:|---:|---:|
| 1 | 64.17 ms | 1.00× | 100% |
| 2 | 33.72 ms | 1.90× | 95% |
| 4 | 17.60 ms | 3.65× | 91% |
| 6 | 13.21 ms | 4.86× | 81% |
| 8 | 13.89 ms | 4.62× | 58% |
| 10 | 10.93 ms | 5.87× | 59% |
| 12 | 11.36 ms | 5.65× | 47% |

结论：

- 6 物理核效率约 81%，12 逻辑线程只有约 47%；
- 10 线程在这组测量中略快于 12，但重复运行有 5%~15% 抖动；
- 全模式 6/10/12 线程对比没有稳定赢家：
  - Bedrock 图案 6 线程 14.41 ms、12 线程 15.34 ms；
  - Bedrock 密度 6 线程 14.52 ms、12 线程 13.86 ms；
  - 不能仅凭一次测量改默认线程数。
- 当前 `threads=12` 不是“充分利用”，但也没有一个稳定更优的固定线程数。

未做：

- 线程亲和性绑定（MSVC OpenMP 不直接暴露 worker affinity）；
- 按模式选择线程数（需要稳定 A/B 和 GUI 设置联动）；
- 进程 CPU 时间/每 worker 占用率（本轮临时计数波动太大，未采用）。

## 2. Java GPU 宿主、回读、结果处理

8192×8192、target=1、Java 密度 GPU 会话：

```text
matchDensity 总计：3.57 ms
  CPU 等 GPU（内核+等待）：1.21 ms（34%）
  GPU 等 CPU（回读+筛选+结果）：2.36 ms（66%）
```

阶段日志：

```text
alloc             0.01 ms
dispatch+kernel   0.21 ms
counter read      1.42 ms
hits read         2.49 ms
select/fill       3.53 ms
matchDensity total 3.57 ms
```

2048² 同样明显：

```text
Java 密度 GPU：
  matchDensity 0.71 ms
  CPU 等 GPU 0.19 ms（27%）
  GPU 等 CPU 0.52 ms（73%）
```

### 本轮发现的根因之一

引擎给 GPU 的 `keepCandidates` 是：

```text
targetCount=1 → max(256, 1×4) = 256
```

但 `GpuSession` 在筛选前仍然回读了全部命中：

```text
8192²：hits read n=152739（不是256）
```

`GpuSession` 里其实有“距离桶直方图 + 第二遍只回读最近桶”的裁剪代码，
但当前直方图始终为空：

```text
[gpu] crop-check total=152739 keepTop=256 keepTarget=256
[gpu] crop-plan  maxHits=4000000 cutBucket=-1
```

`cutBucket=-1` ⇒ 无法裁剪，仍然全量回读。

### 尝试与结论

用 `SLIME_GPU_CROP=1` 开启已有裁剪路径做实验：

- 结果命中数一致；
- 但 `cutBucket=-1`，没有真正裁剪；
- Java 密度反而 0.73→0.82 ms（第二遍无收益）；
- 已撤销实验代码，不进入产品。

因此 #2 的下一步不是改宿主排序，而是先修 **GPU 距离直方图/桶写入**，或实现真正的 GPU 侧 top-N。
只优化宿主 `nth_element` 不会有收益，因为瓶颈在“把15万条命中搬回主机”。

## 3. CPU 匹配阶段

8192×8192、target=1：

| 模式 | 总耗时 | 扫描 | 匹配/结果 | 匹配占比 |
|---|---:|---:|---:|---:|
| Java 图案 | 14.47 ms | 10.80 ms | 3.31 ms | 23% |
| Java 密度 | 19.28 ms | 10.53 ms | 8.42 ms | 44% |
| Bedrock 图案 | 178.27 ms | 174.20 ms | 3.54 ms | 2% |
| Bedrock 密度 | 184.62 ms | 175.35 ms | 8.89 ms | 5% |

target=200000、2048²：

```text
Java 图案匹配占 60%
Java 密度匹配占 77%
Bedrock 图案匹配占 11%
Bedrock 密度匹配占 23%
```

跳过扫描（位图复用）后，8192² target=1：

```text
Java 图案：匹配 3.06 ms，占 59%
Java 密度：匹配 8.06 ms，占 78%
Bedrock 图案：匹配 3.27 ms，占 61%
Bedrock 密度：匹配 8.41 ms，占 80%
```

结论：

- Java 大范围/大 target 的主要剩余成本是匹配；
- Bedrock 仍以判定为主，匹配占比小；
- 匹配的固定位图内核上限还没测，不能判断是算法还是内存/调度；
- 下一项应做固定位图、去掉扫描/结果堆后的 `FastMatch` 纯内核基准。

## 4. GPU 内核硬件占用率

当前没有 GPU profiler（Radeon GPU Profiler / ROCm / Nsight 均未接入），
所以不能给出“SM/CU 占用率”百分比。

只能从时间上判断：

```text
8192 Bedrock 图案：CPU 等 GPU 98.9%，GPU 等 CPU 1.1%
8192 Bedrock 密度：CPU 等 GPU 93.9%，GPU 等 CPU 6.1%
8192 Java 图案：  CPU 等 GPU 67.8%，GPU 等 CPU 32.2%
8192 Java 密度：  CPU 等 GPU 33.9%，GPU 等 CPU 66.1%
```

- Bedrock GPU 是内核主导；
- Java GPU 是宿主/回读主导；
- 但“内核主导”不等于“CU 占用率 100%”，没有 profiler 不能下这个结论。

## 5. 本轮结论与下一步

### 优先 1：CPU 并行

- 12 逻辑线程效率约 47%，6 物理核约 81%；
- 10/12 线程没有稳定赢家，暂不改默认；
- 下一步做稳定的线程数 × 模式矩阵、负载均衡和亲和性实验。

### 优先 2：Java GPU 宿主

- 已知 8192 Java 密度 66% 时间在等 CPU；
- 根因是 `hits read n=152739`，而实际只需要 `keepTop=256`；
- 已有裁剪路径因直方图全零无法生效，实验已撤销；
- 下一步先修 GPU 距离直方图或实现 GPU 侧 top-N，而不是继续优化宿主排序。

### 优先 3：CPU 匹配

- Java 大范围/target=200k 时匹配占 60%~77%；
- 下一步做固定位图纯匹配基准（窗口/s、字节/s、指令/节点上限），再决定是否值得改算法。

### 优先 4：GPU 硬件占用

- 缺 profiler，当前只能做时间占比判断；
- 需要引入 GPU 事件/占用率工具，才能回答“CU 是否打满”。

## 6. 当前状态

```text
正式版：3.12.3（Latest）
测试版：3.13.7（Pre-release）
本轮无产品性能改动，只有调查和一次已撤销的 GPU 裁剪实验。
```
