# 快速开始

## 1. 下载

- 稳定版（Latest）：<https://github.com/PillerBean/SlimeFinder/releases/latest>
- 测试版：Releases 页面里带 `Pre-release` 标记的版本（当前测试版 v3.10.12.11：
  <https://github.com/PillerBean/SlimeFinder/releases/tag/v3.10.12.11>）
- 安装包：在对应 Release 的 **Assets** 里下载 `SlimeFinder-<版本>-Setup.exe`
  （默认安装到 Program Files，需要管理员权限；自动更新只支持安装版）
- 校验：同页下载 `SHA256SUMS-<版本>.txt`，按需核对安装包
- 便携 zip 已停发；旧版本 zip 仍可从历史 Release 的 Assets 获取

系统要求：Windows 10/11 x64。GPU 加速为可选项（需要可用的 OpenCL 驱动）。

## 2. 首次搜索

1. 从开始菜单/桌面运行 `SlimeFinder.exe`；
2. 选择 **Java 版 / 基岩版**，填入种子；
3. 选择搜索范围（可用「附近/局部/全局」快捷按钮，或直接填半径）；
4. 选择模式：
   - **密度模式**：找史莱姆密度高的区域（可设阈值或「最优 N 个」）；
   - **图案模式**：按自己画的图案匹配（2×2、边框、单格等）；
   - **环形模式**：规划 AFK 环形覆盖点；
5. 设置目标数量与查询起点，点「开始搜索」。

## 3. 结果怎么用

- 结果列表按距离/密度排序；
- 右键或按钮可**复制为 Minecraft 命令**（传送/标记），也可导出文本；
- 结果坐标是区块坐标，乘以 16 得到方块坐标。

## 4. GPU 加速（可选）

打开「设置 → 性能」勾选「启用 GPU 加速」；现代独立显卡通常明显快于 CPU，
若驱动异常或显卡很弱可以关闭，搜索会自动回退 CPU。详见 [GPU 与性能](GPU-Performance)。

## 5. 搜索太慢？

- 范围越大越慢：整图搜索会先给出耗时估算并要求确认；
- 开启「提前停止」（找够 N 个即停）通常最快；
- 命中很少时，也可试试「最优 N 个（忽略阈值）」；
- 大范围重复搜索建议启用缓存。

遇到问题见 [常见问题](FAQ) 或提交 <https://github.com/PillerBean/SlimeFinder/issues>。
