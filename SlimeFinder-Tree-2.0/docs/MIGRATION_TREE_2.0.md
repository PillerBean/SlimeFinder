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

## 待完成

- scripts 进一步分组（build/qa/release/maintenance）；
- CMake 目标拆分与 presets；
- MainWindow 进一步拆成独立 TU/组件；
- CI 工作流；
- 完整结构与包内容清单校验。
