cmake_minimum_required(VERSION 3.16)

project(SlimeFinder
    VERSION 3.13.0
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

find_package(Qt6 6.4 REQUIRED COMPONENTS Widgets Network)
find_package(OpenMP REQUIRED)

# ---------------------------------------------------------------- 核心引擎（Qt 无关）

# v3.12.0：目标定义在 cmake/Targets.cmake
include(cmake/Targets.cmake)
