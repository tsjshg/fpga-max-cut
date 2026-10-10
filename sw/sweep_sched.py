#!/usr/bin/env python3
"""温度スケジュールの初期・最終温度を格子で振って、最良カット値の平均を表にする（M0 の探索用）。

使い方: sweep_sched.py graph.adj --total 1000 [--stages 100] [--R 64] [--T0 ...] [--T1 ...] [--extra '--order rand']
"""
import argparse
import os
import re
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))


def run(graph, d, T0, T1, stages, total, R, seed, extra, jobs):
    with tempfile.NamedTemporaryFile("w", suffix=".sched", delete=False) as f:
        path = f.name
    subprocess.run([sys.executable, os.path.join(HERE, "make_sched.py"), str(d), "--T0", str(T0),
                    "--T1", str(T1), "--stages", str(stages), "--sweeps", str(total // stages),
                    "-o", path], check=True)
    out = subprocess.run([os.path.join(HERE, "sa"), "-g", graph, "-s", path, "-R", str(R),
                          "-S", str(seed), "-j", str(jobs), "-q"] + extra.split(),
                         check=True, capture_output=True, text=True).stdout
    os.unlink(path)
    m = re.search(r"mean=([\d.]+) min=(\d+) max=(\d+)", out)
    return float(m.group(1)), int(m.group(2)), int(m.group(3))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("graph")
    ap.add_argument("--total", type=int, required=True, help="周回数の合計")
    ap.add_argument("--stages", type=int, default=100)
    ap.add_argument("--R", type=int, default=64)
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--T0", type=float, nargs="+", default=[1.0, 1.5, 2.0, 3.0, 5.0])
    ap.add_argument("--T1", type=float, nargs="+", default=[0.1, 0.2, 0.3, 0.5])
    ap.add_argument("--extra", default="")
    ap.add_argument("-j", type=int, default=os.cpu_count())
    a = ap.parse_args()
    with open(a.graph) as f:
        line = f.readline()
        while line.startswith("#"):
            line = f.readline()
        d = int(line.split()[1])
    stages = min(a.stages, a.total)
    print(f"# {a.graph} total={a.total} stages={stages} R={a.R} seed={a.seed} {a.extra}")
    print("T0\\T1  " + "".join(f"{t1:>16}" for t1 in a.T1) + "   （平均 [最小-最大]）")
    for t0 in a.T0:
        row = f"{t0:<7}"
        for t1 in a.T1:
            mean, mn, mx = run(a.graph, d, t0, t1, stages, a.total, a.R, a.seed, a.extra, a.j)
            row += f"{mean:9.2f} [{mn}-{mx}]".rjust(16)
        print(row, flush=True)


if __name__ == "__main__":
    main()
