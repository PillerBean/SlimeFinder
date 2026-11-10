# 多语言与 UI 文案隔离（v3.11.0）

## 结构

```text
assets/i18n/
├─ zh-CN.json           # key → 简体中文
├─ en-US.json           # key → English
└─ legacy-zh-en.json    # 旧 UiStrings 翻译表（运行时兼容用）
```

程序目录下的 `assets/i18n` 是**外部文件**，安装包会一起发布；不需要重新编译即可替换文案或增加语言。

## 代码约定

- 所有用户可见文案通过：
  ```cpp
  slime::ui::t("ui.<key>");
  ```
- 代码里不再直接写中文 UI 文案；
- 新增文案必须同时加入 `zh-CN.json` 和 `en-US.json`；
- 运行 `py -3 scripts/check_ui_text.py` 检查 key 完整性（发布前必跑）。

## 增加新语言

1. 复制 `en-US.json` 为 `assets/i18n/<lang>.json`；
2. 翻译所有 value（不要改 key）；
3. 在设置页语言选项中接入该语言（当前版本仍为 zh / en 两个入口）；
4. 重启程序生效。

## 已知边界

- 诊断日志、GUI 自检输出、更新说明正文不属于 UI 文案，仍保留中文；
- 英文翻译优先复用旧 `legacy-zh-en.json`，未覆盖的新文案会回退中文；后续可继续补齐。
