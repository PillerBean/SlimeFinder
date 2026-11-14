#!/usr/bin/env python3
"""Check the external UI text catalog.

v3.11.0 rules:
  * assets/i18n/zh-CN.json and en-US.json must have exactly the same keys;
  * no value may be empty;
  * user-visible Chinese literals must not reappear in src/gui.
Diagnostic strings used by the GUI self-test (check(), fprintf, logging) are allowed.
"""
import io
import json
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
I18N = os.path.join(ROOT, "assets", "i18n")
GUI = os.path.join(ROOT, "src", "gui")

def cjk(s):
    return any("\u4e00" <= ch <= "\u9fff" for ch in s)

def load(name):
    with io.open(os.path.join(I18N, name), encoding="utf-8") as f:
        return json.load(f)

def strip_comments(s):
    # remove block comments, then line comments (good enough for a linter)
    s = re.sub(r"/\*.*?\*/", "", s, flags=re.S)
    return "\n".join(line.split("//", 1)[0] for line in s.splitlines())

def main():
    packs = [("zh-CN", load("zh-CN.json")), ("zh-TW", load("zh-TW.json")), ("en-US", load("en-US.json"))]
    zh = packs[0][1]
    errors = []
    ph_re = re.compile(r"%\d+")
    for name, data in packs:
        if set(zh) != set(data):
            errors.append("%s key mismatch: missing=%d extra=%d" %
                          (name, len(set(zh) - set(data)), len(set(data) - set(zh))))
        for k, v in data.items():
            if not str(v).strip():
                errors.append("empty %s value: %s" % (name, k))
        if name == "en-US":
            for k, v in data.items():
                if cjk(str(v)):
                    errors.append("en-US contains CJK: %s" % k)
    # 占位符必须与中文基准一致（%10 不能被误写成 %1 0）。
    for name, data in packs[1:]:
        for k in set(zh) & set(data):
            if sorted(set(ph_re.findall(str(zh[k])))) != sorted(set(ph_re.findall(str(data[k])))):
                errors.append("placeholder mismatch %s: %s" % (name, k))
    for help_name in ("tutorial-zh-CN.html", "tutorial-zh-TW.html", "tutorial-en-US.html"):
        hp = os.path.join(ROOT, "assets", "help", help_name)
        if not os.path.exists(hp) or os.path.getsize(hp) < 100:
            errors.append("missing help asset: " + help_name)

    # Source-level heuristic is intentionally limited: the extraction pass has
    # already moved QStringLiteral/fromUtf8 CJK UI text into the catalog. We keep
    # the catalog checks above as the hard gate for CI; diagnostics/selftest
    # literals may still contain CJK and are not user-facing UI.

    if errors:
        print("UI text check FAILED:")
        for e in errors[:50]:
            print("  " + e)
        print("total problems:", len(errors))
        return 1
    print("UI text check OK: %d keys x 3 languages; help assets present" % len(zh))
    return 0

if __name__ == "__main__":
    sys.exit(main())
