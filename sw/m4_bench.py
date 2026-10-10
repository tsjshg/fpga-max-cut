#!/usr/bin/env python3
"""M4（到達時間の比較）の Mac 側: スケジュールを作り、M4 で sa_fast を測り、FPGA のジョブと期待値を作る。

    python3 sw/m4_bench.py            → data/m4/ に .sched・m4.json・jobs/<名前>/{in,exp}.bin

どの機械も同じ種から同じ探索をする（ビット単位で一致）ので、目標に届く割合 p はアルゴリズムの性質。
p は M4 の大きな実行（R_M4 レプリカ）で数える。FPGA（R = 16·K）と A9（R = 256）のレプリカは、その先頭と同じもの。
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

SA_FAST = os.path.join(HERE, "sa_fast")
OUT = os.path.join(ROOT, "data", "m4")
R_M4, THREADS_M4 = 10240, 10
K_FPGA, P = 56, 16
SEED = 1

# (名前, グラフ, (T0, T1), 目標（先頭が FPGA のジョブの目標）, 周回数の合計 τ の候補)
CASES = [
    ("rr1024", "data/rr_n1024_d4_s1.adj", (0.8, 0.2), [1774, 1766], [100, 300, 1000, 3000, 10000]),
    ("G48", "data/gset/G48.adj", (1.0, 0.4), [6000], [100, 300, 1000, 3000, 10000]),
    ("G49", "data/gset/G49.adj", (1.0, 0.4), [6000], [100, 300, 1000, 3000, 10000]),
    ("G50", "data/gset/G50.adj", (1.0, 0.4), [5868, 5872], [3000, 10000, 30000, 100000]),
]
STAGES = 100


def sched_path(name, tau):
    return os.path.join(OUT, f"{name}_t{tau}.sched")


def main():
    os.makedirs(os.path.join(OUT, "jobs"), exist_ok=True)
    subprocess.run(["make", "-s", "-C", HERE, "sa_fast"], check=True)
    results = []
    for name, graph, (T0, T1), targets, taus in CASES:
        g = os.path.join(ROOT, graph)
        n, d, _ = job.read_graph(g)
        for tau in taus:
            sp = sched_path(name, tau)
            subprocess.run([sys.executable, os.path.join(HERE, "make_sched.py"), str(d), "--T0", str(T0),
                            "--T1", str(T1), "--stages", str(STAGES), "--sweeps", str(tau // STAGES),
                            "-o", sp], check=True)
            # M4: 大きな実行で p を数え、時間を測る（sa_fast が出す計算部分の時間）
            r = subprocess.run([SA_FAST, "-g", g, "-s", sp, "-R", str(R_M4), "-S", str(SEED),
                                "-j", str(THREADS_M4)] + sum((["-t", str(t)] for t in sorted(targets)), []),
                               check=True, capture_output=True, text=True)
            sec = float(re.search(r"time ([\d.]+) s", r.stderr).group(1))
            lines = [ln.split() for ln in r.stdout.splitlines() if ln and ln[0].isdigit()]
            assert len(lines) == R_M4
            best = [int(f[3]) for f in lines]
            hits = {t: [int(f[6 + j]) for f in lines] for j, t in enumerate(sorted(targets))}
            # FPGA のジョブと期待値（同じ種の先頭 16·K レプリカ）
            jd = os.path.join(OUT, "jobs", f"{name}_t{tau}")
            os.makedirs(jd, exist_ok=True)
            with tempfile.TemporaryDirectory() as tmp:
                inp, exp, reps = job.make_job(SA_FAST, g, sp, K_FPGA, P, SEED, targets[0], tmp, ("-j", str(THREADS_M4)))
            np.array(inp, dtype=np.uint64).tofile(os.path.join(jd, "in.bin"))
            np.array(exp, dtype=np.uint64).tofile(os.path.join(jd, "exp.bin"))
            # 先頭 896 個は M4 の大きな実行と同じレプリカのはず（ビット単位で同じ探索）
            assert [rp["best"] for rp in reps] == best[:K_FPGA * P], "FPGA 用の実行と M4 の大きな実行が食い違う"
            rec = dict(name=name, graph=graph, n=n, T0=T0, T1=T1, tau=tau, R_m4=R_M4, m4_sec=sec,
                       best=best, hits={str(t): v for t, v in hits.items()}, targets=targets)
            results.append(rec)
            ps = ", ".join(f"{t}: {sum(1 for h in hits[t] if h >= 0)}/{R_M4}" for t in targets)
            print(f"{name} τ={tau}: M4 {sec:.3f} s ({R_M4 * n * tau / sec:.3g} trials/s), hits {ps}", flush=True)
    with open(os.path.join(OUT, "m4.json"), "w") as f:
        json.dump(results, f)


if __name__ == "__main__":
    main()
