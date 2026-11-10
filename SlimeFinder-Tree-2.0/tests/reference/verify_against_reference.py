#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
verify_against_reference.py - 新引擎 (slime_tests) 与参考实现 (SlimeChecker.exe) 交叉验证

验证内容：
  1) Java 版扫描：随机种子 × 多区域（含 int32 溢出边界坐标）坐标集合完全一致
  2) 基岩版扫描：区域坐标集合完全一致
  3) 严格图案匹配（方向锁定）：窗口集合完全一致（含任务书验收场景）
  4) 密度搜索：窗口集合 + 密度值完全一致（含任务书验收场景）
  5) GUI 排序等价性：按查询起点距离排序的最近 N 个与 Python GUI 逻辑一致

用法：
  python verify_against_reference.py [参考exe路径] [新slime_tests路径]
"""
import math
import os
import random
import subprocess
import sys
import tempfile

# 证据日志统一 UTF-8：Windows 重定向时 Python 默认按 ANSI 代码页编码 stdout，
# 与 Qt/原生程序的 UTF-8 输出混在同一份日志里会变成 U+FFFD（污染审计发现：
# reference111 证据曾有 503 个替换字符）。这里显式固定 UTF-8。
try:
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    sys.stderr.reconfigure(encoding="utf-8", errors="replace")
except (AttributeError, ValueError):
    pass

HERE = os.path.dirname(os.path.abspath(__file__))
PROJECT = os.path.abspath(os.path.join(HERE, "..", ".."))

def default_ref():
    # v3.12.0：新树自包含参考实现；旧树外的模板仅作为后备。
    local = os.path.join(HERE, "SlimeChecker.exe")
    if os.path.exists(local):
        return local
    return None

def default_ours():
    for cand in (os.path.join(PROJECT, "build", "slime_tests.exe"),):
        if os.path.exists(cand):
            return cand
    return None

REF = sys.argv[1] if len(sys.argv) > 1 else default_ref()
OUR = sys.argv[2] if len(sys.argv) > 2 else default_ours()

if not REF or not os.path.exists(REF):
    sys.exit("找不到参考程序 SlimeChecker.exe（参数1或默认路径）")
if not OUR or not os.path.exists(OUR):
    sys.exit("找不到 slime_tests.exe（参数2或默认路径）")

PASS = 0
FAIL = 0

def report(ok, name, detail=""):
    global PASS, FAIL
    if ok:
        PASS += 1
        print(f"  [PASS] {name}")
    else:
        FAIL += 1
        print(f"  [FAIL] {name} {detail}")

def run_ref(args):
    r = subprocess.run([REF] + args, capture_output=True, text=True, timeout=600)
    if r.returncode != 0:
        raise RuntimeError(f"参考程序失败 {args}: {r.stderr[:300]}")
    return r.stdout

def run_our(args):
    r = subprocess.run([OUR] + args, capture_output=True, text=True, timeout=600)
    if r.returncode != 0:
        raise RuntimeError(f"新引擎失败 {args}: {r.stderr[:300]}")
    return r.stdout

def parse_scan(text):
    s = set()
    for line in text.splitlines():
        line = line.strip()
        if not line:
            continue
        x, z = line.split()
        s.add((int(x), int(z)))
    return s

def parse_windows(text, with_density=False):
    out = set()
    for line in text.splitlines():
        line = line.strip()
        if not line:
            continue
        parts = line.split()
        if with_density:
            a, b, c, d, e = parts
            out.add((int(a), int(b), int(c), int(d), float(e)))
        else:
            a, b, c, d = parts
            out.add((int(a), int(b), int(c), int(d)))
    return out

def make_pattern_file(rows, cols, cells):
    """cells: list of (r,c,state) state: 'g'|'w'|'i'"""
    m = [[2] * cols for _ in range(rows)]
    for (r, c, st) in cells:
        m[r][c] = 1 if st == 'g' else (0 if st == 'w' else 2)
    f = tempfile.NamedTemporaryFile("w", suffix=".pat", delete=False, encoding="utf-8")
    f.write(f"{rows} {cols}\n")
    for row in m:
        f.write(" ".join(map(str, row)) + "\n")
    f.close()
    return f.name

def density_tuple_key(t):
    return t[:4]

def near_equal(a, b, rel=1e-5, abs_=1e-9):
    return abs(a - b) <= abs_ + rel * max(abs(a), abs(b))

print(f"参考程序: {REF}")
print(f"被测引擎: {OUR}\n")

# ---------- 1) Java 扫描 ----------
print("[1] Java 版扫描坐标集合一致性")
java_seeds = [0, 1, -1, 123456789, -987654321, 9223372036854775807, -9223372036854775808,
              9876543210123, 42, -271828182845]
regions = [(-400, 400, -400, 400), (-100, 300, -500, 100), (0, 200, 0, 200), (-40, 40, -40, 40)]
# int32 溢出边界（算法必须精确模拟 32 位回绕）
regions += [(2147482800, 2147483050, -2147483100, -2147482850), (2147482900, 2147483100, 2147482900, 2147483100)]
for seed in java_seeds:
    for (minx, maxx, minz, maxz) in regions:
        args = ["0", str(seed), str(minx), str(maxx), str(minz), str(maxz)]
        try:
            ref = parse_scan(run_ref(args))
            our = parse_scan(run_our(args))
        except RuntimeError as e:
            report(False, f"Java seed={seed} {minx}..{maxx}", str(e))
            continue
        ok = ref == our
        report(ok, f"Java seed={seed} region=({minx},{maxx},{minz},{maxz}) chunks={len(ref)}",
               "" if ok else f"ref={len(ref)} our={len(our)} 差异={len(ref ^ our)}")

# ---------- 2) 基岩版扫描 ----------
print("\n[2] 基岩版扫描坐标集合一致性")
for (minx, maxx, minz, maxz) in [(-300, 300, -300, 300), (-500, 100, -200, 400), (-100, 0, -50, 150)]:
    args = ["1", "0", str(minx), str(maxx), str(minz), str(maxz)]
    ref = parse_scan(run_ref(args))
    our = parse_scan(run_our(args))
    ok = ref == our
    report(ok, f"基岩 region=({minx},{maxx},{minz},{maxz}) chunks={len(ref)}",
           "" if ok else f"ref={len(ref)} our={len(our)} 差异={len(ref ^ our)}")

# ---------- 3) 严格图案匹配（方向锁定） ----------
print("\n[3] 严格图案匹配窗口一致性（方向锁定）")
patdir = tempfile.mkdtemp(prefix="slime_pat_")
patterns = [
    ("2x2全绿", 2, 2, [(0,0,'g'),(0,1,'g'),(1,0,'g'),(1,1,'g')]),                     # 验收场景
    ("3x3L形", 3, 3, [(0,0,'g'),(1,0,'g'),(1,1,'g'),(0,1,'w')]),
    ("1x2绿", 1, 2, [(0,0,'g'),(0,1,'g')]),
    ("2x4混合", 2, 4, [(0,0,'w'),(0,2,'g'),(1,1,'w'),(1,3,'g')]),
    ("4x4稀疏", 4, 4, [(0,0,'g'),(3,3,'g'),(1,2,'w'),(2,1,'w')]),
    ("5x5双斜线", 5, 5, [(0,0,'g'),(1,1,'g'),(2,2,'g'),(3,3,'g'),(4,4,'g'),(0,4,'w')]),
    ("验收2x2变体(含忽略)", 3, 2, [(0,0,'g'),(1,0,'g'),(2,1,'g'),(1,1,'w')]),
]
for (name, rows, cols, cells) in patterns:
    pf = make_pattern_file(rows, cols, cells)
    for (minx, maxx, minz, maxz) in [(-400, 400, -400, 400), (-100, 300, -500, 100)]:
        for seed in ([0, 123456789] if "2x2" in name else [0]):
            args = ["0", str(seed), str(minx), str(maxx), str(minz), str(maxz), "1", pf, "1"]
            ref = parse_windows(run_ref(args))
            our = parse_windows(run_our(args))
            ok = ref == our
            report(ok, f"严格[{name}] seed={seed} region=({minx},{maxx}) 窗口={len(ref)}",
                   "" if ok else f"ref={len(ref)} our={len(our)} 差异={len(ref ^ our)}")
    os.unlink(pf)

# ---------- 4) 密度搜索 ----------
print("\n[4] 密度搜索窗口一致性")
for (grid, thr) in [(5, 0.3), (2, 0.5), (3, 0.4), (8, 0.2), (5, 0.05)]:
    for (minx, maxx, minz, maxz) in [(-300, 300, -300, 300), (-200, 200, -100, 400)]:
        for seed in ([0] if grid != 5 else [0, 987654321]):
            args = ["0", str(seed), str(minx), str(maxx), str(minz), str(maxz), "2", str(grid), f"{thr:.3f}"]
            ref = parse_windows(run_ref(args), with_density=True)
            our = parse_windows(run_our(args), with_density=True)
            refk = {(a, b, c, d) for (a, b, c, d, _) in ref}
            ourk = {(a, b, c, d) for (a, b, c, d, _) in our}
            ok = refk == ourk
            detail = ""
            if ok:
                # 密度数值一致性
                rd = {(a, b, c, d): e for (a, b, c, d, e) in ref}
                od = {(a, b, c, d): e for (a, b, c, d, e) in our}
                bad = [k for k in refk if not near_equal(rd[k], od[k], rel=1e-4)]
                ok = len(bad) == 0
                detail = f"密度不一致 {len(bad)}" if bad else ""
            report(ok, f"密度 g={grid} thr={thr} seed={seed} region=({minx},{maxx}) 窗口={len(refk)}", detail)

# ---------- 5) GUI 排序等价性（最近 N 个） ----------
print("\n[5] GUI 最近 N 个排序等价性（对照 Python GUI 逻辑）")

def python_gui_topn(raw_lines, with_density, startx, startz, n):
    """复刻 史莱姆区块查找.py 的 _finalize_results：按窗口中心距离稳定排序取前 N"""
    items = []
    for line in raw_lines.splitlines():
        line = line.strip()
        if not line:
            continue
        parts = line.split()
        if with_density:
            minx, maxx, minz, maxz = map(int, parts[:4])
            density = float(parts[4])
        else:
            minx, maxx, minz, maxz = map(int, parts[:4])
            density = None
        cx = (minx + maxx) / 2.0
        cz = (minz + maxz) / 2.0
        d = math.hypot(cx - startx, cz - startz)
        items.append((d, (minx, maxx, minz, maxz, density)))
    items.sort(key=lambda t: t[0])  # python list.sort 稳定
    return items[:n]

def cmp_near_lines(ref_items, our_text):
    lines = [ln for ln in our_text.splitlines() if ln.strip()]
    if len(lines) != len(ref_items):
        return False, f"数量 ref={len(ref_items)} our={len(lines)}"
    for i, (d, (minx, maxx, minz, maxz, density)) in enumerate(ref_items):
        parts = lines[i].split()
        od = float(parts[0])
        o4 = tuple(map(int, parts[1:5]))
        if not near_equal(od, d, rel=1e-9, abs_=1e-9):
            return False, f"第{i}名距离 {od} != {d}"
        if o4 != (minx, maxx, minz, maxz):
            return False, f"第{i}名坐标 {o4} != {(minx, maxx, minz, maxz)}"
        if density is not None:
            if len(parts) < 6 or not near_equal(float(parts[5]), density, rel=1e-4):
                return False, f"第{i}名密度不一致"
    return True, ""

strict_pat = make_pattern_file(3, 3, [(0, 0, 'g'), (1, 1, 'g'), (2, 2, 'g'), (0, 2, 'w')])
cases = []
# 严格（验收场景：2x2 全绿）
pf22 = make_pattern_file(2, 2, [(0, 0, 'g'), (0, 1, 'g'), (1, 0, 'g'), (1, 1, 'g')])
for (startx, startz, n, region) in [(0, 0, 30, (-400, 400, -400, 400)), (17, -23, 12, (-300, 300, -500, 500)), (-5, 9, 40, (-400, 400, -400, 400))]:
    cases.append(("strict-2x2", pf22, "1", [pf22, "1"], startx, startz, n, region))
for (startx, startz, n) in [(0, 0, 25), (100, -50, 10)]:
    cases.append(("strict-3x3", strict_pat, "1", [strict_pat, "1"], startx, startz, n, (-300, 300, -300, 300)))
# 密度
for (startx, startz, n) in [(0, 0, 30), (-20, 30, 15)]:
    cases.append(("density-g5-0.3", None, "2", ["5", "0.3"], startx, startz, n, (-400, 400, -400, 400)))
for (startx, startz, n) in [(7, 7, 8)]:
    cases.append(("density-g3-0.5", None, "2", ["3", "0.5"], startx, startz, n, (-200, 200, -200, 200)))
# v3.6.16：最小密度 = 1.0（窗口必须全是史莱姆区块）—— 引擎新加的 AND 快路必须与参考实现一致
for (startx, startz, n) in [(0, 0, 10), (-33, 21, 6)]:
    cases.append(("density-g3-1.0", None, "2", ["3", "1.0"], startx, startz, n, (-160, 160, -160, 160)))
for (startx, startz, n) in [(5, -5, 8)]:
    cases.append(("density-g2-1.0", None, "2", ["2", "1.0"], startx, startz, n, (-120, 120, -120, 120)))

for (name, pf, mode, modeargs, startx, startz, n, region) in cases:
    (minx, maxx, minz, maxz) = region
    seed = 0
    with_density = (mode == "2")
    base = ["0", "0", str(minx), str(maxx), str(minz), str(maxz), mode] + modeargs
    ref_text = run_ref(base)
    ref_items = python_gui_topn(ref_text, with_density, startx, startz, n)
    our_text = run_our(["--near", "0", str(seed), str(minx), str(maxx), str(minz), str(maxz), mode] + modeargs +
                       [str(startx), str(startz), str(n)])
    ok, detail = cmp_near_lines(ref_items, our_text)
    report(ok, f"GUI序[{name}] start=({startx},{startz}) N={n} region=({minx},{maxx})", detail)
    # 智能范围 + 提前停止（引擎保证 = 全局最近 N）
    our_smart = run_our(["--near", "0", str(seed), str(minx), str(maxx), str(minz), str(maxz), mode] + modeargs +
                        [str(startx), str(startz), str(n), "--smart", "--stop"])
    ok, detail = cmp_near_lines(ref_items[:n], our_smart)
    report(ok, f"GUI序[{name}] 智能+提前停止 start=({startx},{startz}) N={n}", detail)

os.unlink(strict_pat)
os.unlink(pf22)

print(f"\n===== 汇总：通过 {PASS} 项，失败 {FAIL} 项 =====")
sys.exit(1 if FAIL else 0)
