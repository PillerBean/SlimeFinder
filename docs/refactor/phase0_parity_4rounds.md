# Phase 0/1 四轮对拍记录（2026-11-15）

> 分支：`refactor-compute-core`
> 状态：只更新副分支源代码；没有安装包、没有 Release、没有改主分支。

## 1. 对拍内容

工具：`tools/refactor/parity_rounds.cpp`

每轮同时检查：

1. `CpuDeterminer` 与 `GpuDeterminer` 的整幅 Bitmap 逐字一致；
2. 抽样坐标与 `SlimeEngine::isSlimeChunk` 一致；
3. 每个区域左上 64×64 小块与独立标量判定逐点一致。

四轮区域：

| 轮次 | 版本 | 区域 | 种子 | 特征 |
|---:|---|---|---:|---|
| 1 | Java | (-200000,300000)+777×333 | -987654321098765 | 远坐标、非 64 对齐宽 |
| 2 | Bedrock | (-8192,-8192)+1024×1024 | 12345 | 1M 区块 |
| 3 | Java | (-1024,-1024)+2048×512 | 42 | 宽范围 |
| 4 | Bedrock | (-5000,7000)+1000×700 | 0 | 偏移、非 64 对齐宽 |

## 2. 第一次运行：发现两个真实边界问题

### 2.1 Java 非 64 对齐宽

```text
ROUND 1 Java-far-odd CPU=OK GPU=OK words_bad=332
sample_bad=0 full64_bad=0
```

原因：GPU 打包字按 64 区块整字输出，最后一字包含宽度之外的位；
新框架的 Bitmap 合同是“只含 [x0,x0+w)”。

修复：`GpuDeterminer` 在接收 words 后按行裁剪最后一字：

```cpp
const int rem = w & 63;
if (rem != 0) {
    const uint64_t mask = (uint64_t{1} << rem) - 1ULL;
    for (int z = 0; z < h; ++z) words[z * wpr + (wpr - 1)] &= mask;
}
```

### 2.2 Bedrock 非 64 对齐宽

```text
ROUND 4 Bedrock-offset words_bad=649
```

原因：`scan_bedrock_words` 的尾部逻辑在 `cxB >= cxEnd` 后只补 `cxA` 的第一个位，
然后 `break`，漏掉最后一字里 b+1..31 的多个有效位。

修复：`src/gpu/slime_kernel.cl` 的尾部循环改为：

```cpp
for (; b < 32; ++b) {
    const int cx = cxBase + b;
    if (cx >= cxEnd) break;
    bits |= (uint)bedrock_is_slime_fast(cx, cz) << b;
}
break;
```

## 3. 修复后的四轮结果

```text
ROUND 1 Java-far-odd   words_bad=0 sample_bad=0 full64_bad=0
ROUND 2 Bedrock-1M     words_bad=0 sample_bad=0 full64_bad=0
ROUND 3 Java-wide      words_bad=0 sample_bad=0 full64_bad=0
ROUND 4 Bedrock-offset words_bad=0 sample_bad=0 full64_bad=0

PARITY ALL PASS (0 failures)
```

## 4. 连续四轮（每轮含 4 个 case）

```text
cycle=1 rc=0 PARITY ALL PASS (0 failures)
cycle=2 rc=0 PARITY ALL PASS (0 failures)
cycle=3 rc=0 PARITY ALL PASS (0 failures)
cycle=4 rc=0 PARITY ALL PASS (0 failures)
```

合计 16 次 case 对拍，全部通过。

## 5. 其他副分支测试

```text
Collector ALL PASS
Planner ALL PASS
Kernel tests 19/19 PASS
```

## 6. 结论

- 四轮对拍不是形式通过：它抓到了两个原有的非 64 对齐边界 bug；
- Java 侧的修复在框架边界（Bitmap 合同）；
- Bedrock 侧的修复在 GPU 内核尾部；
- 修复后 CPU/GPU 位图逐字一致，抽样与小块标量校验一致；
- 仍然没有接入产品、没有安装包、没有 Release。

## 7. 下一步

Phase 2：Materialize SearchPlan（CPU 判定 + Bitmap + CPU Matcher + Collector）
与旧 `SlimeEngine::run` 对拍命中数、排序、取消、进度和线程数。
