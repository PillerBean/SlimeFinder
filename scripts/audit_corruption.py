# -*- coding: utf-8 -*-
"""audit_corruption.py - source-tree corruption audit for SlimeFinder.

Why this exists
---------------
A text helper once received a flattened argument array, so it indexed the first
two *characters* of a string instead of two array elements; the result was a
global String.Replace over whole files (historical accidents: 'c' -> 'o' inside
MainWindow.cpp, '!' -> 'd' inside install.nsi).  Such damage is silent: a
mis-spelled local identifier is renamed consistently in its declaration and all
its uses, so the compiler stays green and every test still passes.  Only a
lexical audit can find it.

Checks
------
  [1] known-bad patterns      literal fragments left behind by the 'c' -> 'o'
                              and '!' -> 'd' accidents (list kept non-contiguous
                              in this file so the tool never flags itself)
  [2] dictionary check        every identifier / string-literal word is split on
                              '_' and camelCase; each part must be a known word
                              (Qt + MSVC headers, v3.1.1 reference, task books,
                              built-in vocabulary).  A part that is unknown but
                              becomes a *frequent* known word when some 'o' is
                              flipped back to 'c' is reported.
  [3] cross-file oracle       a token that occurs in exactly one file while one
                              of its o->c variants occurs elsewhere is almost
                              certainly corrupted
  [4] rare-digraph signature  'c' -> 'o' damage produces digraphs that barely
                              occur in English/code ("uo", "oh", "ao", "eo");
                              reported only for tokens no part of which is a
                              known word, and reviewed against an allowlist
  [5] key string constants    version numbers, release URL, access password,
                              cache/config/log/crash file names, kernel entry
  [6] artefact consistency    config.json parses with its sections; docs/ inside
                              dist/ matches docs/

Vendored third_party/ code is skipped by [1]-[4]; this tool's own source and its
evidence output are skipped by [1]; the two prose documents that quote damaged
spellings on purpose (docs/corruption_audit_report.md, docs/verification_report_v335.md)
are skipped by [1]-[4].  Every other source, script, test and document is checked.

Exit code: 0 = clean, 1 = findings (usable as a release gate).
Usage: py -3 scripts/audit_corruption.py [-v]
"""
import collections
import itertools
import json
import pathlib
import re
import sys

VERBOSE = "-v" in sys.argv
HERE = pathlib.Path(__file__).resolve().parent
REPO = HERE.parent
ROOT = REPO.parent
EXTS = {".cpp", ".h", ".md", ".txt", ".json", ".ps1", ".nsi", ".cl", ".cmake", ".bat", ".rc", ".py"}
SKIP_DIRS = {"build", "dist", "third_party", ".git"}
IDENT_RE = re.compile(r"[A-Za-z_][A-Za-z0-9_]*")
STR_RE = re.compile(r'"((?:[^"\\]|\\.)*)"')
WORD_RE = re.compile(r"[A-Za-z]{2,}")
SPLIT_RE = re.compile(r"[^A-Za-z]+")
CAMEL_RE = re.compile(r"(?<=[a-z0-9])(?=[A-Z])")
SHORT_RE = re.compile(r"\bo\d{1,3}\b")

# pattern list written as split literals: the source never contains the
# contiguous damaged spelling, so this file cannot flag itself
_BAD = [("ino", "lude"), ("o", "onst"), ("o", "onstexpr"), ("o", "onfig"), ("fun", "otion"),
        ("o", "aohe"), ("not", "oaohe"), ("he", "oked"), ("Che", "oked"), ("QStri", "og"),
        ("dd", "efine"), ("din", "clude"), ("dinsert", "macro"), ("den", "dif")]
KNOWN_BAD = [a + b for a, b in _BAD]
SELF_MARKERS = ("audit_corruption.py", "corruption_audit")
# documented exclusions: files that quote or discuss damaged spellings on purpose
# (the audit report lists every damaged fragment verbatim; these are prose, not code)
SKIP_BAD_PATTERN_FILES = ("docs/corruption_audit", "docs/verification_report_v335")
SKIP_LEXICAL_FILES = ("docs/corruption_audit_report.md", "docs/verification_report_v335.md")
# historical captures kept for traceability only: produced before the v3.3.5 encoding
# fix, so their Chinese labels contain U+FFFD (see docs/evidence_legacy/README.md).
# Documented exception - the current-version evidence in docs/*_v335.txt IS checked.
SKIP_LEGACY_EVIDENCE = "docs/evidence_legacy/"
# single-letter+digit names reviewed as intentional (test fixtures using
# SearchOptions o, o2..o5 sequences)
ALLOW_SHORT = {("tests/test_slime.cpp", "o2"), ("tests/test_slime.cpp", "o3"),
               ("tests/test_slime.cpp", "o4"), ("tests/test_slime.cpp", "o5"),
               ("tests/verify_against_reference.py", "o4")}
# tokens reviewed as intentional (documented "ours" prefix in the comparison script)
ALLOW_CROSSFILE = {"our"}
# tokens with a rare digraph that are correct: GPU brand name, Windows VERSIONINFO field,
# Win32 audio handle type
ALLOW_DIGRAPH_TOKENS = {"radeon", "fileos", "hwaveout"}
MIN_FIX_WEIGHT = 10          # a restored word must be this frequent in the vocabulary

BUILTIN = """
slime chunk chunks column columns config configs console constant constants copy counter caches
cache cached cachefile cachedir legacy ocl opencl kernel kernels macro macros msvc string strings
struct stream state static status stderr stdin stdout style styles thread threads timer timers
token tokens total values variable variables version versions vector verify verified wheel widget
soul sculk mangrove cherry windswept dripstone lush podzol badlands savanna taiga
widgets worker workers zone zones lanzoul wwben bedrock minecraft mojang bench benchmark benchmarks
selftest offscreen gpu gpuinfo cpu cpus cpuonly gpuid tile tiles tiling dispatch dispatches
bandwidth density mt19937 lcg nsi nsis uninstall uninstaller ps1 pwsh cmd bat exe dll lib obj pdb
qmake cmake ninja clang gcc utf16 utf8 hashmap hashset autoload autosave ints longs floats doubles
chars bytes bits foreach endif endforeach lround checked check checkedchunks
alignas alignof and asm auto bool break case catch char class concept const consteval constexpr
constinit const_cast continue co_await co_return co_yield decltype default delete do double
dynamic_cast else enum explicit export extern false float for friend goto if inline int long
mutable namespace new noexcept not nullptr operator or private protected public register
reinterpret_cast requires return short signed sizeof static static_assert static_cast struct
switch template this thread_local throw true try typedef typeid typename union unsigned using
virtual void volatile wchar_t while override final
""".split()
WORDS = collections.Counter({w: 20 for w in BUILTIN})
WORD_SOURCES = []


def harvest_sources():
    """External 'correct spelling' vocabulary - never the audited files."""
    qt = pathlib.Path(r"C:\Qt\6.11.2\msvc2022_64\include")
    if qt.is_dir():
        WORD_SOURCES.extend((p, 1) for p in qt.rglob("*.h"))
    msvc = pathlib.Path(r"D:\Visual Studio\Visual Studio\VC\Tools\MSVC")
    if msvc.is_dir():
        for inc in msvc.glob("*/include"):
            WORD_SOURCES.extend((p, 2) for p in inc.glob("*") if p.suffix in (".h", ".hpp"))
    ref = ROOT / "_v311_ref"
    if ref.is_dir():
        WORD_SOURCES.extend((p, 3) for p in ref.rglob("*") if p.is_file() and p.suffix in (".txt", ".md"))
    books = ROOT / "模板：原始文件"
    if books.is_dir():
        WORD_SOURCES.extend((p, 1) for p in books.glob("*") if p.is_file())
    for path, weight in WORD_SOURCES:
        try:
            text = path.read_text(encoding="utf-8", errors="ignore")
        except OSError:
            continue
        for m in WORD_RE.finditer(text):
            WORDS[m.group(0).lower()] += weight


def repo_files():
    out = []
    for p in sorted(REPO.rglob("*")):
        if not p.is_file() or p.suffix.lower() not in EXTS:
            continue
        if SKIP_DIRS & set(p.relative_to(REPO).parts):
            continue
        out.append(p)
    return out


def parts_of(text):
    out = []
    for raw in SPLIT_RE.split(text):
        if raw:
            out.extend(p for p in CAMEL_RE.split(raw) if p)
    return out


def o_variants(part, cap=64):
    pos = [i for i, ch in enumerate(part) if ch == "o"]
    if not pos or len(pos) > 6:
        return []
    out = []
    for bits in itertools.product((0, 1), repeat=len(pos)):
        if not any(bits):
            continue
        ch = list(part)
        for p, b in zip(pos, bits):
            if b:
                ch[p] = "c"
        out.append("".join(ch))
        if len(out) >= cap:
            break
    return out


def suspect(part):
    """Return the restored spelling when `part` only makes sense with c for o."""
    low = part.lower()
    if len(low) < 3 or WORDS.get(low, 0) > 0:
        return None
    best = None
    for v in o_variants(low):
        if WORDS.get(v, 0) >= MIN_FIX_WEIGHT and (best is None or WORDS[v] > WORDS[best]):
            best = v
    return best


def known_token(tok):
    return any(len(p) >= 3 and WORDS.get(p.lower(), 0) > 0 for p in parts_of(tok))


def main():
    harvest_sources()
    files = repo_files()
    findings = []
    print("=" * 78)
    print("SlimeFinder corruption audit: %d files, vocabulary %d words" % (len(files), len(WORDS)))
    print("=" * 78)
    if not WORD_SOURCES:
        print("[warn] no external word source found; dictionary check is weaker")

    where = collections.defaultdict(set)
    texts = {}
    for p in files:
        try:
            texts[p] = p.read_text(encoding="utf-8", errors="ignore")
        except OSError:
            continue
        rel = p.relative_to(REPO).as_posix()
        for m in IDENT_RE.finditer(texts[p]):
            where[m.group(0)].add(rel)

    print("\n[1-4] lexical checks")
    for p, text in texts.items():
        rel = p.relative_to(REPO).as_posix()
        if any(m in rel for m in SELF_MARKERS) or rel.startswith(SKIP_LEXICAL_FILES):
            continue
        if rel.startswith(SKIP_LEGACY_EVIDENCE):
            continue
        low = text.lower()
        bad_hits = 0
        for bad in KNOWN_BAD:
            for m in re.finditer(re.escape(bad), low):
                ln = low[: m.start()].count("\n") + 1
                findings.append(("bad-pattern", rel, ln, "known damaged fragment '%s'" % bad))
                bad_hits += 1
        if p.suffix == ".nsi":
            for ln, line in enumerate(text.splitlines(), 1):
                if re.match(r"^\s*d(efine|include|insertmacro|if|endif|macro)\b\s*\S", line):
                    findings.append(("bad-pattern", rel, ln, "line looks like a '!'-command missing its '!'"))
        if bad_hits:
            print("    %-46s %d known-bad fragment(s)" % (rel, bad_hits))
        for ln, line in enumerate(text.splitlines(), 1):
            tokens = IDENT_RE.findall(line)
            for m in STR_RE.finditer(line):
                tokens += [t for t in SPLIT_RE.split(m.group(1)) if t]
            for tok in tokens:
                fixed = [(part, fx) for part in parts_of(tok) for fx in [suspect(part)] if fx]
                if fixed:
                    findings.append(("dictionary", rel, ln, "%s -> %s"
                                     % (tok, " ".join("%s->%s" % f for f in fixed))))
                    continue
                lowt = tok.lower()
                if len(lowt) >= 3 and not known_token(tok) and lowt not in ALLOW_DIGRAPH_TOKENS:
                    for dig in ("uo", "oh", "ao", "eo"):
                        if dig in lowt:
                            findings.append(("digraph", rel, ln, "%s (rare digraph '%s')" % (tok, dig)))
                            break
                if len(tok) >= 4 and "o" in tok and tok.lower() not in ALLOW_CROSSFILE:
                    if not where.get(tok, set()) - {rel}:
                        for v in o_variants(tok):
                            hit = where.get(v, set())
                            if hit:
                                findings.append(("cross-file", rel, ln, "%s -> %s (seen in %s)"
                                                 % (tok, v, ", ".join(sorted(hit))[:60])))
                                break
            for m in SHORT_RE.finditer(line):
                if (rel, m.group(0)) not in ALLOW_SHORT and not known_token(m.group(0)):
                    findings.append(("short-name", rel, ln, "%s (single-letter+digit name; "
                                     "verify it is not a 'c' -> 'o' victim)" % m.group(0)))

    # ---- expected version: single source of truth = src/app.rc (VERSIONINFO) ----
    # (detected BEFORE the constant checks so the checks below never hard-code a
    #  release number - the v3.3.6 run showed how easily such literals go stale)
    expected = ""
    p_rc = REPO / "src" / "app.rc"
    if p_rc.is_file():
        m = re.search(r'#define\s+APP_VERSION\s+"([0-9.]+)"', texts[p_rc]) or \
            re.search(r'FILEVERSION\s+([0-9,]+)', texts[p_rc])
        if m:
            expected = m.group(1).replace(",", ".").rsplit(".", 1)[0]
    if not expected:
        findings.append(("const", "src/app.rc", 0, "cannot detect the version this tree builds"))

    const_checks = [
        ("release URL", "https://wwben.lanzoul.com/b01d77eebe", "src/gui/MainWindow.cpp"),
        # v3.7.5：发布页已取消访问密码，不再作为关键常量校验
        ("credits entry", "鸣谢", "src/gui/UpdateHistory.cpp"),
        ("cache file name", "slime_cache.slimecache", "src/gui/MainWindow.cpp"),
        ("legacy cache name", "slime_cache.cache", "src/gui/MainWindow.cpp"),
        ("config file name", "config.json", "src/gui/MainWindow.cpp"),
        ("build date file", "build_date.txt", "src/gui/MainWindow.cpp"),
        ("log directory", '"logs"', "src/gui/MainWindow.cpp"),
        ("crash directory", "crashes", "src/main.cpp"),
        ("OpenCL kernel entry", "__kernel void scan_java_slime", "src/gpu/slime_kernel.cl"),
        ("version (main)", "v" + expected, "src/main.cpp"),
        ("version (readme)", "v" + expected, "README.md"),
        ("version (cmake)", expected, "CMakeLists.txt"),
        ("version (rc)", expected, "src/app.rc"),
        ("version (nsi)", expected, "scripts/install.nsi"),
    ]
    print("\n[5] key string constants")
    for label, needle, rel in const_checks:
        p = REPO / rel
        n = texts.get(p, "").count(needle) if p.is_file() else 0
        print("    %-20s %-34s x%-3d %s" % (label, needle, n, "ok" if n else "MISSING"))
        if not n:
            findings.append(("const", rel, 0, "missing %s (%s)" % (label, needle)))

    # version drift: the packaged-zip name in *current-facing* files must match the
    # version this tree builds (a stale "SlimeFinder-3.3.3-win64.zip" in the README was
    # found by this check).  Historical reports and this tool's own comments legitimately
    # mention older package names, so only these files are checked.
    current_facing = ["README.md", "docs/build_instructions.md", "docs/user_manual.md",
                      "docs/architecture.md", "scripts/package_release.ps1",
                      "scripts/verify_release.ps1", "scripts/install.nsi"]
    zip_re = re.compile(r"SlimeFinder-([0-9]+\.[0-9]+\.[0-9]+)-win64\.zip")
    drift = collections.Counter()
    for rel in current_facing:
        text = texts.get(REPO / rel)
        if text is None:
            continue
        for m in zip_re.finditer(text):
            if m.group(1) != expected:
                ln = text[: m.start()].count("\n") + 1
                findings.append(("version-drift", rel, ln, "package name %s but this tree builds %s"
                                 % (m.group(0), expected)))
                drift[rel] += 1
    # version constants inside the sources must equal the version this tree builds
    # (v3.3.6 found MainWindow.cpp still declaring kAppVersion = "v3.3.5" while
    # main.cpp / app.rc had already moved to 3.3.6 - a silent release-version drift)
    ver_re = re.compile(r'(kAppVersion|kVersion|setApplicationVersion)\s*[=(]\s*[^")\n]*"(v?[0-9]+\.[0-9]+\.[0-9]+)"')
    src_drift = 0
    for p, text in texts.items():
        rel = p.relative_to(REPO).as_posix()
        if not (rel.startswith("src/") and p.suffix in (".cpp", ".h")):
            continue
        for m in ver_re.finditer(text):
            got = m.group(2).lstrip("v")
            if got != expected:
                ln = text[: m.start()].count("\n") + 1
                findings.append(("version-drift", rel, ln, "%s = \"%s\" but this tree builds %s"
                                 % (m.group(1), m.group(2), expected)))
                src_drift += 1
    print("    %-20s %-34s      %s" % ("zip name version", expected,
          "ok" if not drift else "DRIFT: " + ", ".join("%s x%d" % kv for kv in sorted(drift.items()))))
    print("    %-20s %-34s      %s" % ("source constants", expected,
          "ok" if not src_drift else "DRIFT x%d" % src_drift))

    print("\n[6] artefact consistency")
    for rel in ("build/config.json", "dist/SlimeFinder/config.json"):
        p = REPO / rel
        if not p.is_file():
            print("    %-34s (absent)" % rel)
            continue
        try:
            data = json.loads(p.read_text(encoding="utf-8-sig"))
            print("    %-34s json ok, %d sections: %s"
                  % (rel, len(data), ",".join(sorted(data.keys()))))
        except Exception as exc:                                    # noqa: BLE001
            print("    %-34s JSON ERROR: %s" % (rel, exc))
            findings.append(("json", rel, 0, "unparsable: %s" % exc))
    dist_docs = REPO / "dist" / "SlimeFinder" / "docs"
    same = diff = absent = skipped = 0
    if dist_docs.is_dir():
        for p in sorted((REPO / "docs").glob("*")):
            if p.suffix not in (".md", ".txt"):
                continue
            # this tool's own evidence output is regenerated by every run, so a
            # byte comparison against the packaged copy is meaningless
            if p.name.startswith("corruption_audit_"):
                skipped += 1
                continue
            q = dist_docs / p.name
            if not q.is_file():
                absent += 1
                continue
            if p.read_bytes() == q.read_bytes():
                same += 1
            else:
                diff += 1
                findings.append(("docs-copy", "docs/" + p.name, 0, "differs from packaged copy"))
        print("    docs copies: identical %d, differing %d, absent %d "
              "(absent = written after packaging; %d generated file(s) skipped)"
              % (same, diff, absent, skipped))

    print("\n[7] file hygiene (encoding / control bytes)")
    hygiene = collections.Counter()
    for p in files:
        rel = p.relative_to(REPO).as_posix()
        if rel.startswith(SKIP_LEGACY_EVIDENCE):
            continue
        raw = p.read_bytes()
        if raw[:2] in (b"\xff\xfe", b"\xfe\xff"):
            body = raw[2:].decode("utf-16-le" if raw[:2] == b"\xff\xfe" else "utf-16-be",
                                  errors="replace")
            if p.suffix.lower() != ".txt":
                findings.append(("hygiene", rel, 0, "UTF-16 encoded source/document (expected UTF-8)"))
                hygiene["utf16"] += 1
        else:
            try:
                body = raw.decode("utf-8-sig")
            except UnicodeDecodeError as exc:
                body = raw.decode("utf-8", errors="replace")
                findings.append(("hygiene", rel, 0, "invalid UTF-8: %s" % str(exc)[:52]))
                hygiene["invalid-utf8"] += 1
        n_nul = body.count("\x00")
        n_rep = body.count("\ufffd")
        if n_nul:
            findings.append(("hygiene", rel, 0, "NUL bytes x%d" % n_nul))
            hygiene["nul"] += 1
        if n_rep:
            findings.append(("hygiene", rel, 0, "U+FFFD replacement chars x%d (mojibake)" % n_rep))
            hygiene["mojibake"] += 1
    print("    %d files checked: %s" % (len(files),
          ", ".join("%s=%d" % kv for kv in sorted(hygiene.items())) or "no encoding defect"))

    groups = collections.Counter(f[0] for f in findings)

    print("\n" + "=" * 78)
    if findings:
        print("FINDINGS: %d" % len(findings))
        for kind, rel, ln, msg in findings[:60]:
            print("  [%-11s] %s:%d  %s" % (kind, rel, ln, msg))
        if len(findings) > 60:
            print("  ... %d more" % (len(findings) - 60))
    else:
        print("CLEAN: no corruption signature in %d files" % len(files))
    print("summary: " + (", ".join("%s=%d" % kv for kv in sorted(groups.items())) or "none"))
    if VERBOSE:
        print("word sources: %d" % len(WORD_SOURCES))
    print("=" * 78)
    return 1 if findings else 0


if __name__ == "__main__":
    sys.exit(main())
