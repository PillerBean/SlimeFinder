# 编译指南（build_instructions.md）— v3.3.10.12.9

## 1. 依赖环境

| 组件 | 要求 | 说明 |
|---|---|---|
| 操作系统 | Windows 10/11 x64 | 目标为 MSVC 工具链 |
| 编译器 | MSVC 2022（v143 及更高；本工程在 VS2026 14.51/14.52 验证） | C++ 桌面负载 |
| CMake | ≥ 3.16（Qt 自带 CMake 亦可） | Ninja / VS 生成器均可 |
| Qt | Qt 6.11.2 MSVC 2022 64-bit（`C:\Qt\6.11.2\msvc2022_64`） | 只需 Widgets |
| OpenMP | MSVC 自带（`/openmp`） | CMake `find_package(OpenMP)` |
| OpenCL（v3.2.0 GPU 加速） | **无需任何 SDK** | 头文件已随源码（`third_party/OpenCL-Headers`）；运行期动态加载系统 OpenCL.dll/驱动 ICD；无 GPU 或驱动缺失自动纯 CPU |

> Qt 官方仅提供动态链接 msvc 包；静态 Qt 需自行源码构建（见 §5），
> 默认交付为动态链接 + `windeployqt` 部署目录。
> v3.2.0 额外说明：GPU 是可选增强——构建不含 OpenCL 链接依赖，任何机器均可编译；
> 运行机器的 GPU 需带 OpenCL 驱动（NVIDIA/AMD/Intel 现代驱动均内置；AMD 部分机器
> 需驱动安装正确注册 ICD，本工具已内置 DriverStore 直连回退）。

## 2. 编译选项说明（v3.1.0）

| 选项 | 值 | 说明 |
|---|---|---|
| 优化 | `/O2`（MSVC） | Release |
| OpenMP | `/openmp` | 扫描并行 |
| 链接时代码生成 | `/GL` + `/LTCG`（Release 自动） | 全程序优化，约 +3% |
| 源码编码 | `/utf-8` | 中文源码/字符串 |
| C++ 标准 | C++20（`-std:c++20`） | CMake 自动 |
| SIMD 架构指令 | 默认不全局开启（保持 SSE2 兼容） | 见 §2.1 |

### 2.1 SIMD（AVX2）编译说明（v3.3.0 更新）
- v3.3.0 有**两个 AVX2 独立编译单元**，其余文件仍保持 SSE2 兼容（旧 CPU 可运行）：

  | 文件 | 内容 | 运行时门控 |
  |---|---|---|
  | `src/core/FastMatch.cpp` | 严格匹配整行 4 窗口位并行、列垂直和 AVX2 行差分 | `cpuHasAvx2()`（CPUID 0/1/7 + XCR0） |
  | `src/core/FastMt.cpp` | 基岩版 4/8/16/32 通道批量 MT19937 + 连续扫描 | 同上；`SLIME_FORCE_SCALAR=1` 可强制标量 |

  ```cmake
  # CMakeLists.txt（已内置）
  set_source_files_properties(src/core/FastMatch.cpp PROPERTIES COMPILE_OPTIONS "/arch:AVX2")
  set_source_files_properties(src/core/FastMt.cpp    PROPERTIES COMPILE_OPTIONS "/arch:AVX2")
  ```
  头文件（`FastMatch.h` / `FastMt.h`）不含任何 SIMD 类型，可被任意 TU 安全包含；
  两个 .cpp 均以 `#if defined(__AVX2__)` 保护，未开启 AVX2 时自动编译为标量回退。
- GCC/Clang 等价写法：仅给这两个文件加 `-mavx2`（其余文件保持基线指令集）；
  `-flto`（对应 MSVC `/GL`+`/LTCG`）与链接期优化一致。
- 未使用全局 `-march=native`/`/arch:AVX2`，避免失去旧 CPU 兼容性。

## 3. 构建步骤（Release）

```bat
call "…\VC\Auxiliary\Build\vcvars64.bat"
cd SlimeFinder
cmake -S . -B build -G Ninja ^
      -DCMAKE_BUILD_TYPE=Release ^
      -DCMAKE_PREFIX_PATH=C:/Qt/6.11.2/msvc2022_64 ^
      -DCMAKE_MAKE_PROGRAM=C:/Qt/Tools/Ninja/ninja.exe
cmake --build build
```
产物：`build/SlimeFinder.exe`、`build/slime_tests.exe`。
Debug：`CMAKE_BUILD_TYPE=Debug`（不启用 /O2、/GL、/LTCG、/openmp 等）。

### 2.3 常见问题
| 现象 | 解决 |
|---|---|
| `Qt6 not found` | `CMAKE_PREFIX_PATH` 指向含 `lib/cmake/Qt6` 的 Qt 目录 |
| `Cannot find vcvars` | 未在 VS 开发者命令行中执行 |
| 中文乱码 | 源码为 UTF-8；编译选项已含 `/utf-8`，无需额外处理 |
| OpenMP 找不到 | MSVC 自带 `/openmp`；CMake 需要 `find_package(OpenMP REQUIRED)` |

## 3. 测试与验证

### 3.1 单元测试（引擎自检）
```bat
build\slime_tests.exe --unit            :: 全部 19 组
build\slime_tests.exe --unit 14         :: 仅第 14 组（调试用；算法模式）
build\slime_tests.exe --unit 15         :: 仅第 15 组（基岩 AVX2 批量 MT19937）
build\slime_tests.exe --unit 17         :: 仅第 17 组（真实 GPU 密度匹配一致性；无设备/未实现时跳过）
build\slime_tests.exe --unit 19         :: 仅第 19 组（v3.3.7 分块提前停止 + 真实扫描计数）
```
覆盖：溢出模拟、LCG 一致性、密度/严格对朴素逐窗、螺旋/提前停止自洽、
缓存流程、取消、旋转匹配、缓存容量剪枝、分块等价、分块增量缓存、
[13] GPU 引擎等价（无 OpenCL 设备时自动跳过）、
**[14] 分层/快速估算模式（无损一致性、稀疏加速、比例激进子集性、采样位置与估算值、
1/s 相关性规律、精算采样相关性 1.00）**、
**[15] 基岩版 AVX2 批量 MT19937（与引擎逐区块判定一致、AVX2 vs 纯标量、
单核吞吐 ≥400 万/秒、引擎级 AVX2/标量结果集合一致）**、
**[16] 严格匹配字级位并行（64 锚点/批）**、
**[17] GPU 密度匹配一致性（`GpuScan::matchDensity`，当前默认实现返回 false → 跳过）**、
**[18] GPU 引擎侧集成（模拟提供者：堆序/回退/启用条件矩阵/分块锚点域）**。

验证 AVX2 标量回退（对照口径）：
```bat
set SLIME_FORCE_SCALAR=1   :: 基岩批内核强制标量（结果必须与 AVX2 完全一致）
set SLIME_AVX2_STRICT=1    :: 强制启用严格匹配 SIMD 内核（默认关闭，见验证报告 §4.1）
```

### 3.1a GPU 探测/一致性/基准（v3.2.0，需 `slime_gpu`/`slime_ocl_probe` 目标）
```bat
build\slime_ocl_probe.exe            :: 设备检测 + 一致性 + 基准 + 引擎对比
build\slime_ocl_probe.exe --long     :: 追加更大基准场景
set SLIME_GPU_DEBUG=1                :: 设备发现详细日志（stderr）
set SLIME_GPU_MODULE=C:\path\to\icd64.dll   :: 强制指定 OpenCL 模块（排障）
```
内核嵌入步骤由构建自动完成（`src/gpu/slime_kernel.cl` → `generated/slime_kernel_embed.h`，
改动 .cl 后重新构建即生效）。嵌入采用**逐字节八进制转义**（生成头文件为纯 ASCII，与源文件编码
/控制台代码页无关），脚本内带「转义自回读校验」，失败即中止构建；
`build\slime_kernel_tests.exe` 的 [D] 段会在运行期把嵌入串与磁盘 `.cl` 逐字节比对
（长度 6058 / 校验和 997496561 / 两个 `__kernel` 签名），可第一时间发现嵌入管线问题。

> **头文件依赖（v3.3.1 修复）**：本地化（中文）cl.exe 的 `/showIncludes` 前缀为 GBK 字节，
> 与 Ninja 的 `msvc_deps_prefix` 不匹配时会**静默丢失全部头文件依赖**（`ninja -t deps` 显示
> `#deps 0`，改头文件不重编译）。本项目已在 CMake 中把全部项目头文件（含生成的
> `slime_kernel_embed.h`）声明为各目标源文件的 `OBJECT_DEPENDS`，任何语言环境都正确；
> 若你自行新增头文件目录，请同步更新 `CMakeLists.txt` 末尾的 `SLIME_HEADERS` glob。
> 交付前的正式验证请始终使用 `scripts\verify_release.ps1`（含 `--clean-first` 全量重建）。

### 3.2 无界面 GUI 自检（v3.1.0；v3.3.0 增加算法模式组 [11] 与布局校验）
```bat
set SLIME_SELFTEST=1
build\SlimeFinder.exe            :: 或 dist\SlimeFinder\SlimeFinder.exe
echo %ERRORLEVEL%   :: 0 = 通过（预设/边界截断/复制命令格式/结果卡片/性能日志/配置/
                    ::           GPU 开关与检测/真实搜索冒烟/算法模式联动与 config 往返/
                    ::           布局校验：文字不截断 + 设置区滚动条）
```
> 界面布局自查（v3.3.0）：`set SLIME_SHOT=%TEMP%\ui` 会在自检过程中把窗口渲染为
> `%TEMP%\ui_mode-exact.png`、`ui_mode-layered.png`、`ui_mode-sample.png`、
> `ui_mode-full-window.png` 等图片，便于人工核对文字是否被遮挡（配合
> `QT_QPA_PLATFORM=offscreen` 可无桌面运行）。
> Windows 下 `SlimeFinder.exe` 为 GUI 子系统程序：从 PowerShell 直接调用不会等待也不会
> 回传退出码。请用 `Start-Process -Wait -PassThru` 或 `cmd /c` 包装，例如：
> ```powershell
> $p = Start-Process -FilePath .\build\SlimeFinder.exe -Wait -PassThru `
>      -RedirectStandardOutput out.log -RedirectStandardError err.log
> $p.ExitCode
> ```

### 3.3 与参考实现交叉验证（需 Python 3；本机用 `py -3`）
```bat
py -3 tests\verify_against_reference.py ^
       "..\模板：原始文件\SlimeChecker.exe" ^
       "build\slime_tests.exe"
```
111 项对照：Java/基岩扫描坐标集合、严格/密度窗口集合与密度值、
以及按 Python GUI 逻辑重建的“最近 N 个”排序列表（含智能范围+提前停止）。

### 3.4 性能基准
```bat
build\slime_tests.exe --bench                    :: 当前版本
bench_v321\slime_tests_v321.exe --bench          :: v3.2.1 归档二进制（同机 A/B 对照）
```

### 3.5 一键发布验证（v3.3.1 新增；v3.3.2+ 沿用）
```powershell
powershell -ExecutionPolicy Bypass -File scripts\verify_release.ps1 `
           -Zip dist\SlimeFinder-3.10.12.9-win64.zip
```
依次执行并汇总：clean 全量重建（校验退出码与 error 行数）→ 单元测试 → 111 参考对照 →
OpenCL 探测 → SIMD 内核自检（含嵌入内核逐字节校验）→ GUI 自检
（config `gpu.enabled` = false/true 各一轮）→ 性能基准（含 v3.2.1 基线）→ dist 自检 →
哈希一致性（build/dist/解压包）。
所有日志写入 `%TEMP%\slime_verify_<时间戳>\`；脚本退出码 0 表示全部通过。
可用 `-SkipClean`（跳过 clean，仅增量）与 `-SkipBench`（跳过基准）加速本地迭代。

> 「哈希一致性」检查要求 `dist\SlimeFinder\` 中的二进制来自**同一次构建**。
> 自 v3.3.1 起构建加入 `/Brepro`（确定性时间戳/链接哈希），同一份源码的 clean 重建
> 产出**逐字节一致**的二进制，因此「先 clean 重建 → 打包 → 再验证」也能通过哈希检查
> （实测：两次 clean 重建 + 重新链接后 `SlimeFinder.exe` SHA256 完全相同）。

### 3.6 一键打包（v3.3.1 新增；`-Version 3.3.10.12.9` 可指定版本号）
```powershell
powershell -ExecutionPolicy Bypass -File scripts\package_release.ps1
```
依次执行：复制 `build\` 产物（`SlimeFinder.exe`、`slime_tests.exe`、`slime_ocl_probe.exe`、
`slime_kernel_tests.exe`）→ 刷新 `dist\SlimeFinder\docs\` 与 `README.md` → 清理运行期残留
（`logs/`、`crashes/`、`*.log`、缓存文件）→ 压缩为 `dist\SlimeFinder-3.10.12.9-win64.zip` →
校验「build == dist == 解压包」SHA256、exe 内版本串、包内 GUI 自检 + 单元组 15 + 内核自检 →
写入 `dist\SHA256SUMS-3.3.10.12.9.txt`。脚本退出码 0 表示打包与包内校验全部通过；
`-SkipSelfTest` 可跳过包内自检（仅快速重打包时使用）。

### 3.7 污染审计与证据导出（v3.3.5 新增）

历史上出过两次「单字符替换」事故（`MainWindow.cpp` 全文 `c`→`o`、`install.nsi` 全文 `!`→`d`，
根因是 PowerShell 辅助脚本拿到被展平的嵌套数组后对整篇做 `String.Replace`）。被一致改名的**局部**
标识符不会引起编译错误、单元测试也照样通过，因此需要词法审计兜底：

```powershell
# 7 类检查：已知坏片段 / 词典判定 / 跨文件还原验证 / 稀有二元组特征 /
#           关键字符串常量 / 产物一致性 / 文件卫生（NUL、非法 UTF-8、U+FFFD 乱码）
py -3 scripts\audit_corruption.py        # 退出码 0 = 无发现；1 = 有发现（可作发布门禁）
py -3 scripts\audit_corruption.py -v     # 附加词表来源统计
```

审计报告见 `docs/corruption_audit_report.md`；当前版本的审计输出为 `docs/corruption_audit_v335.txt`。
工具本身带灵敏度自测方法（临时注入含 4 类损坏的样本文件，应报出 bad-pattern/dictionary/cross-file/
digraph/short-name 命中，删除样本后恢复 CLEAN）。

证据导出（把 `%TEMP%` 里的 UTF-16/混合编码日志按 BOM 解码并统一写成 UTF-8，同时统计 `U+FFFD`/NUL）：

```powershell
powershell -ExecutionPolicy Bypass -File scripts\export_evidence.ps1 `
           -LogDir "$env:TEMP\slime_verify_<时间戳>" -Tag v335
```

> v3.3.5 起诊断输出**全链路 UTF-8**：C++ 侧 `toUtf8()`/`u8string()`/`WideCharToMultiByte(CP_UTF8)`，
> Python 侧 `sys.stdout.reconfigure(encoding="utf-8")`，采集侧 `verify_release.ps1` 固定
> `[Console]::OutputEncoding`/`$OutputEncoding` 与 `PYTHONUTF8=1`。禁止按**字节**截断 UTF-8 文本。

**标准发布流程**：`audit_corruption.py`（污染门禁，必须退出码 0）→ `verify_release.ps1`
（clean 重建 + 全量验证）→ `package_release.ps1`（打包）→ 再跑一次
`verify_release.ps1 -SkipClean -SkipBench -Zip dist\SlimeFinder-3.10.12.9-win64.zip`
确认 dist/zip 哈希与包内自检 → `export_evidence.ps1` 导出证据（必须 0 缺陷）。

## 4. 部署（生成可分发目录）

```bat
mkdir dist\SlimeFinder
copy build\SlimeFinder.exe dist\SlimeFinder\
C:\Qt\6.11.2\msvc2022_64\bin\windeployqt.exe --release ^
      --no-translations --no-system-d3d-compiler --no-opengl-sw ^
      dist\SlimeFinder\SlimeFinder.exe
```
- 运行库（VC++ 运行时/OpenMP，随 VC 重新分发包安装则可不带）：
  从 `C:\...\VC\Redist\MSVC\<版本>\x64\` 复制 `vcruntime140.dll`、
  `vcruntime140_1.dll`、`msvcp140*.dll` 与
  `Microsoft.VC145.OpenMP\vcomp140.dll` 到同一目录；
- 可分发目录另需附上 `slime_tests.exe`、`slime_ocl_probe.exe`、`slime_kernel_tests.exe` 与 `docs/`；
  实际打包直接用 `scripts\package_release.ps1`（见 §3.6，自动完成复制/清理/压缩/校验/写哈希）；
- 交付目录结构即 `dist/SlimeFinder/`；压缩包 `dist/SlimeFinder-3.10.12.9-win64.zip`。
  （`platforms/` 目录另含 `qoffscreen.dll`/`qminimal.dll`，便于无桌面环境自检。）
- **发布前必须校验**：直接运行上面的 `scripts\verify_release.ps1`（等价于下列手工步骤）：
  ```powershell
  cmake --build build --clean-first          # 干净全量重建（检查退出码）
  .\build\slime_tests.exe --unit             # 全部单元组通过
  py -3 tests\verify_against_reference.py    # 111/111
  $p = Start-Process .\build\SlimeFinder.exe -Wait -PassThru -RedirectStandardOutput o.log `
       -RedirectStandardError e.log   # 需先 set SLIME_SELFTEST=1；期望 ExitCode 0
  Get-FileHash build\SlimeFinder.exe, dist\SlimeFinder\SlimeFinder.exe -Algorithm SHA256  # 必须一致
  ```

## 5. 打包为单个自包含 exe（可选，需静态 Qt）

```bat
:: 1) 自行编译静态 Qt（约需 1~2 小时，12 核）
cd qt-src
configure -prefix C:/QtStatic/6.11.2/msvc2022_64 -release -static -static-runtime ^
          -opensource -confirm-license -nomake examples -nomake tests -submodules qtbase
cmake --build . --config Release -j
cmake --install .

:: 2) 用静态 Qt 重编译本工程（无需改任何代码）
cmake -S . -B build-static -DCMAKE_PREFIX_PATH=C:/QtStatic/6.11.2/msvc2022_64 -DCMAKE_BUILD_TYPE=Release
cmake --build build-static
:: 产出 build-static/SlimeFinder.exe 即为单文件程序
```

## 6. 安装包（可选）
安装 NSIS 3.x 后：
```bat
makensis scripts\install.nsi
```
生成 `dist\SlimeFinder-Setup-3.3.5.exe`（安装路径选择/桌面快捷方式/开始菜单/.slimecache 关联）。

