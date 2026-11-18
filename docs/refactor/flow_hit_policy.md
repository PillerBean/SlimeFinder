# 高命中策略（flow-hit-policy）

分支：`flow-hit-policy`，基于 `flow-runtime`。

## 1. 执行前命中量估算

新增：

```text
SlimeEngine::HitEstimate
SlimeEngine::estimateHits(options, maxSamples = 256)
```

做法：

```text
在锚点范围内做分层采样
严格图案逐格判定；密度窗口逐格计数
命中率 × 候选窗口数 = 预计命中数
估算最多做约 1M 次格判定
```

结果包含：

```text
candidateWindows
sampledWindows
sampleHits
hitRate
estimatedHits
```

### 警告阈值

```text
>= 1,000,000  反馈面板警告
>= 4,000,000  模态确认，用户可取消
```

非界面模式（自检/quiet）只记录，不弹模态框。

## 2. 运行时截断

GPU 命中数超过 `maxHits` 时：

```text
旧：置 truncated，然后返回 false，引擎回退 CPU
新：置 truncated，保留已回传的命中，引擎继续，不回退 CPU
```

- 结果转换时仍使用距离 `d2` 排序/裁剪；
- 密度结果保留 `density = count / g²`；
- `out.truncated = true`；
- GUI 反馈面板显示截断说明；
- `totalFound` 仍为 GPU 报告的真实命中总数（超过已回传数量）。

## 3. 质量语义

当前顺序：

```text
距离更近优先
密度/有效格子数作为结果属性保留
```

截断后不保证“全局最近 N”，因为 cap 之前的命中集合已经不完整；
这正是用户选择“继续而不回退 CPU”的代价。

## 4. 验证

```text
slime_hit_estimate_tests
  1x1 Green 预计 24576（256² 区域），采样 256
  density 估算可运行
  HIT-ESTIMATE OK

slime_gpu_truncation_tests
  fake GPU 返回 500 条 + total=1000000 + truncated
  引擎 ok=1 gpuUsed=1 truncated=1 fallback='' results=50
  GPU-TRUNCATION OK

slime_search_job_tests      ALL PASS
slime_cache_band_tests      OK
slime_tests --unit          通过
GUI 自检（GPU off）         exit=0
```

## 5. 发布命名

```text
flow-hit-policy-SlimeFinder-3.13.9
```

所有副分支合并后才恢复统一命名。

## 6. 已知限制

```text
估算为采样近似，不是硬保证
GPU cap 保留的是已回传集合中的最近结果，不是完整全局最近 N
细粒度“有效格子数”只作为结果属性，未改变距离优先排序
```
