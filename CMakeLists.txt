cmake_minimum_required(VERSION 3.16)

project(SlimeFinder
    VERSION 3.10.12.8
    DESCRIPTION "史莱姆区块查找器 - Slime Chunk Finder (C++/Qt)"
    LANGUAGES CXX C)

# ---------------------------------------------------------------- 全局配置
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
# v3.3.9（任务书 5.1）：图标 PNG 走 Qt 资源系统（assets/appicon.qrc）
set(CMAKE_AUTORCC ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

find_package(Qt6 6.4 REQUIRED COMPONENTS Widgets)
find_package(OpenMP REQUIRED)

# ---------------------------------------------------------------- 核心引擎（Qt 无关）

# ---------------------------------------------------------------- cubiomes（MIT）：群系生成
# 来源：https://github.com/Cubitect/cubiomes（MIT License，见 third_party/cubiomes/LICENSE）
# 用途：v3.5.2 地图彩色预览（Java 1.0~1.21 群系）与后续「群系权重」数据基础。
# 注：基岩版群系暂不支持（cubiomes 不提供），地图只画史莱姆区块。
add_library(cubiomes STATIC
    third_party/cubiomes/biomes.c
    third_party/cubiomes/biomenoise.c
    third_party/cubiomes/generator.c
    third_party/cubiomes/layers.c
    third_party/cubiomes/noise.c
    third_party/cubiomes/util.c)
target_include_directories(cubiomes SYSTEM PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/third_party/cubiomes)
target_compile_definitions(cubiomes PRIVATE _CRT_SECURE_NO_WARNINGS)
if(MSVC)
    target_compile_options(cubiomes PRIVATE /W0)
else()
    target_compile_options(cubiomes PRIVATE -w)
endif()

add_library(slime_core STATIC
    src/core/RandomGenerators.cpp
    src/core/SlimeEngine.cpp
    src/core/FastMatch.cpp
    src/core/FastMt.cpp
    src/core/FastJava.cpp   # v3.6.16：Java 版判定 AVX2（16 lane）
    src/core/CacheManager.cpp
    src/core/SearchTask.cpp
    src/core/AfkPlanner.cpp   # v3.5.2（任务 1）：环形覆盖挂机点规划（1/16 区块精度）
    src/core/BiomeMap.cpp     # v3.5.2（任务 2/3）：cubiomes 群系地图（彩色预览 / 权重数据底座）
    src/core/SpawnWeights.cpp # v3.5.2（任务 3）：群系 → 史莱姆刷怪效率权重（Wiki 数据）
    src/core/PerfLog.cpp        # E3.3.7.2（任务书步骤 2 / P1-D）：引擎侧性能事件
    src/utils/IncidentLog.cpp   # E3.3.7.2：移入核心库 —— 引擎需要写关键信息日志（Qt 无关）
)
target_include_directories(slime_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(slime_core PUBLIC OpenMP::OpenMP_CXX cubiomes)
target_compile_definitions(slime_core PUBLIC _CRT_SECURE_NO_WARNINGS)
if(WIN32)
    # IncidentLog / PerfLog 用 GetProcessMemoryInfo 统计工作集与内存峰值
    target_link_libraries(slime_core PUBLIC psapi)
endif()

# E3.3.0：AVX2 内核单独编译单元（其余文件保持 SSE2 兼容；运行时由 cpuHasAvx2 把关）
if(MSVC)
    set_source_files_properties(src/core/FastMatch.cpp PROPERTIES COMPILE_OPTIONS "/arch:AVX2")
    set_source_files_properties(src/core/FastMt.cpp PROPERTIES COMPILE_OPTIONS "/arch:AVX2")
    set_source_files_properties(src/core/FastJava.cpp PROPERTIES COMPILE_OPTIONS "/arch:AVX2")
else()
    set_source_files_properties(src/core/FastMatch.cpp PROPERTIES COMPILE_OPTIONS "-mavx2")
    set_source_files_properties(src/core/FastMt.cpp PROPERTIES COMPILE_OPTIONS "-mavx2")
    set_source_files_properties(src/core/FastJava.cpp PROPERTIES COMPILE_OPTIONS "-mavx2")
endif()

# ---------------------------------------------------------------- GPU 加速模块（E3.2.0）
# slime_kernel.cl 构建期嵌入为 C++ 原始字符串（单一事实来源）
set(SLIME_GPU_GEN ${CMAKE_CURRENT_BINARY_DIR}/generated)
add_custom_command(
    OUTPUT ${SLIME_GPU_GEN}/slime_kernel_embed.h
    COMMAND ${CMAKE_COMMAND} -E make_directory ${SLIME_GPU_GEN}
    COMMAND ${CMAKE_COMMAND} -DKERNEL_IN=${CMAKE_CURRENT_SOURCE_DIR}/src/gpu/slime_kernel.cl
            -DKERNEL_OUT=${SLIME_GPU_GEN}/slime_kernel_embed.h
            -P ${CMAKE_CURRENT_SOURCE_DIR}/scripts/embed_kernel.cmake
    DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/src/gpu/slime_kernel.cl
        ${CMAKE_CURRENT_SOURCE_DIR}/scripts/embed_kernel.cmake
    COMMENT "嵌入 slime_kernel.cl -> slime_kernel_embed.h"
    VERBATIM
)
add_custom_target(slime_kernel_embed DEPENDS ${SLIME_GPU_GEN}/slime_kernel_embed.h)

# ---------------------------------------------------------------- 头文件依赖跟踪（E3.3.1 重要）
# 背景：Ninja 通过 msvc_deps_prefix 匹配 cl.exe /showIncludes 的输出前缀，二者必须逐字节一致。
# 中文（本地化）MSVC 输出的是 GBK 字节，而写进 rules.ninja 的必须是可读文本，两者不匹配时
# Ninja 会**静默丢弃全部头文件依赖**（实测 ninja -t deps 显示 #deps 0）——改头文件后不重编译，
# 曾导致「旧 OpenCL 内核被静默使用」「虚表不匹配崩溃」等极难定位的问题。
# VSLANG=1033 不能切换 /showIncludes 的语言（本机实测无效），故这里显式声明对象依赖头文件：
# 本项目头文件不足 20 个、整包重编译仅数十秒，用「头文件变更即重编译」换取构建正确性。
file(GLOB_RECURSE SLIME_HEADERS CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.h
)
list(APPEND SLIME_HEADERS ${SLIME_GPU_GEN}/slime_kernel_embed.h)

add_library(slime_gpu STATIC
    src/gpu/ClApi.cpp
    src/gpu/GpuSession.cpp
    src/gpu/GpuSessionDescribe.cpp
    src/gpu/GpuScanAdapter.cpp
)
add_dependencies(slime_gpu slime_kernel_embed)
target_include_directories(slime_gpu PRIVATE
    ${SLIME_GPU_GEN}
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/OpenCL-Headers
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
if(WIN32)
    target_link_libraries(slime_gpu PRIVATE version) # GetFileVersionInfo*
endif()

# ---------------------------------------------------------------- GUI 主程序
add_executable(SlimeFinder WIN32
    src/main.cpp
    assets/appicon.qrc    # v3.3.9（任务书 5.1）：程序图标（窗口/任务栏/托盘）
    src/gui/AfkDialog.cpp   # v3.5.2（任务 1）：挂机点（环形覆盖）对话框
    src/gui/MapPreviewWidget.cpp # v3.5.2（任务 2）：地图预览控件（缩放/平移/图层）
    src/gui/MapDialog.cpp       # v3.5.2（任务 2）：地图预览对话框
    src/gui/MainWindow.cpp
    src/gui/PatternEditor.cpp
    src/gui/PatternEditorPanel.cpp
    src/gui/UpdateHistory.cpp # v3.3.9（任务书 四）：完整更新历史文本（1.0.0 → 3.3.9）
    src/gui/HelpDialog.cpp
    src/gui/SettingsDialog.cpp # E3.3.5（优化 5）：程序设置（五标签页）
    src/gui/FeedbackPanel.cpp   # E3.3.4（任务书 2.2）：反馈面板（E3.3.6 改版：时间顺序 + 过滤成功）
    src/gui/LogPanel.cpp        # E3.3.4（任务书 2.3）：日志面板（默认关闭 + 两级选项）
    src/gui/CommandTerminal.cpp # E3.3.4（任务书 2.4）：命令终端（命令可插队）
    src/gui/ProgressStreamPanel.cpp # E3.3.6（任务书 调整 3）：进度分类（累积式纯文字流水）
    src/utils/Logger.cpp
    src/utils/LogBus.cpp        # E3.3.4（任务书 2.3）：日志总线（按日期分文件 + 两级模式）
    src/utils/CrashDump.cpp
    # E3.3.7.2：IncidentLog.cpp 已移入 slime_core（引擎侧性能事件也要写关键信息日志）
    src/utils/ThemeManager.cpp  # E3.3.6（任务书 4.7）：夜间模式三档（QSS + 注册表检测）
    src/utils/SoundPlayer.cpp   # E3.3.6（任务书 4.2）：搜索完成提示音（winmm，带音量）
    src/app.rc            # E3.3.3（任务书 2.1）：VERSIONINFO（属性→详细信息→文件版本）
                          # v3.3.9（任务书 5.1）：IDI_APPICON 程序图标（assets/appicon.ico）
)
# v3.3.9（任务书 5.1）：让 rc.exe 能在 assets/ 下找到 appicon.ico（rc 的相对路径基准不可依赖）
if(WIN32)
    set_source_files_properties(src/app.rc PROPERTIES COMPILE_FLAGS
        "/I\"${CMAKE_CURRENT_SOURCE_DIR}/assets\"")
endif()
target_include_directories(SlimeFinder PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(SlimeFinder PRIVATE
    slime_core
    slime_gpu
    Qt6::Widgets
    OpenMP::OpenMP_CXX
)
if(WIN32)
    target_link_libraries(SlimeFinder PRIVATE dbghelp)
    # E3.3.6（任务书 4.2）：提示音用 waveOut（winmm）；WIN32_LEAN_AND_MEAN 会隐藏 mmsystem.h，
    # 因此 SoundPlayer.cpp 显式包含它，链接库也必须显式声明。
    target_link_libraries(SlimeFinder PRIVATE winmm)
endif()

# ---------------------------------------------------------------- 测试/命令行工具（纯引擎）
add_executable(slime_tests
    tests/test_slime.cpp
    src/utils/Logger.cpp
)
target_include_directories(slime_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(slime_tests PRIVATE slime_core slime_gpu)
if(WIN32)
    target_link_libraries(slime_tests PRIVATE dbghelp)
endif()
# E3.3.1：为崩溃定位保留链接映射文件（仅构建目录产物，不影响交付体积）
if(MSVC)
    target_link_options(slime_tests PRIVATE /MAP)
endif()

# ---------------------------------------------------------------- OpenCL 探测工具（E3.2.0 阶段一）
add_executable(slime_ocl_probe
    tools/ocl_probe.cpp
)
target_include_directories(slime_ocl_probe PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(slime_ocl_probe PRIVATE slime_core slime_gpu)
if(WIN32)
    target_link_libraries(slime_ocl_probe PRIVATE dbghelp)
endif()

# ---------------------------------------------------------------- E3.3.1：SIMD 内核独立自检（不依赖引擎/OpenCL）
# 覆盖：基岩种子查表、跨步采样扫描、密度 g×g 窗口计数（均与独立教科书参考逐位比对）
add_executable(slime_kernel_tests
    tests/kernel_selftest.cpp
)
target_include_directories(slime_kernel_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${SLIME_GPU_GEN} # 嵌入内核头（自检 D 段：长度/校验和/逐字节比对）
)
# 自检需要知道 .cl 的磁盘路径，用于运行期逐字节比对（UTF-8 路径由宽字符打开）
target_compile_definitions(slime_kernel_tests PRIVATE
    SLIME_KERNEL_CL_PATH="${CMAKE_CURRENT_SOURCE_DIR}/src/gpu/slime_kernel.cl"
)
target_include_directories(slime_kernel_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
# 自检本体不启用 /arch:AVX2（验证头文件可在无 AVX2 的编译单元中安全包含）；
# 内核实现随 slime_core 链接（FastMt/FastMatch 在 CMake 中单独加 /arch:AVX2）。
target_link_libraries(slime_kernel_tests PRIVATE slime_core)

# ---------------------------------------------------------------- E3.3.1：GPU 密度匹配内核（暂停开发）
# 按 E3.3.1 交付决定：GPU 密度匹配内核（含其独立自检 tests/gpu_density_selftest.cpp）
# 停止开发，全部代码保留在 src/gpu/experimental/（不加入构建），作为 E3.4.0 起点。
# 恢复步骤见 src/gpu/experimental/README.md；引擎侧接口（GpuScan::matchDensity）与
# 单元测试 [17]/[18] 已就绪，E3.4.0 只需实现该接口即可生效。

# ---------------------------------------------------------------- 编译选项
if(MSVC)
    foreach(tgt SlimeFinder slime_core slime_tests slime_gpu slime_ocl_probe slime_kernel_tests)
        target_compile_options(${tgt} PRIVATE /utf-8 /EHsc /permissive-)
        # /Brepro: deterministic timestamps in the PE header (and hashed link timestamp),
        # so rebuilding the same sources yields byte-identical binaries - this makes the
        # "build Es dist Es zip SHA256 equal" release check meaningful.
        target_compile_options(${tgt} PRIVATE /Brepro)
        target_link_options(${tgt} PRIVATE /Brepro)
        if(NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
            # Release/RelWithDebInfo 下开启优化 + OpenMP + 链接时代码生成(LTCG, E3.1.0)
            target_compile_options(${tgt} PRIVATE /O2 /openmp /GL)
            target_link_options(${tgt} PRIVATE /LTCG)
        endif()
    endforeach()
else()
    foreach(tgt SlimeFinder slime_core slime_tests slime_gpu slime_ocl_probe slime_kernel_tests)
        target_compile_options(${tgt} PRIVATE -O3 -fopenmp)
    endforeach()
endif()

# 关闭第三方头文件噪音告警级别
if(MSVC)
    target_compile_options(SlimeFinder PRIVATE /W3)
    target_compile_options(slime_core PRIVATE /W3)
    target_compile_options(slime_tests PRIVATE /W3)
    target_compile_options(slime_gpu PRIVATE /W3)
    target_compile_options(slime_ocl_probe PRIVATE /W3)
    target_compile_options(slime_kernel_tests PRIVATE /W3)
endif()

# ---------------------------------------------------------------- 头文件依赖绑定（配合上文说明）
# 所有目标的源文件显式依赖全部项目头文件（含构建期生成的 slime_kernel_embed.h）：
# 任何头文件变更都会重编译受影响的对象，不依赖 /showIncludes（本地化前缀不可靠）。
if(MSVC)
    foreach(_tgt SlimeFinder slime_core slime_tests slime_gpu slime_ocl_probe slime_kernel_tests)
        get_target_property(_srcs ${_tgt} SOURCES)
        if(_srcs)
            set_source_files_properties(${_srcs} PROPERTIES OBJECT_DEPENDS "${SLIME_HEADERS}")
        endif()
    endforeach()
endif()

