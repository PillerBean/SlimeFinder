#!/usr/bin/env python3
"""性能回归基线：跑 slime_tests --bench，提取关键指标并与基线比较。

用法：
    py -3 scripts/perf_baseline.py            # 只跑一次，打印当前指标
    py -3 scripts/perf_baseline.py --save     # 把当前指标写进基线文件
    py -3 scripts/perf_baseline.py --check    # 与基线比较，超过容差则以非零码退出

基线文件：build/perf_baseline.json（不随仓库发布，避免把某台机器的数字当标准答案）
设计说明：
  * 只取「扫描/匹配」这类稳定指标，跳过含首次 GPU 初始化的数字；
  * 默认容差 30%（本机实测噪声 ±10~20%，匹配类指标抖动更大）；
  * 全部指标都是「越小越好」，比较时按百分比放宽。
"""
import argparse
import json
import os
import re
import subprocess
import sys

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
EXE = os.path.join(REPO, "build", "slime_tests.exe")
BASELINE = os.path.join(REPO, "build", "perf_baseline.json")

# 需要跟踪的行 → (正则, 指标名)
PATTERNS = [
    (r"Java 10000x10000 单核\s+区块.*?扫描\s+([\d.]+) ms", "java_scan_1t_ms"),
    (r"Java 10000x10000 多核\s+区块.*?扫描\s+([\d.]+) ms", "java_scan_nt_ms"),
    (r"Java 10000x10000 多核\s+区块.*?合计\s+([\d.]+) ms", "java_total_nt_ms"),
    (r"基岩 2000x2000 单核\s+区块.*?扫描\s+([\d.]+) ms", "bedrock_scan_1t_ms"),
    (r"基岩 2000x2000 多核\s+区块.*?扫描\s+([\d.]+) ms", "bedrock_scan_nt_ms"),
    (r"密度 g=5 thr=0\.3 2000x2000 多核 区块.*?匹配\s+([\d.]+) ms", "density_match_nt_ms"),
    (r"严格 2x2全绿 2000x2000 多核.*?匹配\s+([\d.]+) ms", "strict_match_nt_ms"),
]


def run_bench():
    if not os.path.exists(EXE):
        sys.exit("找不到 %s（先构建）" % EXE)
    out = subprocess.run([EXE, "--bench"], cwd=REPO, capture_output=True)
    # Windows 控制台默认 GBK；两种编码都试
    for enc in ("utf-8", "gbk"):
        try:
            return out.stdout.decode(enc)
        except UnicodeDecodeError:
            continue
    return out.stdout.decode("utf-8", "replace")


def collect(text):
    metrics = {}
    for line in text.splitlines():
        for pat, name in PATTERNS:
            m = re.search(pat, line)
            if m:
                metrics[name] = round(float(m.group(1)), 2)
    return metrics


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--save", action="store_true", help="保存为基线")
    ap.add_argument("--check", action="store_true", help="与基线比较")
    ap.add_argument("--tolerance", type=float, default=30.0, help="允许变慢的百分比（默认 30，匹配类指标噪声较大）")
    args = ap.parse_args()

    metrics = collect(run_bench())
    if not metrics:
        sys.exit("没有解析到任何指标（bench 输出格式变了？）")

    print("当前指标：")
    for k, v in metrics.items():
        print("  %-22s %8.2f ms" % (k, v))

    if args.save:
        os.makedirs(os.path.dirname(BASELINE), exist_ok=True)
        with open(BASELINE, "w", encoding="utf-8") as f:
            json.dump(metrics, f, ensure_ascii=False, indent=2)
            f.write("\n")
        print("已保存基线：%s" % BASELINE)
        return 0

    if args.check:
        if not os.path.exists(BASELINE):
            sys.exit("没有基线文件，先跑 --save（当前数字会直接成为基线）")
        with open(BASELINE, "r", encoding="utf-8") as f:
            base = json.load(f)
        bad = []
        for k, v in metrics.items():
            if k not in base:
                continue
            limit = base[k] * (1.0 + args.tolerance / 100.0)
            if v > limit:
                bad.append((k, base[k], v))
        if bad:
            print("性能回归（超过 %.0f%% 容差）：" % args.tolerance)
            for k, b, v in bad:
                print("  %-22s 基线 %.2f → 现在 %.2f ms" % (k, b, v))
            return 1
        print("对比基线通过（容差 %.0f%%）" % args.tolerance)
    return 0


if __name__ == "__main__":
    sys.exit(main())
