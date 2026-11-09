# SlimeFinder 史莱姆区块查找器

Minecraft Java / 基岩版史莱姆区块查找工具，原生 **C++20 + Qt 6**，支持 GPU 加速与超大范围搜索。
判定结果与已验证的参考实现逐位一致。

![SlimeFinder 界面](docs/images/screenshot.png)

## 下载

- **稳定版（Latest）**：<https://github.com/PillerBean/SlimeFinder/releases/latest>
- **测试版 / 历史版本**：<https://github.com/PillerBean/SlimeFinder/releases>（带 `Pre-release` 标记的是测试版）
- 平台：Windows 10/11 x64；便携包解压即用，安装包可选
- 当前版本：**v3.10.12.9**

## 功能

- **Java / 基岩版**史莱姆区块判定，与参考实现 117/117 逐条一致
- **密度模式**：阈值匹配、最优 N 个、提前停止、智能范围、分层/采样
- **图案模式**：自定义图案（2x2、边框、单格等）与旋转匹配
- **环形模式**：AFK 环形覆盖规划
- **GPU 加速**（OpenCL）：自动探测、失败自动回退 CPU；AMD 可选图案 GPU 流式
- **大范围搜索**：Java 整图 / 基岩世界边界，分块有界内存
- 历史缓存、命令、日志、深色模式、内置教程

## 快速开始

1. 从 Releases 下载 `SlimeFinder-*-win64.zip` 并解压；
2. 运行 `SlimeFinder.exe`；
3. 选择 Java/基岩版、搜索范围、密度或图案模式，点「开始搜索」；
4. GPU 加速是可选项（设置 → 性能），显卡较新时建议开启。

## 从源码构建

```bat
:: 需要 VS2022、CMake 3.20+、Qt 6.11 msvc2022_64
cmd /c scripts\build_repo.cmd
build\slime_tests.exe --unit
```

详细步骤（打包、安装包、自检）见 [docs/build_instructions.md](docs/build_instructions.md)。

## 版本与发布规则

- 版本号统一四段，例如 `3.10.12.9`；
- 正式发布：GitHub Release 不勾 `Pre-release`，成为 **Latest**（自动更新默认读取它）；
- 测试发布：发布时勾选 `Pre-release`，不会成为 Latest；
- Latest 由发布者手动控制；完整发版流程见 [docs/GITHUB_PUBLISH_GUIDE.md](docs/GITHUB_PUBLISH_GUIDE.md)。

## 许可与来源

本项目**源码公开、非商业使用**：

- 许可证：[PolyForm Noncommercial 1.0.0](LICENSE)（含 Required Notice 署名要求）
- 任何复制、再分发或修改版必须保留版权声明与 Required Notice，并注明来源：
  **SlimeFinder — <https://github.com/PillerBean/SlimeFinder>**
- 商业使用（含付费产品或服务、商业运营）需作者另行书面授权
- 第三方组件：cubiomes（MIT）、OpenCL-Headers（Apache-2.0）、Qt 6（LGPLv3/GPLv3/商业许可，
  以发行包内 Qt 组件为准）；详见 [NOTICE](NOTICE)

## 文档

- [用户手册](docs/user_manual.md)
- [构建与打包](docs/build_instructions.md)
- [更新历史](docs/update_history.md)
- [GitHub 发布指南](docs/GITHUB_PUBLISH_GUIDE.md)
- [社区性能对照](docs/community_benchmark_reference.md)
