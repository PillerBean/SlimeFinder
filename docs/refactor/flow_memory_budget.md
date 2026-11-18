# RAM 预算（flow-memory-budget）

分支：`flow-memory-budget`，基于 `flow-runtime`。

## 语义

设置里的“内存预算（RAM，下次搜索）”只表示**主机内存条提供的易失内存**：

```text
包含：分块位图、pack 解码 LRU、GPU 主机侧回读/命中缓冲
不包含：磁盘缓存容量 cacheMaxMB
不包含：显存 VRAM
```

## 取值

```text
0 = 自动
auto = clamp(当前可用物理内存 × 50%, 512MB, 4GB)
>0 = 用户指定 MB
     并收敛到 max(64MB, 可用物理内存 - 512MB)
```

例子：

```text
可用 2GB  → 1GB
可用 8GB  → 4GB
可用 16GB → 4GB
```

运行中修改设置只对下一次任务生效；当前任务使用启动时的快照。

## 分配

```text
分块位图/并发      50%
pack 解码 LRU      20%
构建工作集参考     15%
GPU 主机缓冲       10%
保留                5%
```

`pack LRU < 32MB` 时自动关闭 pack 解码缓存。

## 已接入的流程

```text
SearchOptions::memBudgetBytes       → 分块位图预算/并发
CacheStore::setPackCacheLimitBytes   → pack 解码 LRU
SearchOptions::gpuHostBudgetBytes    → GPU 回读/命中结果 cap
```

`buildWorkingBytes` 目前作为构建阶段的保留预算/日志口径；
缓存构建本体仍以 `cacheMaxMB` 作为磁盘容量上限，完整 `m_cache`
的驻留还没有改成分段落盘。

## 日志

每次搜索启动时写：

```text
RAM预算: 自动/手动 总量=...MB 分块=...MB packLRU=...MB 构建=...MB GPU主机=...MB
```

被可用内存收敛时追加：

```text
（已按可用内存收敛）
```

## 测试

```text
slime_memory_budget_tests   RAM-BUDGET ALL PASS
slime_cache_store_tests     OK
slime_cache_band_tests      OK
slime_search_job_tests      ALL PASS
slime_tests --unit          通过
slime_ocl_probe             PASS
GUI 自检（GPU off）         exit=0
```

## 发布命名

开发期安装包：

```text
flow-memory-budget-SlimeFinder-3.13.9
```

所有副分支合并后才恢复统一命名。
