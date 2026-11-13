#!/usr/bin/env python3
"""把 8192x8192 pack 目录重新打成交付到网盘的 zip（与旧命名一致）。"""
import argparse, hashlib, json, os, struct, zipfile
from datetime import datetime, timezone

H = struct.Struct('<8sIiiiiIIIIIQQ')
MAGIC = b'SLM3PKG\x00'

def pack_meta(path, rel):
    with open(path, 'rb') as f:
        h = f.read(H.size)
    if len(h) != H.size:
        raise ValueError('header incomplete')
    (magic, ver, minx, maxx, minz, maxz, cols, rows,
     bx, bz, bc, raw, comp) = H.unpack(h)
    if magic != MAGIC or ver != 3:
        raise ValueError('bad pack')
    sha = hashlib.sha256()
    with open(path, 'rb') as f:
        for c in iter(lambda: f.read(1024 * 1024), b''):
            sha.update(c)
    return {
        'file': rel, 'name': os.path.basename(path), 'url': rel,
        'sha256': sha.hexdigest(), 'bytes': os.path.getsize(path),
        'source': 'local', 'blockCols': cols, 'blockRows': rows,
        'blockCount': bc, 'rawBytes': raw,
        'minX': minx, 'maxX': maxx, 'minZ': minz, 'maxZ': maxz,
    }

def make_zip(out_path, packs, tier, label, serial, limit):
    batch = {
        'range': tier, 'rangeLabel': label, 'serial': serial,
        'packCount': len(packs), 'rangeLimitChunks': limit,
        'minX': min(p['minX'] for p in packs),
        'maxX': max(p['maxX'] for p in packs),
        'minZ': min(p['minZ'] for p in packs),
        'maxZ': max(p['maxZ'] for p in packs),
    }
    manifest = {
        'formatVersion': 3, 'manifestVersion': 1, 'appVersion': 'v3.12.3',
        'algorithmVersion': '1', 'edition': 'bedrock',
        'generatedAt': datetime.now(timezone.utc).isoformat(),
        'baseUrl': '', 'fallbackBaseUrl': '', 'manualFallbackUrl': '',
        'totalPacks': len(packs), 'packs': packs,
    }
    readme = ('史莱姆区块查找器 基岩版缓存包\n'
              f'范围：{label}\npack 数：{len(packs)}\n'
              '解压后可直接在客户端“导入网盘缓存”中选择本 zip。\n')
    os.makedirs(os.path.dirname(os.path.abspath(out_path)), exist_ok=True)
    with zipfile.ZipFile(out_path, 'w', compression=zipfile.ZIP_STORED, allowZip64=True) as z:
        z.writestr('batch.json', json.dumps(batch, ensure_ascii=False, indent=2))
        z.writestr('manifest.json', json.dumps(manifest, ensure_ascii=False, indent=2))
        z.writestr('README.txt', readme.encode('utf-8'))
        for p in packs:
            z.write(os.path.join(ROOT, tier, 'local', p['name']), p['file'])
    print(out_path, 'packs', len(packs), 'bytes', os.path.getsize(out_path))

def load_tier(tier):
    d = os.path.join(ROOT, tier, 'local')
    files = sorted(os.listdir(d)) if os.path.isdir(d) else []
    out = []
    for name in files:
        if not name.endswith('.slmz'):
            continue
        out.append(pack_meta(os.path.join(ROOT, tier, 'local', name), f'local/{name}'))
    return out

def main():
    global ROOT
    ap = argparse.ArgumentParser()
    ap.add_argument('--root', default='dist/netdisk-8192')
    ap.add_argument('--out', default='dist/netdisk-batches-8192')
    ap.add_argument('--per-zip', type=int, default=16)
    args = ap.parse_args()
    ROOT = args.root
    out_root = args.out
    os.makedirs(out_root, exist_ok=True)
    tiers = [('local', '局部', 8192), ('large', '较大', 32768), ('all', '全部', 131072)]
    for tier, label, limit in tiers:
        packs = load_tier(tier)
        packs.sort(key=lambda p: (p['minZ'], p['minX']))
        if not packs:
            print('skip empty', tier); continue
        serial = 0
        for i in range(0, len(packs), args.per_zip):
            serial += 1
            make_zip(os.path.join(out_root, f'基岩版区块缓存-{label}-{serial}.zip'),
                     packs[i:i + args.per_zip], tier, label, serial, limit)

if __name__ == '__main__':
    main()
