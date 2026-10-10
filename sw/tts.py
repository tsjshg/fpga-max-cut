#!/usr/bin/env python3
"""周回数の合計を振って、目標に届く割合 p と TTS99 を表にする（M0 の比較用）。

TTS99 = τ · ln(0.01) / ln(1 − p)   （τ は1回の焼きなましの長さ。p ≥ 0.99 なら τ）
単位は「1 レプリカあたりの周回数」。試行回数にするには n を掛ける。
同じアルゴリズムなら試行回数は CPU と回路で同じなので、時間 = 試行回数 ÷ 試行/秒。

使い方: tts.py graph.adj target --T0 0.8 --T1 0.2 --totals 30 100 300 [--R 1024] [--extra '--order rand']
"""
import argparse
import math
import os
import re
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))


def tts99(tau, p):
    if p <= 0:
        return math.inf
    if p >= 0.99:
        return tau
    return tau * math.log(0.01) / math.log(1 - p)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("graph")
    ap.add_argument("target", type=int)
    ap.add_argument("--T0", type=float, required=True)
    ap.add_argument("--T1", type=float, required=True)
    ap.add_argument("--totals", type=int, nargs="+", required=True)
    ap.add_argument("--stages", type=int, default=100)
    ap.add_argument("--R", type=int, default=1024)
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--extra", default="")
    ap.add_argument("-j", type=int, default=os.cpu_count())
    ap.add_argument("--sa", default=os.path.join(HERE, "sa_fast"), help="sa か sa_fast（結果は同じ）")
    a = ap.parse_args()
    with open(a.graph) as f:
        line = f.readline()
        while line.startswith("#"):
            line = f.readline()
        n, d = map(int, line.split())
    print(f"# {a.graph} target={a.target} T0={a.T0} T1={a.T1} R={a.R} seed={a.seed} {a.extra}")
    print(f"{'周回':>7} {'段数':>5} {'到達':>10} {'p':>7} {'TTS99(周回)':>12} {'平均最良':>9}")
    for total in a.totals:
        stages = min(a.stages, total)
        with tempfile.NamedTemporaryFile("w", suffix=".sched", delete=False) as f:
            path = f.name
        subprocess.run([sys.executable, os.path.join(HERE, "make_sched.py"), str(d), "--T0", str(a.T0),
                        "--T1", str(a.T1), "--stages", str(stages), "--sweeps", str(total // stages),
                        "-o", path], check=True)
        out = subprocess.run([a.sa, "-g", a.graph, "-s", path, "-R", str(a.R),
                              "-S", str(a.seed), "-j", str(a.j), "-q", "-t", str(a.target)]
                             + a.extra.split(), check=True, capture_output=True, text=True).stdout
        os.unlink(path)
        hits = int(re.search(r"target \d+: (\d+)/", out).group(1))
        mean = float(re.search(r"mean=([\d.]+)", out).group(1))
        tau = stages * (total // stages)
        p = hits / a.R
        print(f"{tau:>7} {stages:>5} {hits:>5}/{a.R:<4} {p:7.3f} {tts99(tau, p):12.0f} {mean:9.2f}",
              flush=True)


if __name__ == "__main__":
    main()
