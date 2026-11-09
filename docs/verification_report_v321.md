# v3.2.1 验证报告（闪退修复 + GPU 开关）

日期：2026-09-08（第二版：吸收实测反馈修订）| 基于 v3.2.0
机器：AMD Ryzen 5 5600 + RX 6750 GRE 10GB（GPU 可用环境；无驱动真机无法在本机模拟，已注明模拟方式）

## 一、修复内容（按任务书，经实测反馈修订）
1. **GUI GPU 开关（P0）**：主界面新增「启用 GPU 加速（实验性，默认关闭）」复选框；
   勾选状态即时写入 `config.json gpu.enabled`，启动读取同步；**默认关闭**
   （不再如 v3.2.0 自动启用/预热）。
2. **无 OpenCL 驱动环境闪退（P0）**：v3.2.0 启动即初始化 GPU，异常驱动环境有崩溃风险。
   修复（两轮）：
   - 仅当「用户勾选 且 检测可用」才注入 GPU；引擎 `runGpuScanStage` 显式空指针/
     available=false 保护直接回 CPU；`GpuSession::init` 全部 C++ 异常兜底转失败返回；
   - **第二轮（实测“仍闪退”后）**：早期“启动即后台线程探测”方案仍会在无/异常驱动
     环境触发 OpenCL 驱动加载/初始化，该级原生崩溃无法捕获且后台线程同样击穿进程
     → 改为**启动阶段完全不接触 OpenCL**；GPU 检测收敛为「用户勾选启用时同步执行一次」
     （`ensureGpuScanOnce`）；默认关闭路径启动→搜索全程零 OpenCL 接触。
3. **检测时机（P1 修订）**：随第 2 条改为勾选时单次检测；结果与原因记日志并显示于
   状态文本（未启用/正在检测/可用/不可用）。
4. **日志（P2）**：检测开始/结果、启用/关闭、勾选但不可用/未就绪回退原因全部入 `logs/`；
   main.cpp 新增常驻 VEH（原生崩溃打印异常码/模块/调用栈到 stderr，配合 crash dump 定位）。

## 二、验证结果（本机）
| 场景 | 方式 | 结果 |
|---|---|---|
| 默认关闭（config false，全新构建） | GUI 自检（勾选前不接触 OpenCL；[10] 真实搜索冒烟走 CPU） | exit 0 ✔ |
| config true（勾选+可用 → 检测+注入） | 自检 [9]（2048² GPU 端到端）+ [10] startSearch 全链路 | exit 0 ✔ |
| 用户关闭 GPU（模拟无 GPU 驱动影响面） | `SLIME_GPU=0` 自检 | exit 0 ✔ |
| **真实 GUI 搜索冒烟（新增 [10]）** | startSearch 全链路：SearchTask 后台线程+进度回调+结果渲染+performance.log（512×512 Java 密度） | exit 0 无闪退 ✔ |
| 勾选但不可用 → 自动回退 | 引擎分支 + 日志记录 | 代码路径+日志 ✔（真机不可模拟，已注明） |
| config 写读往返 / 检测完成 | 自检 [9] | ✔ |
| 全量回归（clean 全量重建后） | 单元 13/13、111/111、GUI 自检 ×3（开/关）、冒烟、probe PASS | ✔ |

## 三、交付
- 源码：`src/gui/MainWindow.{h,cpp}`（复选框/勾选检测/config 读写/自检 [9][10]）、
  `src/gpu/GpuSession.cpp`（init 异常兜底）、`src/core/SlimeEngine.cpp`（空指针/不可用保护）、
  `src/main.cpp`（VEH 崩溃现场输出）
- 版本：v3.2.1（main.cpp / kAppVersion / install.nsi / CMake project VERSION 3.2.1）
- 文档：`user_manual.md` §5（默认关闭/勾选检测说明）、`architecture.md` §9.4、本报告
- 包：`dist/SlimeFinder-3.2.1-win64.zip`（config.json 默认 `gpu.enabled: false`；
  clean 全量重建，build 与 dist exe 哈希一致）

## 四、说明
- 驱动层面的原生崩溃（DLL 初始化 AV 级故障）无法被 C++ 异常捕获；默认关闭 + 启动零
  OpenCL 接触 + 仅显式勾选时单次检测，已把暴露面收敛为“用户主动勾选”的单点；
  如需彻底隔离可后续做“子进程探测/启用引导”（超出本版本范围）。

## 五、第三轮修订（2026-09-09，真实搜索闪退复现与修复）
- **复现**：新增自检 [10]「真实 GUI 搜索冒烟」（startSearch 全链路）后，Release 构建稳定
  复现 0xC0000005；Debug 构建正常 → 优化相关缺陷。VEH + MAP + 反汇编定位：
  崩溃为 `runImpl` 内对 `SearchOptions::gpuScan` 的虚调用 `[vtable+8]`，且 worker 线程
  收到的 options 中该字段为未初始化/被覆写值（其余字段正常；Debug 布局下不可见）。
- **修复**：
  1. `SearchOptions::gpuScan` 字段从结构末尾移至中部（远离结构边界/尾部越界写窗口），
     消除跨线程拷贝后该槽位不稳定；clean 全量重建排除陈旧目标文件；
  2. 修正 config=true 启动路径：启动不接触 OpenCL，但「勾选启用」若未触发状态变化
     （config=true 启动即勾选），在 **startSearch 首次使用前兜底检测**（ensureGpuScanOnce）；
  3. 自检 [9] 检测触发改为显式调用 + config 往返断言改为直接查文件（避免 SLIME_GPU
     环境变量干扰）；
  4. 移除全部诊断代码；新增 C4244 告警清零。
- **验证（Release clean 全量）**：GUI 自检 exit 0 ×8（config false ×5、config true ×2、
  SLIME_GPU=0 ×1，均含真实搜索冒烟与 GPU 注入搜索：日志显示 262144 区块 GPU 扫描完成）；
  单元 13/13、111/111、probe PASS、冒烟存活。
