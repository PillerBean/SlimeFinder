#!/usr/bin/env python3
"""生成自动更新清单（Setup-only）。

稳定通道：version.json（只放正式版）
测试通道：version-beta.json（可放 Pre-release）

发版后（Release 已上传）运行：
  py -3 scripts/gen_version_manifest.py --channel beta --version 3.10.12.10 \
      --notes-file docs/release_notes/v3.10.12.10.md
  py -3 scripts/gen_version_manifest.py --channel stable --version 3.11.0 \
      --notes-file docs/release_notes/v3.11.0.md
"""
import argparse
import io
import json
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
DIST = os.path.join(ROOT, "dist")
TAG_BASE = "https://github.com/PillerBean/SlimeFinder/releases/download"


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--version", required=True, help="例如 3.10.12.10")
    ap.add_argument("--channel", choices=["stable", "beta"], default="beta")
    ap.add_argument("--tag", default=None, help="默认 v<version>")
    ap.add_argument("--notes-file", default="", help="更新说明 markdown/text 文件")
    args = ap.parse_args()

    v = args.version
    tag = args.tag or ("v" + v)
    names = [
        "SlimeFinder-%s-Setup.exe" % v,
        "SHA256SUMS-%s.txt" % v,
    ]
    assets = []
    for name in names:
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
    out = os.path.join(ROOT, "version.json" if args.channel == "stable" else "version-beta.json")
    io.open(out, "w", encoding="utf-8", newline="\n").write(
        json.dumps(data, ensure_ascii=False, indent=2) + "\n")
    print("wrote %s (%s, %s, %d assets)" % (os.path.relpath(out, ROOT), args.channel, v, len(assets)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
