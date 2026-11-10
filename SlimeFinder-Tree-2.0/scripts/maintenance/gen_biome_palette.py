"""把 cubiomes 的群系枚举转成 C++ 调色板表（按名字推定颜色，接近 chunkbase 观感）。
输出：<新树>/src/core/BiomePalette.inc —— 形如
    { 0, "ocean", 0x4F7FD0 },
可被 BiomeMap.cpp 直接 #include。
"""
import io
import os
import re

SRC = r'C:\Tools\cubiomes\cubiomes-master\biomes.h'
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
OUT = os.path.join(ROOT, 'src', 'core', 'BiomePalette.inc')

txt = io.open(SRC, encoding='utf-8').read()
start = txt.index('enum BiomeID')
end = txt.index('};', start)
body = txt[start:end]

entries = []   # (id, name)
next_id = None
for raw in body.split('\n'):
    line = raw.split('//')[0].strip().rstrip(',')
    if not line or '=' in line and 'enum' not in line and line.startswith('enum'):
        continue
    # 形如 `plains,` 或 `mountains,  extremeHills = mountains,`
    for part in line.split(','):
        part = part.strip()
        if not part or part.startswith('enum ') or part == '{':
            continue
        m = re.match(r'^([A-Za-z_][A-Za-z0-9_]*)\s*(?:=\s*(-?\d+|[A-Za-z_][A-Za-z0-9_]*))?$', part)
        if not m:
            continue
        name, val = m.group(1), m.group(2)
        if val is None:
            if next_id is None:
                continue
            entries.append((next_id, name))
            next_id += 1
        elif re.fullmatch(r'-?\d+', val):
            next_id = int(val) + 1
            entries.append((int(val), name))
        else:
            # 别名：沿用被引用名字的 id
            ref = next((i for i, n in entries if n == val), None)
            if ref is not None:
                next_id = ref + 1

# 去重（同 id 只留第一个名字）
seen = {}
for i, n in entries:
    if i >= 0 and i not in seen:
        seen[i] = n

# 颜色规则：按名字里的关键词推定（越靠后越优先匹配）
RULES = [
    ('deep_dark', 0x2B2B33), ('lush_caves', 0x3F7A4E), ('dripstone_caves', 0x6E5B4B),
    ('mushroom', 0xB98CB8), ('nether_wastes', 0x7A2B2B), ('the_end', 0xD9D6B8),
    ('soul_sand', 0x5B4A44), ('crimson', 0x8E2F45), ('warped', 0x3E7A6E),
    ('basalt', 0x55555E), ('deep_ocean', 0x1B3E7A), ('frozen_ocean', 0x4E7C9E),
    ('warm_ocean', 0x2E86B8), ('lukewarm_ocean', 0x2F73A8), ('cold_ocean', 0x3A6E96),
    ('ocean', 0x3D6FBE), ('river', 0x4C7FD0), ('beach', 0xE4DC9A),
    ('snowy', 0xEFF4F8), ('frozen', 0xDCE6EE), ('ice', 0xD7E6F0),
    ('desert', 0xE0C878), ('badlands', 0xC06A3E), ('savanna', 0xB9A64B),
    ('jungle', 0x1F7A3D), ('swamp', 0x4B6B3A), ('mangrove', 0x4E7A4E),
    ('taiga', 0x2F6B4F), ('grove', 0x54806A), ('meadow', 0x8FC46A),
    ('plains', 0x8DB360), ('sunflower', 0x9CC46A), ('flower_forest', 0x86B85C),
    ('birch', 0x7FAE63), ('forest', 0x2F7A44), ('wooded', 0x35704A),
    ('dark_forest', 0x2A5A34), ('cherry', 0xE8A8C8), ('pale_garden', 0x8A8F8A),
    ('windswept', 0x9AA37A), ('mountain', 0x8A8A8A), ('stony', 0x777777),
    ('stone', 0x6F6F6F), ('peak', 0xB9B9B9), ('hill', 0x7A9A5A),
    ('stony_shore', 0x7C7C7C), ('the_void', 0x101014),
]

def color_for(name: str) -> int:
    for key, rgb in RULES:
        if key in name:
            return rgb
    return 0x9A9A9A

lines = ['// 自动生成：cubiomes biomes.h 的 BiomeID → 颜色（按名字关键词推定，接近 chunkbase 观感）',
         '// 重新生成脚本：scripts/gen_biome_palette.py（见仓库 docs/farm_siting_progress.md）',
         'static const BiomeColorEntry kBiomePalette[] = {']
for i in sorted(seen):
    n = seen[i]
    lines.append('    { %d, "%s", 0x%06X },' % (i, n, color_for(n)))
lines.append('};')
lines.append('static const int kBiomePaletteSize = (int)(sizeof(kBiomePalette) / sizeof(kBiomePalette[0]));')
io.open(OUT, 'w', encoding='utf-8', newline='\n').write('\n'.join(lines) + '\n')
print('生成 %s：%d 个群系' % (os.path.basename(OUT), len(seen)))
