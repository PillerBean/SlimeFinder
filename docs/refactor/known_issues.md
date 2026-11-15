# 已知问题

## 1. Banded 取消压力测试偶发崩溃

状态：**未解决**

现象：

```text
phase8 / banded-cancel
退出码 -1073740940 (0xC0000409)
输出停在 task start / 无输出
约 1/6 次复现（threads=2）
```

已排除/验证：

```text
CpuExactMatcher 单独循环 20 次：稳定
CacheManager 填充 Bitmap + matcher 循环：多数稳定
MatchLimits 分块 matcher 循环：稳定
Banded 普通不取消：多数稳定
```

仍需继续定位：

```text
SearchTask 取消 + Banded 的组合
OpenMP 低线程数（2）下的重复运行
```

影响：

```text
SLIME_NEXT_PIPELINE 默认关闭
Banded 仍可手动开启，但取消路径不能算验收通过
```

在修复前：

```text
不切换默认
不宣传 Banded 取消已验收
```

## 2. 其他

- Fused GPU 常见范围进度只有开始/结束（短任务）；
- GUI 实时扫描计数在 Fused 上为估算/结束后校正；
- CPU 大范围/低线程尚未系统验收。
