# 流程副分支集成（flow-integration）

基线：`flow-runtime` @ 674bc21

已合并：

```text
flow-hit-policy     dbc864e  D1 高命中预估 / GPU cap 截断不回退 CPU
flow-gui-contract   86c29cf GUI 入口迁移 SearchProgress/SearchReport
flow-ring-job       fe461fd 环形规划执行体接入 SearchJob
flow-memory-budget  e741446 RAM 预算
```

## 集成后验证

```text
slime_memory_budget_tests   RAM-BUDGET ALL PASS
slime_hit_estimate_tests    HIT-ESTIMATE OK
slime_gpu_truncation_tests  GPU-TRUNCATION OK
slime_search_job_tests      ALL PASS
slime_cache_store_tests     OK
slime_cache_band_tests      OK / totalFound=8
slime_gpu_resume_tests      GPU-RESUME OK
slime_tests --unit          通过
slime_ocl_probe             PASS
GUI 自检（GPU off）         exit=0
```

## 合并策略

- 副分支只合入 `flow-integration`；
- `main` 仍是 3.13.8，尚未合并；
- 安装包统一命名规则尚未恢复；
- 开发期仍使用带分支前缀的包名。

## 已知验收缺口

```text
环形搜索没有独立单元测试
GUI 自检不覆盖完整环形规划
高命中截断的“质量”仍以距离为主，有效格数只作为结果属性
```

进入 main / 发布前应补一次真实 GUI 环形与高命中场景验收。
