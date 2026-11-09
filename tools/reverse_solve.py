"""reverse_solve.py - 按图案逆向求解坐标（SMT / Z3 位向量）

v2：把「变量×变量」的乘法从 m 个降到 **2 个**（只保留 x*x 和 z*z），
其余偏移用平方展开的仿射式，显著降低 SAT 位爆破规模：
    (x+dx)^2 ≡ x^2 + 2*dx*x + dx^2   (mod 2^32)   —— 16 位整数回绕下恒成立
    (z+dz)^2 ≡ z^2 + 2*dz*z + dz^2   (mod 2^32)

Java 语义（与引擎逐位一致）：
    F(x) = sext32( int32(x*x*4987142) ) + sext32( int32(x*5947611) )
    H(z) = sext32( int32(z*z) ) * 4392871 + sext32( int32(z*389711) )
    u = seed + F(x) + H(z) (mod 2^64)
    s0 = (u ^ 987234911) & (2^48-1);  s0 = (s0 ^ A) & (2^48-1);
    s1 = (A*s0 + 11) & (2^48-1);      q = s1 >> 17
    命中 ⟺ q % 10 == 0，且 q != 2147483640（该值会被 nextInt(10) 拒绝重抽）

用法：
    py -3 tools/reverse_solve.py --cols 2 --rows 2 --timeout 60
    py -3 tools/reverse_solve.py --cols 3 --rows 3 --timeout 300
"""
import argparse
import time

from z3 import (BitVec, BitVecVal, Extract, LShR, SignExt, Solver, ZeroExt, sat, unknown)

MASK48 = (1 << 48) - 1
A = 0x5DEECE66D
B = 0xB
K = 987234911
REJECT = 2147483640


def slime_expr(seed, f64, h64):
    """f64/h64 为 64 位表达式（已符号扩展）；返回 q（32 位，高 1 位恒 0）。"""
    u = BitVecVal(seed & 0xFFFFFFFFFFFFFFFF, 64) + f64 + h64
    # Java Random 构造：seed = (seed ^ K) & MASK，再 seed = (seed ^ A) & MASK
    s0 = (u ^ BitVecVal(K, 64)) & BitVecVal(MASK48, 64)
    s0 = (s0 ^ BitVecVal(A, 64)) & BitVecVal(MASK48, 64)
    s1 = (s0 * BitVecVal(A, 64) + BitVecVal(B, 64)) & BitVecVal(MASK48, 64)
    return ZeroExt(1, Extract(30, 0, LShR(s1, 17)))


def build(seed, cols, rows, bound, fixz=-1):
    x = BitVec('x', 32)
    z = BitVec('z', 32) if fixz < 0 else BitVecVal(fixz, 32)
    y = BitVec('y', 32)      # y == x*x
    w2 = BitVec('w2', 32)    # w2 == z*z
    s = Solver()
    s.add(y == x * x)
    if fixz < 0:
        s.add(w2 == z * z)
    if bound:
        s.add(x >= -bound, x <= bound, z >= -bound, z <= bound)

    C1 = BitVecVal(4987142, 32)
    C2 = BitVecVal(5947611, 32)
    C3 = BitVecVal(4392871, 64)
    C4 = BitVecVal(389711, 32)

    for dz in range(rows):
        # (z+dz)^2 mod 2^32 = w2 + 2*dz*z + dz^2
        qz = w2 + BitVecVal(2 * dz, 32) * z + BitVecVal(dz * dz, 32)
        hz = SignExt(32, qz) * C3 + SignExt(32, (z + BitVecVal(dz, 32)) * C4)
        for dx in range(cols):
            # (x+dx)^2 mod 2^32 = y + 2*dx*x + dx^2
            qx = y + BitVecVal(2 * dx, 32) * x + BitVecVal(dx * dx, 32)
            fx = SignExt(32, qx * C1) + SignExt(32, (x + BitVecVal(dx, 32)) * C2)
            q = slime_expr(seed, fx, hz)
            t = BitVec('t_%d_%d' % (dz, dx), 28)
            s.add(q == ZeroExt(4, t) * BitVecVal(10, 32))
            s.add(q != BitVecVal(REJECT, 32))
    return s, x, z


def reference_slime(seed, cx, cz):
    def i32(v):
        v &= 0xFFFFFFFF
        return v - (1 << 32) if v & 0x80000000 else v
    t1 = i32(i32(cx * cx) * 4987142)
    t2 = i32(cx * 5947611)
    t3 = i32(cz * cz) * 4392871
    t4 = i32(cz * 389711)
    u = (seed + t1 + t2 + t3 + t4) & 0xFFFFFFFFFFFFFFFF
    s0 = (u ^ K) & MASK48
    s0 = (s0 ^ A) & MASK48
    s1 = (s0 * A + B) & MASK48
    q = s1 >> 17
    return q % 10 == 0 and q != REJECT


def verify(seed, x, z, cols, rows):
    return all(reference_slime(seed, x + dx, z + dz) for dz in range(rows) for dx in range(cols))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--cols', type=int, default=3)
    ap.add_argument('--rows', type=int, default=3)
    ap.add_argument('--seed', type=int, default=0)
    ap.add_argument('--timeout', type=float, default=120.0)
    ap.add_argument('--bound', type=int, default=1 << 20)
    ap.add_argument('--fixz', type=int, default=-1, help='固定 z（只解 x）；用于「逐 z 尝试」的逆向策略')
    a = ap.parse_args()

    s, x, z = build(a.seed, a.cols, a.rows, a.bound, a.fixz)
    s.set('timeout', int(a.timeout * 1000))
    t0 = time.time()
    r = s.check()
    dt = time.time() - t0
    m = a.cols * a.rows
    print('图案 %dx%d（m=%d），seed=%d，Z3 结果=%s，耗时 %.3f s' % (a.cols, a.rows, m, a.seed, r, dt), flush=True)
    if r == sat:
        model = s.model()
        xv = model.eval(x).as_long()
        zv = model.eval(z).as_long() if a.fixz < 0 else a.fixz
        if xv >= 1 << 31:
            xv -= 1 << 32
        if zv >= 1 << 31:
            zv -= 1 << 32
        ok = verify(a.seed, xv, zv, a.cols, a.rows)
        print('  解：x=%d z=%d；正向逐点校验=%s' % (xv, zv, '通过' if ok else '失败'), flush=True)


if __name__ == '__main__':
    main()
