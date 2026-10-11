#!/usr/bin/env python3
"""M5 の M4 側: レプリカ交換法の最適化版 pt_fast を M4 で走らせ、1 回の時間を測る。

    python3 sw/m5_m4pt.py      → data/m5/m4pt_r80.txt（「τ 秒 試行/秒」の行）

M4 の焼きなまし（M4 の 1,280 レプリカ）と同じく、80 系 = 1,280 レプリカ（10 スレッド × 1 組 8 系）を 10 スレッドで。
時間は pt_fast の計算部分（標準エラーの time）。到達の割合は m5_bench.py の 256 系の値を使う（m5_report.py）ので、
ここでは数えない。ただし先頭 80 系の到達が 256 系の実行の先頭と同じこと（同じ探索であること）は確かめる。
"""
import json
import os
import re
import subprocess

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
SYSTEMS, THREADS, SEED, RUNS = 80, 10, 1, 3
TARGETS = [5868, 5872, 5876]


def main():
    subprocess.run(["make", "-s", "-C", HERE, "pt_fast"], check=True)
    g = os.path.join(ROOT, "data/gset/G50.adj")
    lad = os.path.join(ROOT, "data/m5/G50.ladder")
    m5 = json.load(open(os.path.join(ROOT, "data/m5/m5.json")))
    out = []
    for r in sorted(m5["pt"], key=lambda r: r["tau"]):
        tau = r["tau"]
        secs = []
        for _ in range(RUNS if tau < 100000 else 1):
            p = subprocess.run([os.path.join(HERE, "pt_fast"), "-g", g, "-l", lad, "-w", str(tau), "-R", str(SYSTEMS),
                                "-S", str(SEED), "-e", "1", "--lag", "1", "-j", str(THREADS)]
                               + sum((["-t", str(t)] for t in TARGETS), []),
                               check=True, capture_output=True, text=True)
            secs.append(float(re.search(r"time ([\d.]+) s", p.stderr).group(1)))
            lines = [ln.split() for ln in p.stdout.splitlines() if ln and ln[0].isdigit()]
            for i, t in enumerate(TARGETS):
                hit = [any(int(lines[L * 16 + j][6 + i]) >= 0 for j in range(16)) for L in range(SYSTEMS)]
                assert hit == r["sys_hit"][str(t)][:SYSTEMS], f"τ={tau} 目標 {t}: 256 系の実行と食い違う"
        sec = min(secs)
        rate = SYSTEMS * 16 * 3000 * tau / sec
        out.append(f"{tau} {sec:.3f} {rate:.3g}")
        print(out[-1], flush=True)
    with open(os.path.join(ROOT, "data/m5/m4pt_r80.txt"), "w") as f:
        f.write("\n".join(out) + "\n")


if __name__ == "__main__":
    main()
