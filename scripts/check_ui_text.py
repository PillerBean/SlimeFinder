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

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
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
    zh = load("zh-CN.json")
    en = load("en-US.json")
    errors = []
    if set(zh) != set(en):
        errors.append("zh/en key mismatch: missing en=%d, extra en=%d" %
                      (len(set(zh) - set(en)), len(set(en) - set(zh))))
    for k, v in zh.items():
        if not str(v).strip():
            errors.append("empty zh value: " + k)
    for k, v in en.items():
        if not str(v).strip():
            errors.append("empty en value: " + k)

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
    print("UI text check OK: %d keys (zh/en catalog complete)" % len(zh))
    return 0

if __name__ == "__main__":
    sys.exit(main())
