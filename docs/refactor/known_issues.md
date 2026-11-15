# 已知问题

## 1. Banded 取消压力测试偶发崩溃 —— 已修复

状态：**已修复**

现象：

```text
phase8 / banded-cancel
退出码 -1073740940 (0xC0000409)
约 1/6 次复现（threads=2）
```

根因：

```text
vertSumDiffAvx2 按 64 列整字写回 vertSum
它写 wpr*64 个 int，而不是只写 w 个 int
新 Matcher 的 vert 缓冲只按 w 分配
```

Banded 的 tile 宽度常是 68 这类非 64 对齐值，
于是 `vertSumDiffAvx2` 越界写堆，破坏后续分配/对象，
表现为延迟、偶发的 0xC0000409。

修复：

```cpp
std::vector<int32_t> vert(static_cast<size_t>(wpr) * 64, 0);
```

验证：

```text
phase8 连续 8 次：8/8 ALL PASS
```

## 2. 其他

- Fused GPU 常见范围进度只有开始/结束（短任务）；
- GUI 实时扫描计数在 Fused 上为估算/结束后校正；
- CPU 大范围/低线程尚未系统验收。
