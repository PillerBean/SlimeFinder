#!/usr/bin/env python3
"""把官方缓存按快捷范围拆成多个网盘 zip（中文名，带序列号）。

命名：基岩版区块缓存-<范围>-<序列号>.zip
范围按 pack 离原点的最大轴向距离分类：
  nearby / local / large / all
每个 zip 内含 manifest.json、local/*.slmz、README.txt。
"""
import argparse
import json
import os
import shutil
import zipfile

RANGE_LABELS = {
    'nearby': '附近',
    'local': '局部',
    'large': '较大',
    'all': '全部',
}


def pack_abs(p):
    return max(abs(p['minX']), abs(p['maxX']), abs(p['minZ']), abs(p['maxZ']))


def in_range(p, limit):
    return pack_abs(p) <= limit


def write_zip(out_path, subset, manifest, source_root, label, serial, limits):
    os.makedirs(os.path.dirname(out_path), exist_ok=True)
    tmp = out_path + '.tmpdir'
    shutil.rmtree(tmp, ignore_errors=True)
    os.makedirs(os.path.join(tmp, 'local'), exist_ok=True)
    sub_manifest = dict(manifest)
    sub_manifest['packs'] = subset
    sub_manifest['totalPacks'] = len(subset)
    bounds = {
        'minX': min(p['minX'] for p in subset),
        'maxX': max(p['maxX'] for p in subset),
        'minZ': min(p['minZ'] for p in subset),
        'maxZ': max(p['maxZ'] for p in subset),
    }
    batch = {
        'range': label,
        'rangeLabel': RANGE_LABELS.get(label, label),
        'serial': serial,
        'packCount': len(subset),
        'rangeLimitChunks': limits,
        **bounds,
    }
    with open(os.path.join(tmp, 'batch.json'), 'w', encoding='utf-8', newline='\n') as f:
        json.dump(batch, f, ensure_ascii=False, indent=2)
        f.write('\n')
    with open(os.path.join(tmp, 'manifest.json'), 'w', encoding='utf-8', newline='\n') as f:
        json.dump(sub_manifest, f, ensure_ascii=False, indent=2)
        f.write('\n')
    for p in subset:
        src = os.path.join(source_root, p['file'])
        dst = os.path.join(tmp, p['file'])
        if os.path.isfile(src):
            shutil.copy2(src, dst)
        else:
            # 允许从 full manifest 的 local 路径映射
            alt = os.path.join(source_root, 'local', os.path.basename(p['file']))
            if os.path.isfile(alt):
                shutil.copy2(alt, dst)
            else:
                raise FileNotFoundError(src)
    with open(os.path.join(tmp, 'README.txt'), 'w', encoding='utf-8', newline='\n') as f:
        f.write(f'SlimeFinder v3.12.3 官方缓存\n\n范围：{RANGE_LABELS.get(label, label)}\n'
                f'序列号：{serial}\n包数：{len(subset)}\n'
                f'覆盖：X {bounds["minX"]}..{bounds["maxX"]} / Z {bounds["minZ"]}..{bounds["maxZ"]}\n\n'
                '解压后把 local 目录合并到程序目录的 cache/local；\n'
                '程序启动时会自动扫描并去重，不需要手工改 index.json。\n'
                '网盘地址：https://wwben.lanzoul.com/b01d7e4lre\n')
    with zipfile.ZipFile(out_path, 'w', compression=zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for root, _, files in os.walk(tmp):
            for name in files:
                path = os.path.join(root, name)
                z.write(path, os.path.relpath(path, tmp))
    shutil.rmtree(tmp, ignore_errors=True)
    return os.path.getsize(out_path)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--manifest', default='cache-official/manifest.json')
    ap.add_argument('--source-root', default='cache-official')
    ap.add_argument('--out-dir', default='dist/netdisk-batches')
    ap.add_argument('--nearby', type=int, default=2048)
    ap.add_argument('--local', type=int, default=8192)
    ap.add_argument('--large', type=int, default=32768)
    ap.add_argument('--all', type=int, default=131072)
    ap.add_argument('--labels', default='nearby,local,large,all')
    ap.add_argument('--batch-packs', type=int, default=16)
    ap.add_argument('--max-bytes', type=int, default=100 * 1024 * 1024)
    args = ap.parse_args()

    with open(args.manifest, encoding='utf-8') as f:
        manifest = json.load(f)
    limits = {'nearby': args.nearby, 'local': args.local, 'large': args.large, 'all': args.all}
    wanted = [x.strip() for x in args.labels.split(',') if x.strip()]
    packs = sorted(manifest.get('packs', []), key=lambda p: (p['minZ'], p['minX']))
    shutil.rmtree(args.out_dir, ignore_errors=True)
    os.makedirs(args.out_dir, exist_ok=True)
    written = []
    for label in wanted:
        if label not in RANGE_LABELS:
            continue
        subset = [p for p in packs if in_range(p, limits[label])]
        if not subset:
            continue
        batches = [subset[i:i + args.batch_packs] for i in range(0, len(subset), args.batch_packs)]
        for i, batch in enumerate(batches, 1):
            name = f'基岩版区块缓存-{RANGE_LABELS[label]}-{i}.zip'
            out = os.path.join(args.out_dir, name)
            size = write_zip(out, batch, manifest, args.source_root, label, i, limits[label])
            if size > args.max_bytes:
                print('WARN: over max size', out, size)
            written.append((name, len(batch), size))
            print(f'{name}  packs={len(batch)}  bytes={size}')
    print(f'done: {len(written)} zip files -> {args.out_dir}')


if __name__ == '__main__':
    main()
