#!/usr/bin/env python3
"""生成 version.json（检查更新的镜像回退清单）。

发版流程：上传 GitHub Release 之后运行本脚本并提交 version.json。
  py -3 scripts/gen_version_manifest.py --version 3.10.12.10 \
      --notes-file docs/release_notes/v3.10.12.10.md
"""
import argparse
import io
import json
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DIST = os.path.join(ROOT, "dist")
OUT = os.path.join(ROOT, "version.json")
TAG_BASE = "https://github.com/PillerBean/SlimeFinder/releases/download"


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--version", required=True, help="例如 3.10.12.10")
    ap.add_argument("--tag", default=None, help="默认 v<version>")
    ap.add_argument("--notes-file", default="", help="更新说明 markdown/text 文件")
    args = ap.parse_args()

    v = args.version
    tag = args.tag or ("v" + v)
    candidates = [
        ("SlimeFinder-%s-win64.zip" % v),
        ("SlimeFinder-%s-Setup.exe" % v),
        ("SHA256SUMS-%s.txt" % v),
    ]
    assets = []
    for name in candidates:
        path = os.path.join(DIST, name)
        if not os.path.exists(path):
            print("missing:", path, file=sys.stderr)
            return 1
        assets.append({
            "name": name,
            "url": "%s/%s/%s" % (TAG_BASE, tag, name),
            "size": os.path.getsize(path),
        })

    notes = ""
    if args.notes_file:
        notes = io.open(args.notes_file, encoding="utf-8", newline="").read().replace("\r\n", "\n").strip()
    if not notes:
        notes = "SlimeFinder %s" % tag

    data = {"version": v, "tag": tag, "notes": notes, "assets": assets}
    io.open(OUT, "w", encoding="utf-8", newline="\n").write(
        json.dumps(data, ensure_ascii=False, indent=2) + "\n")
    print("wrote %s (version %s, %d assets)" % (os.path.relpath(OUT, ROOT), v, len(assets)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
