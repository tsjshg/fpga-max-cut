#!/usr/bin/env python3
"""温度スケジュールを「しきい値の表」の整数ファイルにする。

浮動小数を使うのはここだけ。ここで1回だけ整数にして、CPU 版（Mac・ボード）と回路の
全員が同じファイルを読む。exp の最下位ビットが libm ごとに違う問題がこれで入り込まない。

頂点 v の同じ色の隣の数を n_same（0..d）とすると、反転したときのカット値の増分は
    ΔC = 2·n_same − d
受け入れの規則（CPU 版と回路で共通）:
    ΔC >= 0                          → 受け入れる
    ΔC <  0 かつ r < TH[n_same]      → 受け入れる（r は乱数の上位 32 bit）
    TH[n_same] = min(floor(exp(ΔC / T) · 2^32), 2^32 − 1)
ΔC >= 0 の欄は使わないが、0xFFFFFFFF を入れておく（r < 0xFFFFFFFF は r = 2^32−1 で偽に
なるので、回路でも「ΔC >= 0」の判定は別に持つこと）。

温度は T0 から T1 まで幾何級数で S 段下げ、各段で m 周回（1 周回 = 全頂点を1回ずつ）回す。

書き出す形（.sched）:
    # コメント
    d S
    m TH[0] TH[1] ... TH[d]     ← S 行。m はその段の周回数
"""
import argparse
import math
import sys


def thresholds(d, T):
    th = []
    for ns in range(d + 1):
        dc = 2 * ns - d
        if dc >= 0:
            th.append(0xFFFFFFFF)
        else:
            th.append(min(math.floor(math.exp(dc / T) * 2**32), 0xFFFFFFFF))
    return th


def geometric(d, T0, T1, S, m):
    rows = []
    for k in range(S):
        T = T0 * (T1 / T0) ** (k / (S - 1)) if S > 1 else T0
        rows.append((m, thresholds(d, T)))
    return rows


def write_sched(f, d, rows, comment):
    f.write(f"# {comment}\n")
    f.write(f"{d} {len(rows)}\n")
    for m, th in rows:
        f.write(f"{m} " + " ".join(map(str, th)) + "\n")


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("d", type=int)
    ap.add_argument("--T0", type=float, required=True, help="初期温度")
    ap.add_argument("--T1", type=float, required=True, help="最終温度")
    ap.add_argument("--stages", type=int, required=True, help="温度の段数 S")
    ap.add_argument("--sweeps", type=int, default=1, help="1 段あたりの周回数 m")
    ap.add_argument("-o", "--out")
    a = ap.parse_args()
    rows = geometric(a.d, a.T0, a.T1, a.stages, a.sweeps)
    comment = (f"geometric T0={a.T0} T1={a.T1} stages={a.stages} sweeps/stage={a.sweeps} "
               f"total={a.stages * a.sweeps} sweeps (make_sched.py)")
    if a.out:
        with open(a.out, "w") as f:
            write_sched(f, a.d, rows, comment)
    else:
        write_sched(sys.stdout, a.d, rows, comment)


if __name__ == "__main__":
    main()
