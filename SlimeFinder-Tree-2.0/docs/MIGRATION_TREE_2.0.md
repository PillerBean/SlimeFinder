# SlimeFinder-Tree-2.0 迁移状态

## 规则

- 新树是唯一开发树；旧树冻结，仅用于对照/回滚/历史归档。
- 新树必须自包含：不引用旧树源码、third_party、assets 或 tests fixture。
- 新树独立构建目录：`build/`；独立打包目录：`dist/`。
- 旧树不作修改。

## 已迁移

- `src` 分层：app/core/gpu/gui/i18n/utils；
- GUI 子目录：mainwindow/dialogs/panels/widgets/services；
- tests：unit/integration/reference；
- tools：gpu/probes；
- packaging/windows：NSIS；
- assets、third_party、telemetry、活跃 docs；
- 新树自包含参考程序 `tests/reference/SlimeChecker.exe`；
- windeployqt 打包部署。

## 已完成

- scripts 分组：build / qa / release / maintenance；
- CMake 目标拆分（cmake/Targets.cmake）与 CMakePresets；
- app.rc 移入 src/platform/windows；
- MainWindow.cpp 从 13,975 行降到 1,270 行；
- GUI 自检、构建界面、截图、设置/环形/地图/交互/搜索/结果/服务分别拆入 inc 文件；
- windeployqt 自包含打包；
- tests/reference 自包含；
- GitHub Actions 结构/语言检查；
- 新树 unit / reference / GUI / Setup 验证通过。

## 后续可选

- 将 inc 文件进一步转为真正的组件类；
- 完成用户验收后再发布 v3.12.2。
