# 交付验证报告（verification_report.md）

交付物：SlimeFinder v1.0.0（C++20 / Qt 6.11.2 / MSVC / OpenMP 重构版）
验证环境：Windows 11 x64，VS2026 MSVC 19.51/19.52，Qt 6.11.2 msvc2022_64，12 逻辑核心，Release /O2 + /openmp

## 1. 准确度（对照已验证参考实现 SlimeChecker.exe）

运行 `tests/verify_against_reference.py`（111 项，全部通过）：

| 类别 | 内容 | 结果 |
|---|---|---|
| Java 扫描 | 10 种子（含 ±2^63）× 6 区域（含 int32 溢出边界坐标 ≈ ±21.4 亿） | 坐标集合完全一致（60 项） |
| 基岩版扫描 | 3 个区域（约 3.7 万~3.7 万+区块） | 坐标集合完全一致（3 项） |
| 严格图案匹配 | 7 种图案（2×2 全绿验收场景、含白/灰格、5×5 等）× 区域/种子 | 窗口集合完全一致（18 项） |
| 密度搜索 | g∈{2,3,5,8}、阈值∈{0.05…0.5}（含验收场景 g=5/thr=0.3） | 窗口集合 + 各窗口密度值一致（14 项） |
| GUI 最近 N 排序 | Python GUI 逻辑（hypot+稳定排序）重建期望，逐条比对 | 逐条一致，含“智能范围+提前停止”组合（16 项） |

验收用例抽查：
- Java 种子 0、区块 (-2,-1)：参考=非史莱姆，本程序=非史莱姆 ✓
- 基岩版区块 (-1,0)：参考=史莱姆，本程序=史莱姆 ✓
- Java 种子 0、区块 (0,0)：非史莱姆（双方一致）

单元测试（`slime_tests.exe --unit`，9 组全部通过）：32/64 位溢出模拟、LCG 与手工重放一致、
密度/严格匹配对朴素逐窗验证、螺旋顺序=光栅顺序、提前停止=全局最近 N、
缓存合并/覆盖/文件往返、取消响应 <5 s、CacheManager API、旋转匹配一致性。

## 2. 性能（Release 实测）

| 场景 | 实测 | 任务书要求 | 结论 |
|---|---|---|---|
| Java 扫描 1000×1000 区块 | 单核 3.1 ms | < 0.1 s | ✓ |
| Java 扫描 10000×10000 区块 | 单核 0.33 s（≈ 3.0 亿区块/秒） | < 10 s（单核） | ✓（快 30 倍） |
| Java 单核扫描速度 | ≈ 3.0 亿区块/秒 | ≥ 1 亿区块/秒 | ✓（3 倍） |
| 多核加速（12 线程） | 1 亿区块 0.054 s，≈ 6.1× | 8 核 ≥ 4× | ✓ |
| 缓存加载 100 万条目 | 45 ms | 100 万 < 0.5 s | ✓（10 倍余量） |
| 基岩版 2000×2000（4M 区块） | 单核 17.6 s / 多核 2.7 s | — | MT19937 初始化开销；缓存覆盖后可即时完成 |
| 密度 g=5 匹配 2000×2000 | ≈ 0.1 s（含扫描 12 ms） | — | — |
| 严格 2×2 匹配 2000×2000 | ≈ 46 ms（含扫描） | — | — |
| 程序启动 | < 1 s（GUI 出现） | < 1 s | ✓ |
| UI 响应 | 搜索在后台线程运行，进度 0.5% 步进 + 速度 + ETA | 永不阻塞 | ✓ |

## 3. 稳定性

| 检查 | 结果 |
|---|---|
| 内存压力：单进程连续 300 次完整搜索（Java/基岩混合、含缓存合并） | 工作集全程平稳 ≈ 7 MB，无增长，正常退出 |
| 取消机制（区域 8 亿区块，2 线程，扫描中取消） | 取消后 0.08 s 内返回，不保留部分结果，无崩溃 |
| GUI 冒烟：启动 → 停留 → 强关（重复 3 次） | 进程存活、日志正常生成（UTF-8） |
| 关闭窗口时线程安全退出 | `SearchTask` 析构 + `closeEvent` 取消并 join |
| 重复点击“开始搜索” | 运行时按钮禁用 + 二次启动守卫提示 |
| 崩溃转储 | 已安装 `SetUnhandledExceptionFilter`（写入 crashes/*.dmp + crash_report.txt） |

## 4. 交付物清单核对

| 交付项 | 位置 |
|---|---|
| 项目源码（core/gui/utils 分层） | `src/` |
| CMakeLists（Release/Debug、Qt6、OpenMP） | `CMakeLists.txt` |
| Release 可执行文件 | `build/SlimeFinder.exe`；可分发目录 `dist/SlimeFinder/`（含 Qt 运行库） |
| 引擎测试/基准/对照 CLI | `build/slime_tests.exe` |
| 代码架构说明 | `docs/architecture.md` |
| 用户使用说明 | `docs/user_manual.md` |
| 编译与打包指南 | `docs/build_instructions.md` |
| NSIS 安装脚本 | `scripts/install.nsi` |
| 单元测试 | `tests/test_slime.cpp`（9 组） |
| 交叉验证脚本 | `tests/verify_against_reference.py`（111 项） |
| 旧版缓存转换脚本 | `scripts/convert_pkl_cache.py` |
| 日志示例 | `dist/SlimeFinder/logs/SlimeFinder_2026-09-04.log` |

## 5. 复现命令

```bat
build\slime_tests.exe --unit                       :: 单元测试
python tests\verify_against_reference.py           :: 111 项对照
build\slime_tests.exe --bench                      :: 性能基准
build\slime_tests.exe --stress 300                 :: 内存稳定性
```
