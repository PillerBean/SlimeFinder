// slime_kernel.cl - 史莱姆区块判定 OpenCL 内核（扫描用；v3.2.0 起）
//
// 与 CPU 引擎（src/core/SlimeEngine.cpp）逐位一致：
//   Java 版：LCG 判定（种子合成中的 32 位溢出/符号扩展、48 位 LCG 单步）
//   基岩版：MT19937 完整 init(624) + twist(624) + 首次加扰输出
//
// 全部使用无符号算术/显式位宽（OpenCL C 保证 uint/ulong 模回绕），
// 整数乘不依赖有符号溢出行为，保证跨品牌 GPU 结果一致。
//
// v3.3.1：GPU「密度匹配」内核（scan_*_bitmap / density_count_emit*）的开发已暂停，
//         相关代码（含未完成调试代码）移至 src/gpu/experimental/slime_kernel.cl.wip，
//         作为 v3.4.0 起点；本文件只保留扫描内核（scan_java_slime / scan_bedrock_slime）。
//
// 【重要实现注记 / 教训】Java 判定不要用「uint4 向量化 + 逐分量取用」的写法：实测 AMD
// OpenCL 编译器对 `uint4 z4 = ...; z4 * z4; (long)(int)zz.s0 ...` 这类组合会产生与标量
// 路径不一致的结果（v3.3.1 实验内核自检中 128 行里 127 行与引擎不符，标量路径 0 行不符）。
// 因此扫描路径一律使用已验证的标量 java_is_slime。

// ---------- Java 版辅助 ----------

// int32 符号扩展为 int64（C 语义：保值转换 → 算术右移等价扩展，全平台一致）
static inline long sext32(int v) { return (long)v; }

// 每行基座：worldSeed + se64(int32Mul(cz,cz))*4392871（64 位回绕）+ se64(int32Mul(cz,389711))
static inline ulong java_row_base(long worldSeed, int cz)
{
    uint czu = (uint)cz;
    uint t3u = czu * czu;                     // int32Mul(cz,cz) 低 32 位（无符号回绕）
    long t3s = sext32((int)t3u);              // 符号扩展
    uint t4u = czu * 389711u;                 // int32Mul(cz,389711)
    long t4s = sext32((int)t4u);
    ulong b = (ulong)worldSeed;
    b += (ulong)((ulong)t3s * 4392871UL);      // 64 位回绕乘法（无符号，位模式与 CPU 一致）
    b += (ulong)t4s;
    return b;
}

// x 尾项：se64(int32Mul(int32Mul(x,x),4987142)) + se64(int32Mul(x,5947611))
static inline ulong java_x_tail(int x)
{
    uint xu = (uint)x;
    uint t1u = (xu * xu) * 4987142u;          // (x*x) 与 *4987142 均取低 32 位
    uint t2u = xu * 5947611u;
    return (ulong)(long)(int)t1u + (ulong)(long)(int)t2u;
}

// v3.7.7：GPU 的整数除法/取模是模拟的（很贵），这里用魔数乘法判定 bits % 10 == 0
static inline int mod10_is_zero(uint bits)
{
    const uint q = (uint)(((ulong)bits * 0xCCCCCCCDu) >> 35);   // bits / 10（bits < 2^31）
    return (bits - q * 10u) == 0u ? 1 : 0;
}

// 判定：s ^= 987234911 → JavaRandom.next(31)（单步 LCG，31 位无符号 → 无拒绝循环）→ %10==0
// v3.7.7：48 位 LCG 乘法改用 32 位乘法拼装（GPU 的 64×64 乘法是模拟的，很慢），
//   并且把「行基座」（只与 cz 有关）提到每行算一次，避免每区块重复 64 位乘法。
static inline ulong java_lcg48(ulong x) // x < 2^48；返回 (x * MULT + 0xB) & (2^48-1)
{
    const uint kMlo = 0xDEECE66Du;   // MULT 低 32 位
    const uint kMhi = 5u;            // MULT 高 32 位
    const uint kMlo16 = 0xE66Du;     // MULT 低 16 位
    const uint lo = (uint)x;                     // 低 32 位
    const uint hi = (uint)(x >> 32) & 0xFFFFu;   // 高 16 位（x < 2^48）
    const ulong p1 = (ulong)lo * (ulong)kMlo;    // 32×32→64，单条 mad
    const uint cross = ((uint)(lo * kMhi) + (uint)(hi * kMlo16)) & 0xFFFFu;
    return (p1 + ((ulong)cross << 32) + 0xBUL) & 0xFFFFFFFFFFFFUL;
}

static inline int java_is_slime_from_base(ulong rowBase, int cx)
{
    ulong s = rowBase + java_x_tail(cx);
    s ^= 987234911UL;
    const ulong lcg = java_lcg48((s ^ 0x5DEECE66DUL) & 0xFFFFFFFFFFFFUL);
    const uint bits = (uint)(lcg >> 17);
    return mod10_is_zero(bits);
}

static inline int java_is_slime(long worldSeed, int cx, int cz)
{
    return java_is_slime_from_base(java_row_base(worldSeed, cz), cx);
}

// v3.8.3：尾项用差分递推（每区块 3 次加法替代 3 次乘法），状态拆成两个 uint32。
//   t1(x) = (x^2)*4987142 mod 2^32，t2(x) = x*5947611 mod 2^32，
//   t1(x+1)-t1(x) = (2x+1)*4987142（其差分恒为 2*4987142），t2(x+1)-t2(x) = 5947611。
static inline void java_tail_init(int x, uint* t1, uint* d, uint* t2)
{
    const uint xu = (uint)x;
    *t1 = (xu * xu) * 4987142u;
    *t2 = xu * 5947611u;
    *d = (2u * xu + 1u) * 4987142u;
}

static inline int java_hit_incr(uint baseLo, uint baseHi, uint t1, uint d, uint t2)
{
    (void)d;
    const uint lo = t1 + t2;
    const uint carry = (lo < t1) ? 1u : 0u;
    const int hiTail = (int)carry - (int)(t1 >> 31) - (int)(t2 >> 31);
    const uint sLo = baseLo + lo;
    const uint carry2 = (sLo < baseLo) ? 1u : 0u;
    uint lcgLo = sLo ^ 987234911u;
    uint lcgHi = baseHi + (uint)hiTail + carry2;
    lcgLo ^= 0xDEECE66Du;
    lcgHi = (lcgHi ^ 5u) & 0xFFFFu;
    const ulong prod = (ulong)lcgLo * (ulong)0xDEECE66Du;
    const uint cross = ((uint)(lcgLo * 5u) + (uint)(lcgHi * 0xE66Du)) & 0xFFFFu;
    const ulong r = (prod + ((ulong)cross << 32) + 0xBUL) & 0xFFFFFFFFFFFFUL;
    const uint bits = (uint)(r >> 17u);
    return mod10_is_zero(bits);
}

// 连续 32 个区块 → 一个 32 位掩码（状态在 t1/d/t2 中前进）
static inline uint java_hits_word32(uint baseLo, uint baseHi, uint* t1, uint* d, uint* t2)
{
    // v3.10.12.8 实测：4 路展开无收益（填充阶段受 int32 乘法吞吐限制），保持简单循环。
    uint bits = 0u;
    uint l1 = *t1, dd = *d, l2 = *t2;
    for (int b = 0; b < 32; ++b) {
        bits |= (uint)java_hit_incr(baseLo, baseHi, l1, dd, l2) << b;
        l1 += dd;
        dd += 2u * 4987142u;
        l2 += 5947611u;
    }
    *t1 = l1;
    *d = dd;
    *t2 = l2;
    return bits;
}

// ---------- 基岩版辅助 ----------

// MT19937：init(624) + twist(624) + 首次加扰（与 CPU 引擎逐位一致；保留作对照/回退）
static inline int bedrock_is_slime(int cx, int cz)
{
    uint seed = ((uint)cx * 0x1F1F1F1Fu) ^ (uint)cz;
    uint mt[624];
    mt[0] = seed;
    for (int i = 1; i < 624; ++i) {
        mt[i] = 1812433253u * (mt[i - 1] ^ (mt[i - 1] >> 30u)) + (uint)i;
    }
    // twist 三段（无取模版本，与 CPU 相同：i<227 读 mt[i+397]，之后读 mt[i-227]）
    for (int i = 0; i < 227; ++i) {
        uint y = (mt[i] & 0x80000000u) | (mt[i + 1] & 0x7FFFFFFFu);
        mt[i] = mt[i + 397] ^ (y >> 1u) ^ (0x9908B0DFu & (0u - (y & 1u)));
    }
    for (int i = 227; i < 623; ++i) {
        uint y = (mt[i] & 0x80000000u) | (mt[i + 1] & 0x7FFFFFFFu);
        mt[i] = mt[i - 227] ^ (y >> 1u) ^ (0x9908B0DFu & (0u - (y & 1u)));
    }
    {
        uint y = (mt[623] & 0x80000000u) | (mt[0] & 0x7FFFFFFFu);
        mt[623] = mt[396] ^ (y >> 1u) ^ (0x9908B0DFu & (0u - (y & 1u)));
    }
    uint v = mt[0];
    v ^= (v >> 11u);
    v ^= (v << 7u) & 0x9D2C5680u;
    v ^= (v << 15u) & 0xEFC60000u;
    v ^= (v >> 18u);
    return (v % 10u) == 0u ? 1 : 0;
}

// 基岩版代数等价捷径（证明同 src/core/FastMt.cpp 顶部）：
//   首次输出只依赖 twist 之后的 mt[0]，而 twist 第 0 次迭代写入的正是 mt[0]，它读取的
//   三个旧字 mt[0]/mt[1]/mt[397] 此时仍为 init 阶段的原始值，twist 其余迭代不参与首次输出
//   ⇒ 只需 init 递推到 mt[397] 即可，结果与完整 624+624 步逐位相同。
static inline int bedrock_is_slime_fast(int cx, int cz)
{
    uint m0 = ((uint)cx * 0x1F1F1F1Fu) ^ (uint)cz;
    uint m1 = 1812433253u * (m0 ^ (m0 >> 30u)) + 1u;
    uint cur = m1;
    for (uint i = 2u; i <= 397u; ++i) {
        cur = 1812433253u * (cur ^ (cur >> 30u)) + i;
    }
    uint y = (m0 & 0x80000000u) | (m1 & 0x7FFFFFFFu);
    uint v = cur ^ (y >> 1u) ^ (0x9908B0DFu & (0u - (y & 1u)));
    v ^= (v >> 11u);
    v ^= (v << 7u) & 0x9D2C5680u;
    v ^= (v << 15u) & 0xEFC60000u;
    v ^= (v >> 18u);
    return (v % 10u) == 0u ? 1 : 0;
}

// ---------- 扫描内核（字节输出，供 GpuSession::scanRect 使用） ----------

// 二维矩形区域扫描（行主序，out 尺寸 w*h）：gid -> (cx,cz)，x 连续方向为行
__kernel void scan_java_slime(long worldSeed, int minX, int minZ, int w, int h,
                              __global uchar* out)
{
    size_t idx = get_global_id(0);
    if (idx >= (size_t)w * (size_t)h) return;
    int cx = minX + (int)(idx % (size_t)w);
    int cz = minZ + (int)(idx / (size_t)w);
    out[idx] = (uchar)java_is_slime(worldSeed, cx, cz);
}

// v3.7.3：Java 版「一次 64 区块 → 一个 ulong」打包输出（回读量降到 1/8）
// v3.8.1（流程优化）：全 GPU 流式密度匹配 —— 判定 + 窗口统计都在 GPU 内完成，
//   只回传命中（坐标 + 窗口计数），不回传位图、CPU 不参与匹配。
//   结构：work-group 处理 TILE_W x TILE_H 区块，先用共享位图存本块判定结果，
//   再由组内线程按「锚点分区」求 g×g 窗口计数（g ≤ 32）。
#define DENS_TILE_W 256
#define DENS_TILE_H 64
// v3.10.12.2：锚点阶段改为「按列滑窗」——每个线程负责一列 x，只算两行的窗口 popcount，
//   窗口计数用列和递增/递减维护；旧版每个锚点要重算 g 行（g=8 时约 4 倍冗余）。
#define DENS_WORDS (DENS_TILE_W / 32)
#define DENS_SHARED (DENS_WORDS * DENS_TILE_H)
#define DENS_ANCHOR_SLIDE 1
static inline int dens_row_win_popcount(__local const uint* bits, int row, int x, int g, uint mask)
{
    const int word = x >> 5;
    const int shift = x & 31;
    if (shift + g <= 32) {
        return (int)popcount((bits[row * DENS_WORDS + word] >> shift) & mask);
    }
    const uint v = (bits[row * DENS_WORDS + word] >> shift) |
                   (bits[row * DENS_WORDS + word + 1] << (32 - shift));
    return (int)popcount(v & mask);
}

// v3.8.10：两遍模式 —— mode=0 只统计距离直方图（不写命中），mode=1 只回写 d2 <= cutoffD2 的命中。
__kernel void density_stream(long worldSeed, int minX, int minZ, int w, int h, int ozBase,
                             int gridSize, int thr, int axMin, int axMax, int azMin, int azMax,
                             int bedrock, uint maxHits,
                             __global int2* hits, __global uchar* counts, __global uint* counter,
                             int mode, int bucketShift, long start2x, long start2z, long cutoffD2,
                             __global uint* hist)
{
    __local uint bits[DENS_SHARED];
    // v3.10.12.1：块预留连续回写 —— 每 workgroup 局部暂存至多 256 条命中，
    //   循环末尾一次 atomic_add 预留连续区间并整块写出；超过 256 条的极端密度
    //   自动退回逐命中全局原子（不丢结果）。
    __local uint lhitX[256];
    __local uint lhitZ[256];
    __local uchar lhitC[256];
    __local uint lcount;
    __local uint lbase;
    __local int densColCnt[256];   // v3.10.12.2：每列当前 g 行窗口计数（strideX ≤ 256）
    const int g = gridSize;
    const int lid = (int)get_local_id(0);
    const int strideX = DENS_TILE_W - g + 1;
    const int strideZ = DENS_TILE_H - g + 1;
    const int tilesX = (w - g + strideX) / strideX;
    const int tileIdx = (int)get_group_id(0);
    const int tx = tileIdx % tilesX;
    const int tz = tileIdx / tilesX;
    const int ox = tx * strideX;
    const int oz = ozBase + tz * strideZ;   // 分带派发：ozBase = 本带起始行

    // ① 每个线程算 32 个连续区块（一个 32 位字）写入共享位图
    for (int wordIdx = lid; wordIdx < DENS_SHARED; wordIdx += (int)get_local_size(0)) {
        const int lz = wordIdx / DENS_WORDS;
        const int wx = (wordIdx % DENS_WORDS) * 32;
        const int cz = oz + lz;
        const int cx0 = minX + ox + wx;
        uint v = 0;
        if (cz < h && ox + wx < w) {
            if (bedrock) {
                for (int b = 0; b < 32; ++b) {
                    const int cx = ox + wx + b;
                    if (cx >= w) break;
                    v |= (uint)bedrock_is_slime_fast(minX + cx, minZ + cz) << b;
                }
            } else {
                const ulong rb = java_row_base(worldSeed, minZ + cz);
                const uint baseLo = (uint)rb, baseHi = (uint)(rb >> 32);
                uint t1 = 0, dd = 0, t2 = 0;
                java_tail_init(minX + ox + wx, &t1, &dd, &t2);   // v3.8.8 修正：必须用绝对 x（原来漏加 minX）
                v = java_hits_word32(baseLo, baseHi, &t1, &dd, &t2);
                if (ox + wx + 32 > w) {
                    const int valid = w - (ox + wx);
                    v &= (valid >= 32) ? 0xFFFFFFFFu : ((1u << valid) - 1u);
                }
            }
        }
        bits[wordIdx] = v;
    }
    if (lid == 0) { lcount = 0u; lbase = 0u; }
    barrier(CLK_LOCAL_MEM_FENCE);

    // ② 锚点分区（v3.10.12.2：按列滑窗，线程 = 列 x，窗口计数只维护列和）
#if DENS_ANCHOR_SLIDE
    {
        const uint mask = (g >= 32) ? 0xFFFFFFFFu : ((1u << g) - 1u);
        // 初始：每列 = 行 0..g-1 的窗口 popcount 之和
        if (lid < strideX) {
            int sum = 0;
            for (int r = 0; r < g; ++r) sum += dens_row_win_popcount(bits, r, lid, g, mask);
            densColCnt[lid] = sum;
        }
        for (int az = 0; az < strideZ; ++az) {
            if (lid < strideX) {
                const int cnt = densColCnt[lid];
                const int cx = minX + ox + lid;
                const int cz = minZ + oz + az;
                if (cx >= axMin && cx <= axMax && cz >= azMin && cz <= azMax) {
                    if (cnt >= thr) {
            if (mode == 0) {
                // 第一遍：照常回写全部命中（稀疏时直接可用），同时统计距离直方图
                if (hist) {
                    const long off2x = 2L * (long)cx + (long)(g - 1) - start2x;
                    const long off2z = 2L * (long)cz + (long)(g - 1) - start2z;
                    const long d2 = off2x * off2x + off2z * off2z;
                    long bucket = d2 >> bucketShift;
                    if (bucket > 1023L) bucket = 1023L;
                    atomic_inc(&hist[bucket]);
                }
                // v3.10.12.1：先写组内暂存；容量 256 满时才逐命中走全局原子
                const uint li = atomic_inc(&lcount);
                if (li < 256u) {
                    lhitX[li] = (uint)cx;
                    lhitZ[li] = (uint)cz;
                    lhitC[li] = (uchar)cnt;
                } else {
                    const uint slot = atomic_inc(counter);
                    if (slot < maxHits) {
                        hits[slot] = (int2)(cx, cz);
                        counts[slot] = (uchar)cnt;
                    }
                }
            } else {
                const long off2x = 2L * (long)cx + (long)(g - 1) - start2x;
                const long off2z = 2L * (long)cz + (long)(g - 1) - start2z;
                const long d2 = off2x * off2x + off2z * off2z;
                if (d2 <= cutoffD2) {
                    const uint slot = atomic_inc(counter);
                    if (slot < maxHits) {
                        hits[slot] = (int2)(cx, cz);
                        counts[slot] = (uchar)cnt;
                    }
                }
            }
                            }
                }
                if (az + 1 < strideZ) {
                    // 列和滑窗：+ 新进入的行 az+g，- 离开的行 az（同一线程独占本列，无需 barrier）
                    densColCnt[lid] += dens_row_win_popcount(bits, az + g, lid, g, mask);
                    densColCnt[lid] -= dens_row_win_popcount(bits, az, lid, g, mask);
                }
            }
        }
    }
#else
    // ② 锚点分区：每组负责 (strideX x strideZ) 个锚点，恰好覆盖、不重不漏
    //   v3.8.2：遍历改成「按行 + 行内跨步」，去掉每锚点两次整数除法（GPU 上除法是模拟的，很贵；实测 +7.7%）
    const int mask = (g >= 32) ? 0xFFFFFFFFu : ((1u << g) - 1u);
    for (int az = 0; az < strideZ; ++az) {
        for (int ax = lid; ax < strideX; ax += 256) {
        const int cx = minX + ox + ax;
        const int cz = minZ + oz + az;
        if (cx < axMin || cx > axMax || cz < azMin || cz > azMax) continue;
        int cnt = 0;
        for (int dz = 0; dz < g; ++dz) {
            const int rowBase = (az + dz) * DENS_WORDS;
            const int word = ax >> 5;
            const int shift = ax & 31;
            uint v;
            if (shift + g <= 32) {
                v = (bits[rowBase + word] >> shift) & mask;
            } else {
                const uint v1 = bits[rowBase + word] >> shift;
                const uint v2 = bits[rowBase + word + 1] << (32 - shift);
                v = (v1 | v2) & mask;
            }
            cnt += (int)popcount(v);
        }
        if (cnt >= thr) {
            if (mode == 0) {
                // 第一遍：照常回写全部命中（稀疏时直接可用），同时统计距离直方图
                if (hist) {
                    const long off2x = 2L * (long)cx + (long)(g - 1) - start2x;
                    const long off2z = 2L * (long)cz + (long)(g - 1) - start2z;
                    const long d2 = off2x * off2x + off2z * off2z;
                    long bucket = d2 >> bucketShift;
                    if (bucket > 1023L) bucket = 1023L;
                    atomic_inc(&hist[bucket]);
                }
                // v3.10.12.1：先写组内暂存；容量 256 满时才逐命中走全局原子
                const uint li = atomic_inc(&lcount);
                if (li < 256u) {
                    lhitX[li] = (uint)cx;
                    lhitZ[li] = (uint)cz;
                    lhitC[li] = (uchar)cnt;
                } else {
                    const uint slot = atomic_inc(counter);
                    if (slot < maxHits) {
                        hits[slot] = (int2)(cx, cz);
                        counts[slot] = (uchar)cnt;
                    }
                }
            } else {
                const long off2x = 2L * (long)cx + (long)(g - 1) - start2x;
                const long off2z = 2L * (long)cz + (long)(g - 1) - start2z;
                const long d2 = off2x * off2x + off2z * off2z;
                if (d2 <= cutoffD2) {
                    const uint slot = atomic_inc(counter);
                    if (slot < maxHits) {
                        hits[slot] = (int2)(cx, cz);
                        counts[slot] = (uchar)cnt;
                    }
                }
            }
        }
        }
    }

#endif
    // v3.10.12.1：块预留 —— 每 workgroup 一次 global atomic_add 预留连续区间，整块写出。
    //   与溢出的逐命中 atomic_inc 共用同一计数器：atomic_add(n) 与单槽占用互不重叠，
    //   最终 [0, counter[0]) 无空洞，宿主一次连续回读即可。
    barrier(CLK_LOCAL_MEM_FENCE);
    if (lid == 0) {
        const uint n = (lcount < 256u) ? lcount : 256u;
        lbase = atomic_add(counter, n);
    }
    barrier(CLK_LOCAL_MEM_FENCE);
    {
        const uint n = (lcount < 256u) ? lcount : 256u;
        if (lbase < maxHits) {
            const uint lim = (lbase + n <= maxHits) ? n : (maxHits - lbase);
            for (uint i = (uint)lid; i < lim; i += (uint)get_local_size(0)) {
                hits[lbase + i] = (int2)((int)lhitX[i], (int)lhitZ[i]);
                counts[lbase + i] = (uchar)lhitC[i];
            }
        }
    }
}

// v3.8.4：严格图案流式匹配 —— 共享位图上按图案行掩码做位并行 AND，只回传命中。
//   enable/green 为 rows 个掩码（长度 = cols ≤ 32），由宿主按变体预先算好。
#define PAT_TILE_W 256
#define PAT_TILE_H 64
#define PAT_WORDS (PAT_TILE_W / 32)
#define PAT_SHARED (PAT_WORDS * PAT_TILE_H)

__kernel void pattern_stream(long worldSeed, int bedrock, int minX, int minZ, int w, int h,
                             int rows, int cols, int ozBase,
                             __global const uint* enableMasks, __global const uint* greenMasks,
                             int axMin, int axMax, int azMin, int azMax, uint maxHits,
                             __global int2* hits, __global uint* counter)
{
    __local uint bits[PAT_SHARED];
    __local uint enL[32];
    __local uint grL[32];
    // v3.10.12.4：图案命中同样改为「块预留连续写」，避免每命中一次全局原子（密集图案可达百万命中）
    __local uint lhitX[256];
    __local uint lhitZ[256];
    __local uint lcount;
    __local uint lbase;
    const int lid = (int)get_local_id(0);
    if (lid < rows) {
        enL[lid] = enableMasks[lid];
        grL[lid] = greenMasks[lid];
    }
    barrier(CLK_LOCAL_MEM_FENCE);
    const int strideX = PAT_TILE_W - cols + 1;
    const int strideZ = PAT_TILE_H - rows + 1;
    const int tilesX = (w - cols + strideX) / strideX;
    const int tileIdx = (int)get_group_id(0);
    const int ox = (tileIdx % tilesX) * strideX;
    const int oz = ozBase + (tileIdx / tilesX) * strideZ;
    const uint colMask = (cols >= 32) ? 0xFFFFFFFFu : ((1u << cols) - 1u);

    for (int wordIdx = lid; wordIdx < PAT_SHARED; wordIdx += (int)get_local_size(0)) {
        const int lz = wordIdx / PAT_WORDS;
        const int wx = (wordIdx % PAT_WORDS) * 32;
        const int cz = oz + lz;
        uint v = 0;
        if (cz < h && ox + wx < w) {
            if (bedrock) {
                for (int b = 0; b < 32; ++b) {
                    const int cx = ox + wx + b;
                    if (cx >= w) break;
                    v |= (uint)bedrock_is_slime_fast(minX + cx, minZ + cz) << b;
                }
            } else {
                // v3.10.12.4：与密度流式内核一致，用「32 区块一个字的批量内核」代替逐区块判定
                //   （后者每区块都要一次 48 位 LCG，图案 GPU 流式 40k² 曾有 ~170 ms 花在填充位图上）
                const ulong rb = java_row_base(worldSeed, minZ + cz);
                const uint baseLo = (uint)rb, baseHi = (uint)(rb >> 32);
                uint t1 = 0, dd = 0, t2 = 0;
                java_tail_init(minX + ox + wx, &t1, &dd, &t2);
                v = java_hits_word32(baseLo, baseHi, &t1, &dd, &t2);
                if (ox + wx + 32 > w) {
                    const int valid = w - (ox + wx);
                    v &= (valid >= 32) ? 0xFFFFFFFFu : ((1u << valid) - 1u);
                }
            }
        }
        bits[wordIdx] = v;
    }
    if (lid == 0) { lcount = 0u; lbase = 0u; }
    barrier(CLK_LOCAL_MEM_FENCE);

    const int axMaxTile = strideX - 1;
    // v3.10.12.7：32 锚点位并行（纯 32 位运算，避免 64 位模拟开销）+ 按「行×组」展平任务，
    //   每个线程都有活干（每行 8 组，256 线程可跑 2 行）。
    const int groupsX32 = (strideX + 31) >> 5;
    const int totalTasks = strideZ * groupsX32;
    for (int task = lid; task < totalTasks; task += 256) {
        const int az = task / groupsX32;
        const int A = (task % groupsX32) * 32;
        uint ok = 0xFFFFFFFFu;
        for (int dz = 0; dz < rows; ++dz) {
            const uint en = enL[dz];
            if (en == 0u) continue;
            const uint gr = grL[dz];
            const int rowBase = (az + dz) * PAT_WORDS;
            for (int cc = 0; cc < cols; ++cc) {
                if (((en >> cc) & 1u) == 0u) continue;
                const int pos = A + cc;
                const int word = pos >> 5;
                const int sh = pos & 31;
                const int rb = rowBase + word;
                uint v;
                if (sh == 0) {
                    v = bits[rb];
                } else {
                    const uint hi = (word + 1 < PAT_WORDS) ? bits[rb + 1] : 0u;
                    v = (bits[rb] >> sh) | (hi << (32 - sh));
                }
                if (((gr >> cc) & 1u) != 0u) {
                    ok &= v;
                } else {
                    ok &= ~v;
                }
            }
        }
        for (int i = 0; i < 32; ++i) {
            if (((ok >> i) & 1u) == 0u) continue;
            const int ax = A + i;
            if (ax > axMaxTile) break;
            const int cx = minX + ox + ax;
            const int cz = minZ + oz + az;
            if (cx < axMin || cx > axMax || cz < azMin || cz > azMax) continue;
            const uint li = atomic_inc(&lcount);
            if (li < 256u) {
                lhitX[li] = (uint)cx;
                lhitZ[li] = (uint)cz;
            } else {
                const uint slot = atomic_inc(counter);
                if (slot < maxHits) hits[slot] = (int2)(cx, cz);
            }
        }
    }

    // v3.10.12.4：每 workgroup 一次块预留，整块写出
    barrier(CLK_LOCAL_MEM_FENCE);
    if (lid == 0) {
        const uint n = (lcount < 256u) ? lcount : 256u;
        lbase = atomic_add(counter, n);
    }
    barrier(CLK_LOCAL_MEM_FENCE);
    {
        const uint n = (lcount < 256u) ? lcount : 256u;
        if (lbase < maxHits) {
            const uint lim = (lbase + n <= maxHits) ? n : (maxHits - lbase);
            for (uint i = (uint)lid; i < lim; i += (uint)get_local_size(0)) {
                hits[lbase + i] = (int2)((int)lhitX[i], (int)lhitZ[i]);
            }
        }
    }
}

__kernel void scan_java_words(long worldSeed, int minX, int minZ, int w, int h, int wpr,
                              __global ulong* out)
{
    size_t idx = get_global_id(0);
    if (idx >= (size_t)wpr * (size_t)h) return;
    const int row = (int)(idx / (size_t)wpr);
    const int word = (int)(idx % (size_t)wpr);
    const int cxBase = minX + word * 64;
    const int cz = minZ + row;
    const ulong rowBase = java_row_base(worldSeed, cz);   // 每行一次
    const uint baseLo = (uint)rowBase, baseHi = (uint)(rowBase >> 32);
    uint t1 = 0, dd = 0, t2 = 0;
    java_tail_init(cxBase, &t1, &dd, &t2);
    const uint lo32 = java_hits_word32(baseLo, baseHi, &t1, &dd, &t2);
    const uint hi32 = java_hits_word32(baseLo, baseHi, &t1, &dd, &t2);
    out[idx] = (ulong)lo32 | ((ulong)hi32 << 32);
}

// v3.7.4：基岩版「一次 64 区块 → 一个 ulong」打包输出
// v3.7.7：两个区块的种子链交错推进（每步算 2 条链 ⇒ 把乘法的 4+ 周期延迟藏起来）
static inline uint bedrock_output(uint m0, uint m1, uint cur)
{
    uint y = (m0 & 0x80000000u) | (m1 & 0x7FFFFFFFu);
    uint v = cur ^ (y >> 1u) ^ (0x9908B0DFu & (0u - (y & 1u)));
    v ^= (v >> 11u);
    v ^= (v << 7u) & 0x9D2C5680u;
    v ^= (v << 15u) & 0xEFC60000u;
    v ^= (v >> 18u);
    return mod10_is_zero(v);
}

static inline void bedrock_pair(int cxA, int cxB, int cz, uint* hitA, uint* hitB)
{
    const uint m0a = ((uint)cxA * 0x1F1F1F1Fu) ^ (uint)cz;
    const uint m0b = ((uint)cxB * 0x1F1F1F1Fu) ^ (uint)cz;
    uint ma = 1812433253u * (m0a ^ (m0a >> 30u)) + 1u;
    uint mb = 1812433253u * (m0b ^ (m0b >> 30u)) + 1u;
    const uint m1a = ma, m1b = mb;
    for (uint i = 2u; i <= 397u; ++i) {
        ma = 1812433253u * (ma ^ (ma >> 30u)) + i;
        mb = 1812433253u * (mb ^ (mb >> 30u)) + i;
    }
    *hitA = bedrock_output(m0a, m1a, ma);
    *hitB = bedrock_output(m0b, m1b, mb);
}

__kernel void scan_bedrock_words(int minX, int minZ, int w, int h, int wpr,
                                 __global ulong* out)
{
    size_t idx = get_global_id(0);
    if (idx >= (size_t)wpr * (size_t)h) return;
    const int row = (int)(idx / (size_t)wpr);
    const int word = (int)(idx % (size_t)wpr);
    const int cxBase = minX + word * 64;
    const int cz = minZ + row;
    const int cxEnd = minX + w;
    ulong bits = 0;
    for (int b = 0; b < 32; ++b) {
        const int cxA = cxBase + b;
        const int cxB = cxBase + b + 32;
        if (cxB < cxEnd) {
            uint ha = 0, hb = 0;
            bedrock_pair(cxA, cxB, cz, &ha, &hb);
            bits |= (ulong)ha << b;
            bits |= (ulong)hb << (b + 32);
        } else {
            if (cxA < cxEnd) bits |= (ulong)(uint)bedrock_is_slime_fast(cxA, cz) << b;
            break;
        }
    }
    out[idx] = bits;
}

// v3.7.6：环形覆盖 —— 每个 work-item 算一个候选中心的「环带内史莱姆格数」。
//   rowPref 为占用图的逐行前缀和（(w+1)*h，int32），ocx/ocz 为占用图原点（方块坐标）。
//   与 CPU 的 Occupancy::rect 语义一致：越界按 0 处理（夹到边界）。
__kernel void annulus_cov(__global const int* pref2d, int w, int h,
                          int ocx, int ocz, int step, int nx, int nz,
                          int cx0, int cz0, int rIn, int rOut, __global int* out)
{
    int idx = (int)get_global_id(0);
    if (idx >= nx * nz) return;
    const int ix = idx % nx;
    const int iz = idx / nx;
    const int cx = cx0 + ix * step;
    const int cz = cz0 + iz * step;
    int total = 0;
    const long r2o = (long)rOut * rOut;
    for (int dz = -rOut; dz <= rOut; ++dz) {
        long rem = r2o - (long)dz * dz;
        if (rem < 0) continue;
        int rw = (int)sqrt((float)rem);
        while ((long)(rw + 1) * (rw + 1) + (long)dz * dz <= r2o) ++rw;
        while (rw > 0 && (long)rw * rw + (long)dz * dz > r2o) --rw;
        const int lz = cz + dz - ocz;
        if (lz < 0 || lz >= h) continue;
        int a = cx - rw - ocx;
        int b = cx + rw - ocx;
        if (a < 0) a = 0;
        if (b >= w) b = w - 1;
        const int stride = w + 1;
        total += (pref2d[(lz + 1) * stride + b + 1] - pref2d[(lz + 1) * stride + a]) -
                 (pref2d[lz * stride + b + 1] - pref2d[lz * stride + a]);
    }
    if (rIn > 0) {
        const long r2i = (long)rIn * rIn - 1;
        for (int dz = -rIn; dz <= rIn; ++dz) {
            long rem = r2i - (long)dz * dz;
            if (rem < 0) continue;
            int rw = (int)sqrt((float)rem);
            while ((long)(rw + 1) * (rw + 1) + (long)dz * dz <= r2i) ++rw;
            while (rw > 0 && (long)rw * rw + (long)dz * dz > r2i) --rw;
            const int lz = cz + dz - ocz;
            if (lz < 0 || lz >= h) continue;
            int a = cx - rw - ocx;
            int b = cx + rw - ocx;
            if (a < 0) a = 0;
            if (b >= w) b = w - 1;
            const int stride = w + 1;
            total -= (pref2d[(lz + 1) * stride + b + 1] - pref2d[(lz + 1) * stride + a]) -
                     (pref2d[lz * stride + b + 1] - pref2d[lz * stride + a]);
        }
    }
    out[idx] = total;
}

__kernel void scan_bedrock_slime(int minX, int minZ, int w, int h,
                                 __global uchar* out)
{
    size_t idx = get_global_id(0);
    if (idx >= (size_t)w * (size_t)h) return;
    int cx = minX + (int)(idx % (size_t)w);
    int cz = minZ + (int)(idx / (size_t)w);
    // v3.6.21 实验：改用 398 步代数等价捷径（原为 624 init + 624 twist + 624 项私有数组）
    out[idx] = (uchar)bedrock_is_slime_fast(cx, cz);
}
