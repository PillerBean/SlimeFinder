# Phase 2 第二步：FastMatch / 多线程 / 进度 / 取消

> 分支：`refactor-compute-core`
> 只更新副分支源代码；没有安装包、没有 Release、没有改主分支。

## 1. FastMatch 接入

`CpuExactMatcher` 已从逐锚点朴素循环改为：

```text
严格图案：strictRowAvx2
精确密度：densityWindowCountsAvx2
```

仍保留严格的输入边界检查：列数超过 64、非精确密度、旋转图案等会明确返回错误。

### 对拍

512×512，target=50，单线程：

| case | 旧路径 | 新路径 | 结果 |
|---|---|---|---|
| Java 图案 | 24/24 | 24/24 | SAME |
| Java 密度 | 50/586 | 50/586 | SAME |
| Bedrock 图案 | 33/33 | 33/33 | SAME |
| Bedrock 密度 | 50/647 | 50/647 | SAME |

结果坐标、总数、density、排序一致。

### 性能变化（原型，不代表产品最终）

```text
Java 密度：
  朴素 Matcher 约 7.31 ms
  FastMatch Matcher 约 1.52 ms

Java 图案：
  朴素约 1.58 ms
  FastMatch 约 1.20 ms
```

## 2. 多线程

`CpuDeterminer` 和 `CpuExactMatcher` 都走 OpenMP；Collector 线程安全。

1/4/12 线程对拍：

```text
Java pattern   t=1  0.36 / 1.28 ms
Java pattern   t=4  1.33 / 0.57 ms
Java pattern   t=12 1.36 / 0.46 ms

Java density   t=1  0.71 / 1.68 ms
Java density   t=4  0.68 / 0.58 ms
Java density   t=12 1.47 / 0.68 ms

Bedrock pattern t=1  5.32 / 5.24 ms
Bedrock pattern t=4  4.78 / 1.53 ms
Bedrock pattern t=12 6.33 / 1.19 ms

Bedrock density t=1  5.56 / 5.13 ms
Bedrock density t=4  4.75 / 1.77 ms
Bedrock density t=12 6.41 / 1.29 ms
```

格式：旧路径 / 新路径；全部 `SAME`。

注意：这是 512² 小范围，线程启动/调度占比很高；不能直接当作大范围吞吐结论。

## 3. 进度

`MaterializePipeline::run` 新增 `ProgressFn`：

```text
0.00      开始扫描
0.00~0.75 扫描阶段
0.75      扫描完成
0.75~1.00 匹配阶段
1.00      完成
```

进度由 `emitProgress` 串行化并保证单调；扫描/匹配内部的并发回调不会让进度倒退。

测试：

```text
progress: ok=1 scan=1 match=1 final=1.000 monotonic=1
```

## 4. 取消

两个场景：

```text
pre-cancel：启动前 cancel=true
mid-cancel：进度到 0.2 时置 cancel=true
```

结果：

```text
pre-cancel: ok=0 cancelled=1 results=0
mid-cancel: ok=0 cancelled=1 results=0
```

取消后不返回部分结果，也不再继续匹配。

## 5. 当前测试汇总

```text
Phase2 search parity       : ALL PASS
Phase2 threads             : ALL PASS
Phase2 progress/cancel     : ALL PASS
Phase0 parity 4 cycles     : ALL PASS
Planner                    : ALL PASS
Collector                  : ALL PASS
Kernel tests               : 19/19 PASS
```

## 6. 仍未做

- Phase 3：分块/分带；
- Phase 4：FusedGpuPattern/FusedGpuDensity；
- 缓存读取/构建；
- smartRange/stopAtTarget/best-density；
- 旋转图案；
- 接入产品路径；
- 打包/Release。

当前仍然只是副分支原型。
