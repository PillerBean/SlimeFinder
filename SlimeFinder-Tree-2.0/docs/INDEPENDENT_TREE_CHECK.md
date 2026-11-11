# SlimeFinder-Tree-2.0 独立性验证

日期：2026-11-11

## 目的

验证 `SlimeFinder-Tree-2.0` 在**脱离旧树目录**后，能否独立完成：

- CMake 配置
- 编译
- 单元测试
- 参考对拍
- GUI 自检
- 打包 Setup

## 方法

只复制新树源码（排除顶层 `build/`、`dist/`、`.git/`），复制到：

```text
C:\Temp\SlimeFinder-Tree-2.0-independent
```

该目录不在旧树 `SlimeFinder/` 下，也不引用旧树文件。

然后在新目录中执行：

```text
scripts\build\configure.cmd
scripts\build\build.cmd
build\slime_tests.exe --unit
py -3 tests\reference\verify_against_reference.py
scripts\qa\gui_selftest.ps1 -GpuOff
scripts\release\build_setup_release.ps1 -Version 3.12.2 -SkipSelfTest
```

## 结果

| 项目 | 结果 |
|---|---|
| CMake 配置 | 通过 |
| 编译 | 通过 |
| 单元测试 | 通过 |
| 参考对拍 | **117/117 通过** |
| 结构检查 | 通过 |
| GUI 自检 | **0 FAIL** |
| Setup 构建 | **PACKAGE OK** |

独立目录中生成的 Setup：

```text
SlimeFinder-3.12.2-Setup.exe
17,651,994 B
SHA256 4273A46F7A1756840419939ACDC49545B62098688CAA22666CEBD0BFC842D172
```

## 结论

新树在源码、资源、参考程序、测试和打包脚本层面**不依赖旧树目录**。

仍然依赖的外部环境（不是旧树）：

- Windows 10/11
- Visual Studio 2022 / MSVC
- Qt 6.11.2
- Ninja
- OpenMP
- Python 3（测试和部分工具）
- NSIS（Setup）

旧树目前只作为历史归档，保留历史文档、证据和旧版本源码；新树复制到任意独立目录后仍可构建和测试。
