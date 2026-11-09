#!/usr/bin/env python3
"""从 src/gui/UpdateHistory.cpp 生成 docs/update_history.md。

用法: py -3 scripts/export_update_history.py
生成结果与界面数据同源；修改历史时只改 UpdateHistory.cpp，然后重新运行本脚本。
"""
import io
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "src", "gui", "UpdateHistory.cpp")
DST = os.path.join(ROOT, "docs", "update_history.md")
TAG_ORDER = ["Added", "Fixed", "Improved", "Adjusted", "Removed"]
TAG_ZH = {"Added": "新增", "Fixed": "修复", "Improved": "优化",
          "Adjusted": "调整", "Removed": "移除"}


def unescape(s: str) -> str:
    return s.replace("\\\\", "\\").replace('\\"', '"')


def main() -> int:
    raw = io.open(SRC, encoding="utf-8", newline="").read().replace("\r\n", "\n")
    start = raw.find("const RawEntry kRawEntries[] = {")
    end = raw.find("\n};", start)
    if start < 0 or end < 0:
        print("kRawEntries not found", file=sys.stderr)
        return 1
    body = raw[start:end]

    entry_re = re.compile(
        r'\{ "([^"]+)", "([^"]+)", (true|false), (\d+), \{(.*?)\n    \} \},',
        re.S)
    item_re = re.compile(r'\{ HistoryTag::(\w+), "((?:[^"\\]|\\.)*)" \}')

    entries = []
    for m in entry_re.finditer(body):
        version, date, official, _count, items_src = m.groups()
        items = []
        for im in item_re.finditer(items_src):
            tag, text = im.groups()
            if tag not in TAG_ZH:
                print("unknown tag", tag, file=sys.stderr)
                return 1
            items.append((tag, unescape(text)))
        entries.append((version, date, official == "true", items))

    if not entries:
        print("no entries parsed", file=sys.stderr)
        return 1

    lines = []
    lines.append("# SlimeFinder 更新历史")
    lines.append("")
    lines.append("> 本文件由 `scripts/export_update_history.py` 从 "
                 "`src/gui/UpdateHistory.cpp` 自动生成，请勿手改。")
    lines.append("> 共 %d 个版本，版本范围：%s → %s；★ 表示正式版。"
                 % (len(entries), entries[-1][0], entries[0][0]))
    lines.append("")
    for version, date, official, items in entries:
        lines.append("## V%s · %s%s" % (version, date, " ★正式版" if official else ""))
        by_tag = {t: [] for t in TAG_ORDER}
        for tag, text in items:
            by_tag[tag].append(text)
        for tag in TAG_ORDER:
            if not by_tag[tag]:
                continue
            lines.append("")
            lines.append("**【%s】**" % TAG_ZH[tag])
            lines.append("")
            for text in by_tag[tag]:
                lines.append("- " + text)
        lines.append("")

    out = "\n".join(lines).rstrip("\n") + "\n"
    io.open(DST, "w", encoding="utf-8", newline="").write(out)
    print("wrote %s (%d entries)" % (os.path.relpath(DST, ROOT), len(entries)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
