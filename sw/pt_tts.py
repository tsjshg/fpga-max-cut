#!/usr/bin/env python3
"""レプリカ交換法（pt）と焼きなまし（sa_fast）を、目標に届くまでの試行回数（TTS99）で比べる（M5 の検討用）。

試行回数で比べるのは、回路では 1 クロックの試行数がどちらでも同じだから（比がそのまま時間の比になる）。
  焼きなまし: 1 レプリカの TTS99 = τ·n·ln(0.01)/ln(1 − p)               （p = レプリカが届く割合）
  交換法:     1 系（M レプリカ）の TTS99 = M·τ·n·ln(0.01)/ln(1 − P)    （P = 系の中のどれかが届く割合）
どちらも「全レプリカの試行の合計」で、P ≥ 0.99 なら 1 回分（τ·n、交換法は M·τ·n）。

使い方: pt_tts.py graph target --ladder Tmin Tmax --taus 1000 3000 [--systems 64] [-e 1] [--lag 0]
        pt_tts.py graph target --sa T0 T1 --taus ... [--replicas 1024]
"""
import argparse
import math
import os
import re
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))


def tts(total, P):
    if P <= 0:
        return math.inf
    return total if P >= 0.99 else total * math.log(0.01) / math.log(1 - P)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("graph")
    ap.add_argument("target", type=int)
    g = ap.add_mutually_exclusive_group(required=True)
    g.add_argument("--ladder", type=float, nargs=2, metavar=("Tmin", "Tmax"))
    g.add_argument("--sa", type=float, nargs=2, metavar=("T0", "T1"))
    ap.add_argument("--taus", type=int, nargs="+", required=True)
    ap.add_argument("--systems", type=int, default=64)
    ap.add_argument("--replicas", type=int, default=1024)
    ap.add_argument("--M", type=int, default=16)
    ap.add_argument("-e", type=int, default=1)
    ap.add_argument("--lag", type=int, default=0)
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("-j", type=int, default=os.cpu_count())
    a = ap.parse_args()
    with open(a.graph) as f:
        line = f.readline()
        while line.startswith("#"):
            line = f.readline()
        n, d = map(int, line.split())
    best = math.inf
    with tempfile.TemporaryDirectory() as tmp:
        if a.ladder:
            lp = os.path.join(tmp, "l.ladder")
            subprocess.run([sys.executable, os.path.join(HERE, "make_ladder.py"), str(d), "--Tmin", str(a.ladder[0]),
                            "--Tmax", str(a.ladder[1]), "--M", str(a.M), "-o", lp], check=True, capture_output=True)
            label = f"PT ladder {a.ladder[0]}–{a.ladder[1]} M={a.M} e={a.e} lag={a.lag} systems={a.systems}"
        else:
            label = f"SA {a.sa[0]}→{a.sa[1]} replicas={a.replicas}"
        print(f"# {os.path.basename(a.graph)} target={a.target} {label}")
        for tau in a.taus:
            if a.ladder:
                out = subprocess.run([os.path.join(HERE, "pt"), "-g", a.graph, "-l", lp, "-w", str(tau),
                                      "-R", str(a.systems), "-S", str(a.seed), "-e", str(a.e), "--lag", str(a.lag),
                                      "-j", str(a.j), "-q", "-t", str(a.target)],
                                     check=True, capture_output=True, text=True).stdout
                k = int(re.search(r"target \d+: (\d+)/", out).group(1))
                P = k / a.systems
                T = tts(a.M * tau * n, P)
                cnt = f"{k}/{a.systems} systems"
            else:
                sp = os.path.join(tmp, "s.sched")
                subprocess.run([sys.executable, os.path.join(HERE, "make_sched.py"), str(d), "--T0", str(a.sa[0]),
                                "--T1", str(a.sa[1]), "--stages", "100", "--sweeps", str(tau // 100), "-o", sp],
                               check=True)
                out = subprocess.run([os.path.join(HERE, "sa_fast"), "-g", a.graph, "-s", sp, "-R", str(a.replicas),
                                      "-S", str(a.seed), "-j", str(a.j), "-q", "-t", str(a.target)],
                                     check=True, capture_output=True, text=True).stdout
                k = int(re.search(r"target \d+: (\d+)/", out).group(1))
                P = k / a.replicas
                T = tts(tau * n, P)
                cnt = f"{k}/{a.replicas} replicas"
            best = min(best, T)
            print(f"τ={tau:>7,}: {cnt:>22}  TTS99 = {T:.3g} trials", flush=True)
    print(f"best TTS99 = {best:.3g} trials")


if __name__ == "__main__":
    main()
