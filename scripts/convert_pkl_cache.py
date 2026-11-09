#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
convert_pkl_cache.py - 将 Python v2.x 版本的 pickle 缓存转换为新版二进制 .slimecache 格式

旧格式（史莱姆区块查找.py v2.9.1 的 bedrock_cache_combined.pkl）：
    dict { 'ranges': [(minX,maxX,minZ,maxZ), ...], 'slime_set': {(x,z), ...} }
新格式（C++/Qt 版 SlimeFinder，v3.3.1 起扩展名统一为 .slimecache）：
    魔数 "SLME"(0x534C4D45) + 版本 1 + range 数(u32) + 条目数(u64)
    + i32×4×range + u64×条目（高 32 位 X，低 32 位 Z）

用法：
    python convert_pkl_cache.py 旧缓存.pkl [输出.slimecache]
    未指定输出时默认 旧缓存同名 .slimecache
    说明：文件内容与扩展名无关，程序同样能读取旧版 .cache 文件（向后兼容）。
"""
import struct
import sys

MAGIC = 0x534C4D45
VERSION = 1


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    src = sys.argv[1]
    dst = sys.argv[2] if len(sys.argv) > 2 else src.rsplit(".", 1)[0] + ".slimecache"

    import pickle
    with open(src, "rb") as f:
        data = pickle.load(f)

    if isinstance(data, dict) and "ranges" in data and "slime_set" in data:
        ranges = list(data["ranges"])
        slime = set(data["slime_set"])
    elif isinstance(data, dict) and "slime_set" in data:
        print("警告: 该文件无 ranges 覆盖信息，将以虚拟范围保存（导入后请自行全量补扫一次）")
        slime = set(data["slime_set"])
        if slime:
            xs = [p[0] for p in slime]
            zs = [p[1] for p in slime]
            ranges = [(min(xs), max(xs), min(zs), max(zs))]
        else:
            ranges = []
    else:
        print("错误: 无法识别的缓存结构", file=sys.stderr)
        return 1

    if not all(len(r) == 4 for r in ranges):
        print("错误: ranges 元素应为 4 元组 (minX,maxX,minZ,maxZ)", file=sys.stderr)
        return 1

    with open(dst, "wb") as f:
        f.write(struct.pack("<II", MAGIC, VERSION))
        f.write(struct.pack("<I", len(ranges)))
        f.write(struct.pack("<Q", len(slime)))
        for (a, b, c, d) in ranges:
            f.write(struct.pack("<iiii", a, b, c, d))
        for (x, z) in sorted(slime):
            f.write(struct.pack("<Q", ((x & 0xFFFFFFFF) << 32) | (z & 0xFFFFFFFF)))

    print(f"转换完成: {len(ranges)} 个区域 / {len(slime)} 个史莱姆区块 -> {dst}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
