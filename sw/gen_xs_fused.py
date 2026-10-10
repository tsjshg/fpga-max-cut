#!/usr/bin/env python3
"""xorshift64 の1回（x ^= x<<13; x ^= x>>7; x ^= x<<17）を、ビットスライスの語の演算に展開する。

語 X[b] のビット i = レプリカ i の状態のビット b。シフトは語の番号のずれになり、
xorshift64 は語どうしの XOR だけになる。作るもの:

XS_FUSED(w, x)   C。新しい状態のビット b を古い状態の 2〜8 ビットの XOR で直接作る（計 392 項）。
                 3 段を語の上で順にやると XOR のたびにメモリへ書き戻す（155 回）が、これは 64 回で済む
XS_A64(w, x)     AArch64 のインラインアセンブリ（q レジスタ 1 本 = 128 レプリカ）。
XS_A32(w, x)     ARMv7 NEON のインラインアセンブリ（d レジスタ 1 本 = 64 レプリカ）。語の間隔は 16 バイト
                 （128 レプリカの語の前半か後半の 64 ビットだけを進める。2 回呼んで 128 レプリカ）。
                 どちらも b を下から順に
                     y_b = x_b ^ x_{b−13}、z_{b−7} = y_{b−7} ^ y_b、w_{b−7} = z_{b−7} ^ z_{b−24}
                 と流し、y の 8 語・z の 18 語の窓をレジスタに置く（同時に生きる値は 26 個）。
                 レジスタの割り当てをここで決めるので、コンパイラの退避が入らない。
                 XOR 155 回・読み込み 115 回・書き込み 64 回

    python3 sw/gen_xs_fused.py > sw/xs_fused.h
"""
import random

import os

M64 = (1 << 64) - 1
LAT_LD = int(os.environ.get("LAT_LD", 4))
LAT_EOR = int(os.environ.get("LAT_EOR", 3))
WINDOW = int(os.environ.get("WINDOW", 12))


def xs64(v):
    v ^= (v << 13) & M64
    v ^= v >> 7
    v ^= (v << 17) & M64
    return v


def fused_terms():
    x = [{b} for b in range(64)]
    shl = lambda v, k: [set() if b - k < 0 else set(v[b - k]) for b in range(64)]
    shr = lambda v, k: [set() if b + k > 63 else set(v[b + k]) for b in range(64)]
    xor = lambda a, c: [ai ^ ci for ai, ci in zip(a, c)]
    y = xor(x, shl(x, 13))
    z = xor(y, shr(y, 7))
    return xor(z, shl(z, 17))


def stream_ops():
    """流す順の演算の列。('ld', 値, 語) / ('eor', 値, 入力1, 入力2) / ('st', 値, 語)"""
    seq, alias = [], {}

    def R(v):
        while v in alias:
            v = alias[v]
        return v

    def eor(d, a, b):
        seq.append(("eor", d, R(a), R(b)))

    for b in range(64):
        seq.append(("ld", "xa%d" % b, b))
        if b >= 13:
            seq.append(("ld", "xb%d" % b, b - 13))
            eor("y%d" % b, "xa%d" % b, "xb%d" % b)
        else:
            alias["y%d" % b] = "xa%d" % b
        if b >= 7:
            c = b - 7
            eor("z%d" % c, "y%d" % c, "y%d" % b)
            if c >= 17:
                eor("w%d" % c, "z%d" % c, "z%d" % (c - 17))
                seq.append(("st", "w%d" % c, c))
            else:
                seq.append(("st", R("z%d" % c), c))
    for c in range(57, 64):
        alias["z%d" % c] = "y%d" % c
        eor("w%d" % c, "z%d" % c, "z%d" % (c - 17))
        seq.append(("st", "w%d" % c, c))
    return seq


def simulate(seq, x):
    val, w = {}, [None] * 64
    for op in seq:
        if op[0] == "ld":
            val[op[1]] = x[op[2]]
        elif op[0] == "eor":
            val[op[1]] = val[op[2]] ^ val[op[3]]
        else:
            w[op[2]] = val[op[1]]
    return w


def schedule(seq, lat_ld, lat_eor, window):
    """順に実行する CPU（Cortex-A9 の NEON）向けの並べ替え。入力がそろっている命令のうち、
    結果が早くそろうものから出す。先読みは window 命令まで（レジスタが足りなくならないように）。
    書き込み（st）はその語を読み終えてからでないと、次の呼び出しの読みと… は関係ない（w と x は別）"""
    ready_at = {}
    pending = list(seq)
    out, cyc = [], 0
    while pending:
        best = None
        for j, op in enumerate(pending[:window]):
            ins = op[2:4] if op[0] == "eor" else [op[1]] if op[0] == "st" else []
            if any(u not in ready_at for u in ins):
                continue
            t = max([ready_at[u] for u in ins] + [cyc])
            if best is None or t < best[0]:
                best = (t, j)
        t, j = best
        op = pending.pop(j)
        cyc = t + 1
        if op[0] == "ld":
            ready_at[op[1]] = cyc - 1 + lat_ld
        elif op[0] == "eor":
            ready_at[op[1]] = cyc - 1 + lat_eor
        out.append(op)
    return out, cyc


def allocate(seq, nregs):
    """寿命の終わった値のレジスタを使い回す。足りなければ止める"""
    last = {}
    for i, op in enumerate(seq):
        for u in (op[2:4] if op[0] == "eor" else [op[1]] if op[0] == "st" else []):
            last[u] = i
    free, reg, out, maxlive = list(range(nregs)), {}, [], 0
    for i, op in enumerate(seq):
        if op[0] == "ld":
            reg[op[1]] = free.pop(0)
            out.append(("ld", reg[op[1]], op[2]))
        elif op[0] == "eor":
            a, b = reg[op[2]], reg[op[3]]
            for u in (op[2], op[3]):
                if last[u] == i:
                    free.insert(0, reg[u])
            reg[op[1]] = free.pop(0)
            out.append(("eor", reg[op[1]], a, b))
        else:
            out.append(("st", reg[op[1]], op[2]))
            if last[op[1]] == i:
                free.insert(0, reg[op[1]])
        maxlive = max(maxlive, nregs - len(free))
    return out, maxlive


def asm_macro(name, alloc, fmt, nregs, clobber, size):
    lines = ["#define %s(W_, X_) \\" % name, "    __asm__ volatile( \\"]
    for op in alloc:
        if op[0] == "ld":
            s = fmt["ld"] % (op[1], op[2] * size)
        elif op[0] == "eor":
            s = fmt["eor"] % (op[1], op[2], op[3])
        else:
            s = fmt["st"] % (op[1], op[2] * size)
        lines.append('        "%s\\n\\t" \\' % s)
    lines.append('        : : [w] "r"(W_), [x] "r"(X_) \\')
    lines.append('        : "memory", %s)' % ", ".join('"%s%d"' % (clobber, r) for r in range(nregs)))
    return "\n".join(lines)


def main():
    w = fused_terms()
    seq = stream_ops()
    for _ in range(1000):  # 展開が xorshift64 と同じかを、64 レプリカぶんの語で確かめる
        st = [random.getrandbits(64) for _ in range(64)]
        X = [sum(((st[i] >> b) & 1) << i for i in range(64)) for b in range(64)]
        nst = [xs64(v) for v in st]
        NX = [sum(((nst[i] >> b) & 1) << i for i in range(64)) for b in range(64)]
        F = [0] * 64
        for b in range(64):
            for s in w[b]:
                F[b] ^= X[s]
        assert F == NX
        assert simulate(seq, X) == NX
    alloc, maxlive = allocate(seq, 32)
    sched, _ = schedule(seq, LAT_LD, LAT_EOR, WINDOW)
    assert simulate(sched, X) == NX
    alloc32, maxlive32 = allocate(sched, 32)
    nld = sum(op[0] == "ld" for op in seq)
    neor = sum(op[0] == "eor" for op in seq)
    nst = sum(op[0] == "st" for op in seq)

    print("/* sw/gen_xs_fused.py が作る。手で直さない。w と x は別の配列（x が古い状態、w が新しい状態） */")
    print("#define XS_FUSED(w, x) \\")
    print("    do { \\")
    for b in range(64):
        print("        (w)[%d] = %s; \\" % (b, " ^ ".join("(x)[%d]" % s for s in sorted(w[b]))))
    print("    } while (0)")
    print()
    print("/* 窓を流す形: 読み %d・XOR %d・書き %d、同時に生きる値 %d 個 */" % (nld, neor, nst, maxlive))
    print("#if defined(__aarch64__)")
    print(asm_macro("XS_A64", alloc,
                    {"ld": "ldr q%d, [%%[x], #%d]", "eor": "eor v%d.16b, v%d.16b, v%d.16b",
                     "st": "str q%d, [%%[w], #%d]"}, 32, "v", 16))
    print("#elif defined(__arm__) && defined(__ARM_NEON)")
    print("/* A9 向けに並べ替えた形（結果の遅れ 読み %d・XOR %d、先読み %d 命令）: 同時に生きる値 %d 個 */"
          % (LAT_LD, LAT_EOR, WINDOW, maxlive32))
    print(asm_macro("XS_A32", alloc32,
                    {"ld": "vldr d%d, [%%[x], #%d]", "eor": "veor d%d, d%d, d%d",
                     "st": "vstr d%d, [%%[w], #%d]"}, 32, "d", 16))
    print("#endif")


if __name__ == "__main__":
    main()
