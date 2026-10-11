#!/usr/bin/env python3
"""GPU 版の到達時間（TTS99）。docs/m4.md と同じ式・同じ問題・同じスケジュール（data/m4/*.sched）。

    python3 gpu/bench_tts.py            → data/gpu/tts.json と表（標準出力）

TTS99 = t · ln(0.01) / ln(1 − P)、P = 1 − (1 − p)^R（P ≥ 0.99 なら t）。p は M4 の大きな実行（10,240 レプリカ、
data/m4/m4.json）の値をそのまま使う（どの機械も同じ種から同じ探索をするので、アルゴリズムの性質）。
GPU は (エンジン, R, τ) を選べる。手順:
  1. 較正: 問題ごとに、温度の並びが同じ短いスケジュール（100 段 × 3 周回）で (エンジン, R) ごとの
     1 回の時間を測り、t(τ) = t(300) · τ / 300 で各 τ の 1 回の時間を見積もる（試算）
  2. 目標・τ・エンジンごとに、見積もりの TTS99 が最小の R を選び、その R で実際のスケジュール（data/m4）を
     走らせて 1 回の時間を測る（実測）。到達数は先頭 10,240 レプリカについて m4.json と同じはず（確かめる）
  3. 実測の時間で TTS99 を出し、目標ごとに最良のものを選ぶ
"""
import json
import math
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
SA_GPU = os.path.join(HERE, "sa_gpu")
OUT = os.path.join(ROOT, "data", "gpu")
M4 = os.path.join(ROOT, "data", "m4")

GRAPHS = {"rr1024": "data/rr_n1024_d4_s1.adj", "G48": "data/gset/G48.adj", "G49": "data/gset/G49.adj",
          "G50": "data/gset/G50.adj"}
TEMPS = {"rr1024": (0.8, 0.2), "G48": (1.0, 0.4), "G49": (1.0, 0.4), "G50": (1.0, 0.4)}
RS = {"a": [32, 64, 128, 256, 512, 1024, 2048, 4096, 8192, 16384, 32768, 65536],
      "b": [1024, 4096, 16384, 32768, 65536, 131072, 262144]}
MAX_RUN = 600.0  # これより長く見積もられる実行はしない（秒）


def tts(t, p, R):
    if p <= 0:
        return math.inf
    P = 1 - (1 - p) ** R
    return t if P >= 0.99 else t * math.log(0.01) / math.log(1 - P)


def run(eng, g, s, R, targets=()):
    cmd = [SA_GPU, "-e", eng, "-g", g, "-s", s, "-R", str(R)] + sum((["-t", str(t)] for t in targets), [])
    r = subprocess.run(cmd, check=True, capture_output=True, text=True)
    sec = float(re.search(r"time ([\d.]+) s", r.stderr).group(1))
    rows = [ln.split() for ln in r.stdout.splitlines() if ln and ln[0].isdigit()]
    return sec, rows


def main():
    os.makedirs(OUT, exist_ok=True)
    subprocess.run(["make", "-s", "-C", HERE], check=True)
    m4 = json.load(open(os.path.join(M4, "m4.json")))
    names = sorted({r["name"] for r in m4}, key=list(GRAPHS).index)

    # 1. 較正
    cal = {}
    for name in names:
        g = os.path.join(ROOT, GRAPHS[name])
        T0, T1 = TEMPS[name]
        s = os.path.join(OUT, f"{name}_cal300.sched")
        subprocess.run([sys.executable, os.path.join(ROOT, "sw", "make_sched.py"), "4", "--T0", str(T0), "--T1", str(T1),
                        "--stages", "100", "--sweeps", "3", "-o", s], check=True, capture_output=True)
        for eng, Rs in RS.items():
            for R in Rs:
                sec = min(run(eng, g, s, R)[0] for _ in range(2))
                cal[(name, eng, R)] = sec
                print(f"較正 {name} {eng} R={R}: τ=300 で {sec:.3f} s", flush=True)

    # 2. 目標・τ・エンジンごとに見積もりが最小の R を選ぶ
    plan = {}  # (name, tau, eng, R) → 走らせる
    est = []
    for rec in m4:
        name, tau = rec["name"], rec["tau"]
        for t in rec["targets"]:
            p = sum(1 for h in rec["hits"][str(t)] if h >= 0) / rec["R_m4"]
            for eng, Rs in RS.items():
                cands = [(tts(cal[(name, eng, R)] * tau / 300, p, R), R) for R in Rs]
                e, R = min(cands)
                est.append(dict(name=name, target=t, tau=tau, engine=eng, R=R, p=p, tts_est=e,
                                t_est=cal[(name, eng, R)] * tau / 300))
                if cal[(name, eng, R)] * tau / 300 <= MAX_RUN and not math.isinf(e):
                    plan[(name, tau, eng, R)] = rec

    # 3. 実測
    meas = []
    for (name, tau, eng, R), rec in sorted(plan.items(), key=lambda kv: (names.index(kv[0][0]), kv[0][1], kv[0][2])):
        g = os.path.join(ROOT, GRAPHS[name])
        s = os.path.join(M4, f"{name}_t{tau}.sched")
        targets = sorted(rec["targets"])
        sec, rows = run(eng, g, s, R, targets)
        ok = True
        for j, t in enumerate(targets):
            mine = [int(f[6 + j]) for f in rows[:rec["R_m4"]]]
            ref = rec["hits"][str(t)][:len(mine)]
            ok &= mine == ref
        best_ok = [int(f[3]) for f in rows[:rec["R_m4"]]] == rec["best"][:min(R, rec["R_m4"])]
        for t in rec["targets"]:
            p = sum(1 for h in rec["hits"][str(t)] if h >= 0) / rec["R_m4"]
            meas.append(dict(name=name, target=t, tau=tau, engine=eng, R=R, p=p, t=sec, tts=tts(sec, p, R),
                             match=bool(ok and best_ok)))
        print(f"実測 {name} τ={tau} {eng} R={R}: {sec:.3f} s、M4 の実行と{'一致' if ok and best_ok else '食い違い'}",
              flush=True)

    with open(os.path.join(OUT, "tts.json"), "w") as f:
        json.dump(dict(cal=[dict(name=k[0], engine=k[1], R=k[2], t300=v) for k, v in cal.items()], est=est,
                       meas=meas), f, indent=1)


if __name__ == "__main__":
    main()
