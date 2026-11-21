#!/usr/bin/env python3
"""Offline evaluator for SlimeFinder ring candidate pools.

Candidate pool CSV schema:
  seed,cx,cz,blocks,weight,pct
Coordinates are block coordinates of the candidate center.

The evaluator deliberately does not generate candidates. It only measures
ranking quality of an existing pool, so new candidate-scoring ideas can be
compared without another full-map run.
"""

from __future__ import annotations

import argparse
import csv
import json
import math
from pathlib import Path


def load_pool(path: Path) -> list[dict]:
    rows: list[dict] = []
    with path.open("r", encoding="utf-8", newline="") as f:
        reader = csv.DictReader(f)
        required = {"seed", "cx", "cz", "blocks", "weight", "pct"}
        if not required.issubset(reader.fieldnames or []):
            raise SystemExit(f"{path}: expected columns {sorted(required)}")
        for row in reader:
            rows.append(
                {
                    "seed": int(row["seed"]),
                    "cx": int(row["cx"]),
                    "cz": int(row["cz"]),
                    "blocks": int(row["blocks"]),
                    "weight": float(row["weight"]),
                    "pct": float(row["pct"]),
                }
            )
    return rows


def rank_of_reference(rows: list[dict], ref_x: int, ref_z: int, radius: int) -> int | None:
    """Rank by weight (1-based), matching reference within radius blocks."""
    ordered = sorted(rows, key=lambda r: r["weight"], reverse=True)
    for i, row in enumerate(ordered, 1):
        if math.hypot(row["cx"] - ref_x, row["cz"] - ref_z) <= radius:
            return i
    return None


def evaluate(path: Path, ref: tuple[int, int] | None, radius: int, ks: list[int]) -> dict:
    rows = load_pool(path)
    if not rows:
        return {"pool": str(path), "rows": 0, "error": "empty"}

    ordered = sorted(rows, key=lambda r: r["weight"], reverse=True)
    out: dict = {
        "pool": str(path),
        "rows": len(rows),
        "best": {
            "cx": ordered[0]["cx"],
            "cz": ordered[0]["cz"],
            "blocks": ordered[0]["blocks"],
            "weight": ordered[0]["weight"],
            "pct": ordered[0]["pct"],
        },
        "topk": {},
    }
    if ref is not None:
        out["reference"] = {"cx": ref[0], "cz": ref[1], "radius": radius}
        out["reference_rank"] = rank_of_reference(rows, ref[0], ref[1], radius)
    for k in ks:
        take = ordered[: min(k, len(ordered))]
        out["topk"][str(k)] = {
            "best_weight": take[0]["weight"] if take else None,
            "best_blocks": take[0]["blocks"] if take else None,
            # top-k prefix contains only the first k rows; best is take[0].
            "prefix_best_weight": take[0]["weight"] if take else None,
            "prefix_best_blocks": take[0]["blocks"] if take else None,
            "has_reference": rank_of_reference(take, ref[0], ref[1], radius) is not None if ref else None,
        }
    return out


def load_manifest(path: Path) -> dict[str, dict]:
    """Map resolved pool path -> seed row with reference metadata."""
    refs: dict[str, dict] = {}
    with path.open("r", encoding="utf-8", newline="") as f:
        reader = csv.DictReader(f)
        for row in reader:
            pool = (path.parent / row["pool_file"]).resolve()
            refs[str(pool)] = {
                "seed": int(row["seed"]),
                "ref": (int(row["ref_cx"]), int(row["ref_cz"])),
                "blocks": int(row["ref_blocks"]),
                "weight": float(row["ref_weight"]),
                "status": row.get("status", ""),
            }
    return refs


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("pools", nargs="*", type=Path)
    ap.add_argument("--manifest", type=Path, help="seeds.csv; uses each pool's own reference")
    ap.add_argument("--ref", help="reference block coordinate dx,dz")
    ap.add_argument("--radius", type=int, default=16)
    ap.add_argument("--ks", default="1,10,100,1000")
    ap.add_argument("--json", action="store_true")
    args = ap.parse_args()

    global_ref = None
    if args.ref:
        parts = args.ref.split(",")
        if len(parts) != 2:
            raise SystemExit("--ref expects x,z")
        global_ref = (int(parts[0]), int(parts[1]))

    refs: dict[str, dict] = {}
    if args.manifest:
        refs = load_manifest(args.manifest)
        if not args.pools:
            args.pools = [Path(p) for p in refs.keys()]

    ks = [int(x) for x in args.ks.split(",") if x.strip()]
    results = []
    for p in args.pools:
        meta = refs.get(str(p.resolve()))
        ref = meta["ref"] if meta else global_ref
        result = evaluate(p, ref, args.radius, ks)
        if meta:
            result["seed"] = meta["seed"]
            result["reference_status"] = meta["status"]
        results.append(result)
    if args.json:
        print(json.dumps(results, ensure_ascii=False, indent=2))
    else:
        for r in results:
            if r.get("error"):
                print(f"{r['pool']}: ERROR {r['error']}")
                continue
            b = r["best"]
            print(
                f"{r['pool']}: seed={r.get('seed')} rows={r['rows']} best=({b['cx']},{b['cz']}) "
                f"blocks={b['blocks']} weight={b['weight']:.3f} pct={b['pct']:.6f} "
                f"ref_rank={r.get('reference_rank')}"
            )
            for k, v in r["topk"].items():
                print(
                    f"  top{k}: prefix_best_weight={v['prefix_best_weight']:.3f} "
                    f"prefix_best_blocks={v['prefix_best_blocks']} has_ref={v['has_reference']}"
                )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
