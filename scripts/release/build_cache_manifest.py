#!/usr/bin/env python3
"""生成官方缓存分发 manifest。

扫描 cache/local + cache/downloaded 下的 .slmz pack，读取固定 header 元数据，
计算 SHA256，输出 manifest JSON。默认只写本地文件，不上传。

用法：
  py -3 scripts/release/build_cache_manifest.py \
     --cache-root cache --out dist/cache-v3123/manifest.json \
     --base-url https://cache.example.com/v3.12.3 \
     --fallback-url "https://国内网盘占位/"

R2/网盘上传由 publish_cache.ps1 负责；本脚本不会联网。
"""
import argparse
import datetime
import hashlib
import json
import os
import struct
import sys

HEADER = struct.Struct('<8sIiiiiIIIIQQ')
MAGIC = b'SLM3PKG\x00'  # 8 bytes: S L M 3 P K G NUL


def parse_pack(path):
    with open(path, 'rb') as f:
        data = f.read(HEADER.size)
    if len(data) != HEADER.size:
        raise ValueError('header 不完整')
    (magic, version, min_x, max_x, min_z, max_z,
     cols, rows, blocks_x, blocks_z, block_count, raw_bytes, comp_bytes) = HEADER.unpack(data)
    if magic != MAGIC or version != 3:
        raise ValueError('不是 v3 pack')
    return {
        'formatVersion': 3,
        'minX': min_x, 'maxX': max_x, 'minZ': min_z, 'maxZ': max_z,
        'blockCols': cols, 'blockRows': rows,
        'blockCount': block_count,
        'rawBytes': raw_bytes,
        'bytes': comp_bytes,
    }


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b''):
            h.update(chunk)
    return h.hexdigest()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--cache-root', default='cache')
    ap.add_argument('--out', default='dist/cache/manifest.json')
    ap.add_argument('--base-url', default='')
    ap.add_argument('--fallback-url', default='')
    ap.add_argument('--app-version', default='v3.12.3')
    ap.add_argument('--algorithm-version', default='1')
    args = ap.parse_args()

    packs = []
    for source in ('local', 'downloaded'):
        root = os.path.join(args.cache_root, source)
        if not os.path.isdir(root):
            continue
        for name in sorted(os.listdir(root)):
            if not name.lower().endswith('.slmz'):
                continue
            path = os.path.join(root, name)
            if not os.path.isfile(path):
                continue
            try:
                meta = parse_pack(path)
            except ValueError as e:
                print('skip', path, e, file=sys.stderr)
                continue
            rel = source + '/' + name
            base = args.base_url.rstrip('/')
            url = base + '/' + rel if base else rel
            item = {
                'file': rel,
                'name': name,
                'url': url,
                'sha256': sha256_file(path),
                'bytes': os.path.getsize(path),
                'source': 'downloaded' if source == 'downloaded' else 'local',
                'blockCols': meta['blockCols'],
                'blockRows': meta['blockRows'],
                'blockCount': meta['blockCount'],
                'rawBytes': meta['rawBytes'],
                'minX': meta['minX'],
                'maxX': meta['maxX'],
                'minZ': meta['minZ'],
                'maxZ': meta['maxZ'],
            }
            packs.append(item)

    manifest = {
        'formatVersion': 3,
        'manifestVersion': 1,
        'appVersion': args.app_version,
        'algorithmVersion': args.algorithm_version,
        'edition': 'bedrock',
        'generatedAt': datetime.datetime.now(datetime.timezone.utc).isoformat(),
        'baseUrl': args.base_url,
        'fallbackBaseUrl': args.fallback_url,
        'totalPacks': len(packs),
        'packs': packs,
    }
    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
    with open(args.out, 'w', encoding='utf-8', newline='\n') as f:
        json.dump(manifest, f, ensure_ascii=False, indent=2)
        f.write('\n')
    print('manifest:', args.out)
    print('packs:', len(packs), 'bytes:', sum(p['bytes'] for p in packs))


if __name__ == '__main__':
    main()
