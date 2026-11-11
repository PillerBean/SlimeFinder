#!/usr/bin/env python3
"""Sanity checks for the SlimeFinder-Tree-2.0 structure."""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

required_dirs = [
    "src/app", "src/core", "src/gpu", "src/gui/mainwindow", "src/gui/dialogs",
    "src/gui/panels", "src/gui/widgets", "src/gui/services", "src/i18n", "src/utils",
    "tests/unit", "tests/integration", "tests/reference",
    "tools/gpu", "tools/probes",
    "assets/i18n", "assets/help",
    "packaging/windows", "scripts/build", "scripts/qa", "scripts/release", "scripts/maintenance",
    "telemetry", "third_party/cubiomes", "cmake", "docs/wiki", "docs/release_notes",
]
required_files = [
    "CMakeLists.txt", "CMakePresets.json", "cmake/Targets.cmake",
    "assets/i18n/zh-CN.json", "assets/i18n/zh-TW.json", "assets/i18n/en-US.json",
    "assets/help/tutorial-zh-CN.html", "assets/help/tutorial-zh-TW.html",
    "assets/help/tutorial-en-US.html",
    "tests/reference/SlimeChecker.exe",
    "packaging/windows/install.nsi",
    "scripts/build/configure.cmd", "scripts/build/build.cmd",
    "scripts/qa/check_ui_text.py", "scripts/qa/gui_selftest.ps1",
    "scripts/release/build_setup_release.ps1", "scripts/release/package_release.ps1",
]

errors = []
for d in required_dirs:
    if not os.path.isdir(os.path.join(ROOT, d)):
        errors.append("missing dir: " + d)
for f in required_files:
    if not os.path.isfile(os.path.join(ROOT, f)):
        errors.append("missing file: " + f)

# No source should point back into the old tree.
old_refs = re.compile(r"(\.\./)+SlimeFinder/(src|assets|third_party)|SlimeFinder[\\/]src")
for dirpath, _, files in os.walk(ROOT):
    if any(p in dirpath for p in ("build", "dist", ".git", "docs/history", "docs\\history")):
        continue
    for fn in files:
        if not fn.endswith((".cpp", ".h", ".cmake", ".txt", ".py", ".ps1", ".cmd", ".nsi")):
            continue
        p = os.path.join(dirpath, fn)
        try:
            s = open(p, encoding="utf-8", errors="ignore").read()
        except OSError:
            continue
        if old_refs.search(s):
            errors.append("old-tree reference: " + os.path.relpath(p, ROOT))

# No duplicate core implementation file names in the expected layout.
if os.path.exists(os.path.join(ROOT, "src/gui/MainWindow.cpp")):
    errors.append("old flat MainWindow.cpp still present")
if os.path.exists(os.path.join(ROOT, "src/gui/UiText.cpp")):
    errors.append("old flat UiText.cpp still present")

if errors:
    print("tree check FAILED")
    for e in errors:
        print("  " + e)
    sys.exit(1)
print("tree check OK: %d dirs, %d required files" % (len(required_dirs), len(required_files)))
