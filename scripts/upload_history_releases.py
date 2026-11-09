#!/usr/bin/env python3
"""批量把 dist/ 下的历史版本发布到 GitHub Releases。

- 每个版本一个 Release（zip + setup 同页）；
- 正式版：不勾 Pre-release；测试版：勾 Pre-release；
- 历史版本没有源码，tag 使用 v<版本>-archive（指向当前归档提交），避免冒充历史源码 tag；
- 已有 Release 自动跳过；失败重试；状态保存在 release_upload_state.json。

用法:
  py -3 scripts/upload_history_releases.py --dry-run
  py -3 scripts/upload_history_releases.py --workers 3
  py -3 scripts/upload_history_releases.py --only 3.2.1
"""
import argparse
import concurrent.futures
import hashlib
import io
import json
import os
import re
import subprocess
import sys
import threading
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DIST = os.path.join(ROOT, "dist")
HISTORY = os.path.join(ROOT, "docs", "update_history.md")
STATE = os.path.join(ROOT, "release_upload_state.json")
GH = os.environ.get("GH_BIN") or os.path.join(
    os.path.dirname(ROOT), "github_tools", "gh_dir", "bin", "gh.exe")
CURRENT_RELEASE_TAG = "v3.10.12.9"   # 已单独发布，不重复创建

_lock = threading.Lock()


def natural_key(v):
    return [(0, int(x)) if x.isdigit() else (1, x) for x in re.split(r"[.\-]", v)]


def collect_versions():
    zips, setups, sums = {}, {}, {}
    for f in os.listdir(DIST):
        path = os.path.join(DIST, f)
        m = re.fullmatch(r"SlimeFinder-(.+)-win64\.zip", f)
        if m:
            zips[m.group(1)] = path
            continue
        m = re.fullmatch(r"史莱姆区块查找器V(.+)\.zip", f)
        if m:
            zips.setdefault(m.group(1), path)
            continue
        m = re.fullmatch(r"SlimeFinder-(.+)-Setup\.exe", f)
        if m:
            setups[m.group(1)] = path
            continue
        m = re.fullmatch(r"SHA256SUMS-(.+)\.txt", f)
        if m:
            sums[m.group(1)] = path
    return zips, setups, sums


def official_set():
    src = os.path.join(ROOT, "src", "gui", "UpdateHistory.cpp")
    raw = io.open(src, encoding="utf-8", newline="").read().replace("\r\n", "\n")
    start = raw.find("const RawEntry kRawEntries[] = {")
    end = raw.find("\n};", start)
    body = raw[start:end]
    out = set()
    for m in re.finditer(r'\{ "([^"]+)", "([^"]+)", (true|false),', body):
        if m.group(3) == "true":
            out.add(m.group(1))
    return out


def notes_map():
    text = io.open(HISTORY, encoding="utf-8", newline="").read().replace("\r\n", "\n")
    out = {}
    parts = re.split(r"^## V([^\n]+)\n", text, flags=re.M)
    # parts: [head, version1, body1, version2, body2, ...]
    for i in range(1, len(parts) - 1, 2):
        head, body = parts[i], parts[i + 1]
        ver = head.split(" · ")[0].strip()
        out[ver] = body.strip()
    return out


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as fp:
        for chunk in iter(lambda: fp.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def load_state():
    if os.path.exists(STATE):
        try:
            return json.load(io.open(STATE, encoding="utf-8"))
        except Exception:
            pass
    return {"done": {}, "failed": {}}


def save_state(st):
    with _lock:
        io.open(STATE, "w", encoding="utf-8", newline="").write(
            json.dumps(st, ensure_ascii=False, indent=2) + "\n")


def make_assets(ver, zips, setups, sums):
    assets = []
    if ver in zips:
        assets.append(zips[ver])
    if ver in setups:
        assets.append(setups[ver])
    return assets, (sums.get(ver) if ver in sums else None)


def ensure_sums(ver, assets, sums, dry):
    """没有现成 SHA256SUMS 时，为本次资产生成一个。"""
    if ver in sums:
        return sums[ver]
    outdir = os.path.join(ROOT, "release", ver)
    if not dry:
        os.makedirs(outdir, exist_ok=True)
    path = os.path.join(outdir, "SHA256SUMS-" + ver + ".txt")
    if dry:
        return path
    lines = ["# SlimeFinder v%s - SHA256 checksums" % ver]
    for a in assets:
        lines.append("SHA256  %s   = %s" % (os.path.basename(a), sha256(a).upper()))
    io.open(path, "w", encoding="utf-8", newline="\n").write("\n".join(lines) + "\n")
    return path


def write_notes(ver, official, body, dry):
    outdir = os.path.join(ROOT, "release", ver)
    if not dry:
        os.makedirs(outdir, exist_ok=True)
    path = os.path.join(outdir, "release-notes.md")
    if dry:
        return path
    head = [
        "## SlimeFinder v%s" % ver,
        "",
        "> 历史版本归档：仅提供二进制安装包/便携包；该版本源码未保留，tag 指向归档提交。",
    ]
    if official:
        head.insert(1, "**正式版（历史归档）**")
    else:
        head.insert(1, "测试版（历史归档）")
    head.append("")
    head.append(body if body else "（该版本没有可用的更新历史条目。）")
    io.open(path, "w", encoding="utf-8", newline="\n").write("\n".join(head) + "\n")
    return path


def gh(args, timeout=1800):
    env = dict(os.environ)
    env["PATH"] = os.path.dirname(GH) + os.pathsep + env.get("PATH", "")
    p = subprocess.run([GH] + args, cwd=ROOT, env=env, capture_output=True, text=True,
                       encoding="utf-8", errors="replace", timeout=timeout)
    return p


def release_exists(tag):
    p = gh(["release", "view", tag, "--json", "isDraft,assets"], timeout=120)
    return p.returncode == 0, p


def upload_one(ver, tag, title, notes, assets, official, dry):
    if dry:
        return "dry", ""
    for attempt in range(1, 4):
        args = ["release", "create", tag]
        args += assets
        args += ["--title", title, "--notes-file", notes, "--target", "main"]
        if not official:
            args.append("--prerelease")
        p = gh(args)
        if p.returncode == 0:
            return "ok", p.stdout.strip()
        # 失败时清理可能留下的草稿 release，再重试
        g = gh(["release", "delete", tag, "--yes"], timeout=180)
        msg = (p.stderr or p.stdout).strip().replace("\n", " ")[:300]
        if attempt < 3:
            time.sleep(5 * attempt)
            continue
        return "fail", msg
    return "fail", "unknown"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--workers", type=int, default=3)
    ap.add_argument("--only", default="")
    ap.add_argument("--limit", type=int, default=0)
    args = ap.parse_args()

    zips, setups, sums = collect_versions()
    officials = official_set()
    notes = notes_map()
    state = load_state()
    versions = sorted(set(zips), key=natural_key)
    if args.only:
        versions = [v for v in versions if v == args.only]
    if args.limit > 0:
        versions = versions[:args.limit]

    plan = []
    for v in versions:
        assets, sumfile = make_assets(v, zips, setups, sums)
        if v == "3.10.12.9":
            continue  # 已单独发布；由外部 edit 标记为测试版
        if state["done"].get(tag_of(v)):
            continue
        plan.append(v)

    # 官方版本最后上传，保证 Latest 指向最新官方版
    plan_tests = [v for v in plan if v not in officials]
    plan_official = [v for v in plan if v in officials]
    plan = plan_tests + plan_official

    print("versions in dist:", len(versions))
    print("official in dist:", sorted([v for v in officials if v in zips], key=natural_key))
    print("plan:", len(plan), "releases; workers:", args.workers,
          "; dry-run:", args.dry_run)
    missing_official = sorted([v for v in officials if v not in zips], key=natural_key)
    if missing_official:
        print("official without local package (will be skipped):", missing_official)

    total_bytes = 0
    for v in plan:
        assets, _ = make_assets(v, zips, setups, sums)
        total_bytes += sum(os.path.getsize(a) for a in assets)
    print("planned bytes: %.2f GB" % (total_bytes / (1 << 30)))

    if args.dry_run:
        for v in plan:
            assets, sumfile = make_assets(v, zips, setups, sums)
            kind = "正式" if v in officials else "测试"
            print("  %-16s %-3s %d asset(s): %s" % (
                v, kind, len(assets) + 1,
                ", ".join(os.path.basename(a) for a in assets)))
        return 0

    def task(v):
        tag = tag_of(v)
        # 远端已存在则跳过（上次中断/测试上传可能已建好 release）
        exists, _p = release_exists(tag)
        if exists:
            with _lock:
                state["done"][tag] = {"version": v, "official": v in officials, "skipped": True}
                save_state(state)
            print("[skip] %-16s already exists" % v)
            return "skip"
        assets, sumfile = make_assets(v, zips, setups, sums)
        sumfile = ensure_sums(v, assets, sums, False)
        assets = assets + [sumfile]
        tag = tag_of(v)
        title = "SlimeFinder v%s（历史归档）" % v
        note = write_notes(v, v in officials, notes.get(v, ""), False)
        t0 = time.time()
        ok, detail = upload_one(v, tag, title, note, assets, v in officials, False)
        dt = time.time() - t0
        with _lock:
            if ok == "ok":
                state["done"][tag] = {"version": v, "official": v in officials,
                                      "seconds": round(dt, 1)}
                print("[ok] %-16s %-3s %.1fs %s" % (v, "正式" if v in officials else "测试",
                                                     dt, detail[:120]))
            else:
                state["failed"][tag] = {"version": v, "error": detail}
                print("[fail] %-16s %-3s %.1fs %s" % (v, "正式" if v in officials else "测试",
                                                       dt, detail))
            save_state(state)
        return ok

    t0 = time.time()
    okc = failc = skipc = 0
    with concurrent.futures.ThreadPoolExecutor(max_workers=max(1, args.workers)) as ex:
        futs = {ex.submit(task, v): v for v in plan}
        for fut in concurrent.futures.as_completed(futs):
            r = fut.result()
            if r == "ok":
                okc += 1
            elif r == "skip":
                skipc += 1
            else:
                failc += 1
    print("done: ok=%d skip=%d fail=%d total=%.1fs" % (okc, skipc, failc, time.time() - t0))
    if failc:
        print("failed entries in", STATE)
    return 0 if failc == 0 else 1


def tag_of(v):
    return "v%s-archive" % v


if __name__ == "__main__":
    sys.exit(main())
