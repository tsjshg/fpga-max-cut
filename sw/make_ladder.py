#!/usr/bin/env python3
"""レプリカ交換法の温度のはしごを、整数の表（.ladder）にする。浮動小数はここだけ（make_sched.py と同じ考え方）。

温度は T_0 < T_1 < … < T_{M−1} の幾何級数。k = 0 がいちばん冷たい。
  TH[k][n_same]: 反転の受け入れのしきい値（make_sched.py と同じ式）
  SW[k][D]:      温度 k と k+1 にいる 2 つのレプリカの交換の受け入れのしきい値（D = 1 … DMAX）
                 冷たい側のカット値 C_k、熱い側 C_{k+1} として D = C_k − C_{k+1}。
                 D ≤ 0 なら必ず交換、D > 0 なら r < SW[k][min(D, DMAX)] のとき交換
                 SW[k][D] = floor(exp(−(β_k − β_{k+1})·D)·2^32)、β = 1/T。DMAX を超える D は SW[k][DMAX]（= 0 になるよう DMAX を取る）

書き出す形:
    # コメント
    d M DMAX
    TH[0][0] … TH[0][d]          ← M 行
    SW[0][1] … SW[0][DMAX]       ← M−1 行
"""
import argparse
import math
import sys

sys.path.insert(0, __import__("os").path.dirname(__import__("os").path.abspath(__file__)))
from make_sched import thresholds  # noqa: E402


def ladder(d, Tmin, Tmax, M):
    Ts = [Tmin * (Tmax / Tmin) ** (k / (M - 1)) for k in range(M)]
    th = [thresholds(d, T) for T in Ts]
    dbeta = [1 / Ts[k] - 1 / Ts[k + 1] for k in range(M - 1)]
    dmax = math.ceil(32 * math.log(2) / min(dbeta)) + 1      # これを超えると確率は 2^-32 未満
    sw = [[min(math.floor(math.exp(-db * D) * 2**32), 0xFFFFFFFF) for D in range(1, dmax + 1)] for db in dbeta]
    assert all(row[-1] == 0 for row in sw)
    return Ts, th, sw, dmax


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("d", type=int)
    ap.add_argument("--Tmin", type=float, required=True)
    ap.add_argument("--Tmax", type=float, required=True)
    ap.add_argument("--M", type=int, default=16)
    ap.add_argument("-o", "--out", required=True)
    a = ap.parse_args()
    Ts, th, sw, dmax = ladder(a.d, a.Tmin, a.Tmax, a.M)
    with open(a.out, "w") as f:
        f.write(f"# ladder Tmin={a.Tmin} Tmax={a.Tmax} M={a.M} T=[{', '.join(f'{T:.4f}' for T in Ts)}] (make_ladder.py)\n")
        f.write(f"{a.d} {a.M} {dmax}\n")
        for row in th:
            f.write(" ".join(map(str, row)) + "\n")
        for row in sw:
            f.write(" ".join(map(str, row)) + "\n")
    print(f"M={a.M} DMAX={dmax}", file=sys.stderr)


if __name__ == "__main__":
    main()
