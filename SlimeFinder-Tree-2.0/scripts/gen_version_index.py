#!/usr/bin/env python3
"""生成 Wiki「版本索引」页面（按版本号排序）。

GitHub Releases 页面按发布时间排序，补传历史版本后会乱；本脚本从 Releases API
生成按版本号排序的索引，写入 docs/wiki/Version-Index.md。
运行前需要 gh 已登录；之后用 scripts/publish_wiki.ps1 发布到 Wiki。
"""
import io
import json
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GH = os.environ.get("GH_BIN") or os.path.join(
    os.path.dirname(ROOT), "github_tools", "gh_dir", "bin", "gh.exe")
REPO = "PillerBean/SlimeFinder"
OUT = os.path.join(ROOT, "docs", "wiki", "Version-Index.md")


def gh(args):
    env = dict(os.environ)
    env["PATH"] = os.path.dirname(GH) + os.pathsep + env.get("PATH", "")
    p = subprocess.run([GH] + args, capture_output=True, text=True,
                       encoding="utf-8", errors="replace", env=env)
    if p.returncode != 0:
        print(p.stderr, file=sys.stderr)
        raise SystemExit(1)
    return p.stdout


def ver_key(tag):
    v = tag[1:] if tag.startswith("v") else tag
    v = re.sub(r"-archive$", "", v)
    parts = []
    for x in re.split(r"[.\-]", v):
        parts.append((0, int(x)) if x.isdigit() else (1, x))
    return parts


def main():
    releases = json.loads(gh(["api", "--paginate",
                              "repos/%s/releases?per_page=100" % REPO]))
    items = []
    for r in releases:
        if r.get("draft"):
            continue
        tag = r.get("tag_name", "")
        if not tag:
            continue
        typ = "测试版" if r.get("prerelease") else "正式版"
        items.append((tag, r.get("name", ""), typ, r.get("html_url", "")))
    items.sort(key=lambda x: ver_key(x[0]), reverse=True)

    lines = [
        "# 版本索引（按版本号排序）",
        "",
        "> GitHub Releases 页面默认按**发布时间**排序（补传历史版本会打乱顺序），",
        "> 本页按版本号从新到旧排列，便于查找和下载。",
        "> 本页由 `scripts/gen_version_index.py` 自动生成。",
        "",
        "| 版本 | 类型 | 下载页 |",
        "|---|---|---|",
    ]
    for tag, name, typ, url in items:
        label = name.strip() or tag
        lines.append("| %s | %s | [%s](%s) |" % (label, typ, tag, url))
    lines.append("")
    lines.append("共 %d 个已发布版本。" % len(items))
    io.open(OUT, "w", encoding="utf-8", newline="\n").write("\n".join(lines) + "\n")
    print("wrote %s (%d versions)" % (os.path.relpath(OUT, ROOT), len(items)))


if __name__ == "__main__":
    main()
