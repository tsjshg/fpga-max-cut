#!/usr/bin/env python3
"""GPU 版の試行/秒を、レプリカ数を振って測る（docs/gpu.md の「速さ」）。

    python3 gpu/bench_tput.py            → data/gpu/tput.json と表（標準出力）

温度の範囲が同じで長さだけ違う 2 つのスケジュール（100 段 × 1 周回と 100 段 × 10 周回）で走らせ、
時間の差から傾き = 試行/秒を出す（dispatch を送る固定費などを除く）。各点 REPS 回の最短。
問題: n = 1024 のランダム 4 正則（T 0.8 → 0.2、data/std_d4.sched と同じ温度）と G50（T 1.0 → 0.4、M4 と同じ）。
"""
import json
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
SA_GPU = os.path.join(HERE, "sa_gpu")
OUT = os.path.join(ROOT, "data", "gpu")
REPS = 2

CASES = [
    ("rr1024", "data/rr_n1024_d4_s1.adj", 4, 1024, (0.8, 0.2)),
    ("G50", "data/gset/G50.adj", 4, 3000, (1.0, 0.4)),
]
ENGINES = {
    "a": [1024, 2048, 4096, 8192, 16384, 32768, 65536, 131072, 262144],
    "b": [4096, 16384, 32768, 65536, 131072, 262144, 524288, 1048576],
}


def sched(d, T0, T1, sweeps, path):
    subprocess.run([sys.executable, os.path.join(ROOT, "sw", "make_sched.py"), str(d), "--T0", str(T0), "--T1", str(T1),
                    "--stages", "100", "--sweeps", str(sweeps), "-o", path], check=True, capture_output=True)


def run(eng, g, s, R):
    best = None
    for _ in range(REPS):
        r = subprocess.run([SA_GPU, "-e", eng, "-g", g, "-s", s, "-R", str(R), "-q"], check=True,
                           capture_output=True, text=True)
        sec = float(re.search(r"time ([\d.]+) s", r.stderr).group(1))
        best = sec if best is None else min(best, sec)
    return best


def main():
    os.makedirs(OUT, exist_ok=True)
    subprocess.run(["make", "-s", "-C", HERE], check=True)
    res = []
    for name, graph, d, n, (T0, T1) in CASES:
        g = os.path.join(ROOT, graph)
        s1, s10 = os.path.join(OUT, f"{name}_t100.sched"), os.path.join(OUT, f"{name}_t1000.sched")
        sched(d, T0, T1, 1, s1)
        sched(d, T0, T1, 10, s10)
        for eng, Rs in ENGINES.items():
            for R in Rs:
                t1, t10 = run(eng, g, s1, R), run(eng, g, s10, R)
                slope = R * n * 900 / (t10 - t1)
                rec = dict(name=name, n=n, engine=eng, R=R, t100=t1, t1000=t10, slope=slope,
                           direct=R * n * 1000 / t10, step_ns=(t10 - t1) / (900 * n) * 1e9)
                res.append(rec)
                print(f"{name} {eng} R={R}: τ=100 {t1:.3f} s, τ=1000 {t10:.3f} s, 傾き {slope:.3g} 試行/秒"
                      f"（τ=1000 だけなら {rec['direct']:.3g}）、1 頂点 {rec['step_ns']:.0f} ns", flush=True)
    with open(os.path.join(OUT, "tput.json"), "w") as f:
        json.dump(res, f, indent=1)


if __name__ == "__main__":
    main()
