#!/usr/bin/env python3
"""xsim 用の照合ベクタを作る: hw/sim/vec_k<K>/{in.hex, exp.hex, params.vh}

期待値は CPU 版 sw/sa.c が出す。ジョブを何本か続けて流し、回路が次のジョブへ正しく戻るかも見る。
1 行 = {TLAST, 64 bit のビート} を 17 桁の16進で。

使い方: mkvec.py --K 1
"""
import argparse
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import job  # noqa: E402

# (グラフ, (T0, T1, 段数, 段あたりの周回数), 主種, 目標)
JOBS = [
    ("data/circ_n64_d4.adj",  (1.0, 0.2, 5, 3),  11,   0),    # 隣に v-1・v-2 が必ずいる。目標 0 は「最初の改善で到達」
    ("data/rr_n64_d4_s1.adj", (3.0, 0.3, 20, 2), 7,    106),
    ("data/rr_n256_d4_s1.adj", (0.8, 0.2, 10, 3), 2026, 420),
    ("data/rr_n100_d4_s1.adj", (2.0, 2.0, 1, 5),  5,    140),  # n が 2 のべきでない・段が 1 つ
]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--K", type=int, default=1)
    ap.add_argument("--P", type=int, default=16)
    a = ap.parse_args()
    sa = os.path.join(HERE, "sa")
    subprocess.run(["make", "-s", "-C", HERE, "sa"], check=True)
    outdir = os.path.join(ROOT, "hw", "sim", f"vec_k{a.K}")
    os.makedirs(outdir, exist_ok=True)
    inp, exp = [], []
    with tempfile.TemporaryDirectory() as tmp:
        for graph, (T0, T1, S, m), seed, target in JOBS:
            sched = os.path.join(tmp, "s.sched")
            n, d, _ = job.read_graph(os.path.join(ROOT, graph))
            subprocess.run([sys.executable, os.path.join(HERE, "make_sched.py"), str(d), "--T0", str(T0),
                            "--T1", str(T1), "--stages", str(S), "--sweeps", str(m), "-o", sched], check=True)
            i, e, reps = job.make_job(sa, os.path.join(ROOT, graph), sched, a.K, a.P, seed, target, tmp)
            inp += [(w, k == len(i) - 1) for k, w in enumerate(i)]
            exp += [(w, k == len(e) - 1) for k, w in enumerate(e)]
            hits = sum(r["hit_t"] >= 0 for r in reps)
            imps = sum(r["best_t"] >= 0 for r in reps)
            print(f"{graph}: n={n} in {len(i)} beats, out {len(e)} beats, "
                  f"windows {e[2]}, improved {imps}/{len(reps)}, hit {hits}/{len(reps)}")
    for name, beats in (("in.hex", inp), ("exp.hex", exp)):
        with open(os.path.join(outdir, name), "w") as f:
            for w, last in beats:
                f.write(f"{(int(last) << 64) | w:017x}\n")
    with open(os.path.join(outdir, "params.vh"), "w") as f:
        kw = max(1, (a.K - 1).bit_length())
        f.write(f"`define K_LANES {a.K}\n`define KW_LANES {kw}\n`define NIN {len(inp)}\n`define NEXP {len(exp)}\n"
                f"`define NJOBS {len(JOBS)}\n"
                f"`define IN_HEX \"../vec_k{a.K}/in.hex\"\n`define EXP_HEX \"../vec_k{a.K}/exp.hex\"\n")
    print(f"→ {outdir}: {len(inp)} in, {len(exp)} out")


if __name__ == "__main__":
    main()
