#!/usr/bin/env python3
"""M5 の Mac 側: レプリカ交換法の到達の割合を大きな CPU の実行で数え、FPGA のジョブと期待値を作る。

    python3 sw/m5_bench.py      → data/m5/{m5.json, jobs_k48/<名前>/{in,exp}.bin}

系（16 レプリカ）のどれかが目標に届いた割合 P をアルゴリズムの性質として 256 系で数える。
FPGA（K = 48）の 48 系は、その先頭と同じもの（ビット単位で同じ探索。ここで確かめる）。
焼きなましの 5876 の到達の割合も、M4 と同じ 10,240 レプリカで数える（M4 の m4.json には無いため）。
"""
import json
import os
import re
import subprocess
import sys
import tempfile

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import job  # noqa: E402

OUT = os.path.join(ROOT, "data", "m5")
PT = os.path.join(HERE, "pt")
SA_FAST = os.path.join(HERE, "sa_fast")
SYSTEMS, K, P, SEED = 256, 48, 16, 1   # K = 48: 交換法つきの回路が入るレーンの数
GRAPH = "data/gset/G50.adj"
LADDER = (0.35, 1.0)
TARGETS = [5868, 5872, 5876]
TAUS = [3000, 5000, 10000, 20000, 30000, 100000]


def main():
    os.makedirs(os.path.join(OUT, "jobs_k48"), exist_ok=True)
    subprocess.run(["make", "-s", "-C", HERE, "pt", "sa_fast"], check=True)
    g = os.path.join(ROOT, GRAPH)
    n, d, _ = job.read_graph(g)
    lad = os.path.join(OUT, "G50.ladder")
    subprocess.run([sys.executable, os.path.join(HERE, "make_ladder.py"), str(d), "--Tmin", str(LADDER[0]),
                    "--Tmax", str(LADDER[1]), "--M", str(P), "-o", lad], check=True, capture_output=True)
    results = []
    for tau in TAUS:
        r = subprocess.run([PT, "-g", g, "-l", lad, "-w", str(tau), "-R", str(SYSTEMS), "-S", str(SEED), "-e", "1",
                            "--lag", "1", "-j", "10"] + sum((["-t", str(t)] for t in TARGETS), []),
                           check=True, capture_output=True, text=True)
        sec = float(re.search(r"time ([\d.]+) s", r.stderr).group(1))
        lines = [ln.split() for ln in r.stdout.splitlines() if ln and ln[0].isdigit()]
        assert len(lines) == SYSTEMS * P
        best = [int(f[3]) for f in lines]
        sys_hit = {t: [any(int(lines[L * P + j][6 + i]) >= 0 for j in range(P)) for L in range(SYSTEMS)]
                   for i, t in enumerate(TARGETS)}
        name = f"G50pt_t{tau}"
        jd = os.path.join(OUT, "jobs_k48", name)
        os.makedirs(jd, exist_ok=True)
        with tempfile.TemporaryDirectory() as tmp:
            inp, exp, reps = job.make_job_pt(PT, g, lad, K, P, SEED, 5872, tau, tmp, ("-j", "10"))
        assert [rp["best"] for rp in reps] == best[:K * P], "FPGA 用の実行と大きな実行が食い違う"
        np.array(inp, dtype=np.uint64).tofile(os.path.join(jd, "in.bin"))
        np.array(exp, dtype=np.uint64).tofile(os.path.join(jd, "exp.bin"))
        rec = dict(name=name, tau=tau, n=n, systems=SYSTEMS, cpu_sec=sec,
                   sys_hit={str(t): v for t, v in sys_hit.items()})
        results.append(rec)
        print(f"{name}: " + ", ".join(f"{t}: {sum(v)}/{SYSTEMS} systems" for t, v in sys_hit.items()), flush=True)
    # 焼きなましの 5876（M4 の m4.json に無い目標）
    sp = os.path.join(ROOT, "data", "m4", "G50_t100000.sched")
    r = subprocess.run([SA_FAST, "-g", g, "-s", sp, "-R", "10240", "-S", str(SEED), "-j", "10", "-q", "-t", "5876"],
                       check=True, capture_output=True, text=True)
    k = int(re.search(r"target 5876: (\d+)/", r.stdout).group(1))
    print(f"SA τ=100000: 5876: {k}/10240 replicas")
    with open(os.path.join(OUT, "m5.json"), "w") as f:
        json.dump(dict(pt=results, sa_5876_tau100000=k), f)


if __name__ == "__main__":
    main()
