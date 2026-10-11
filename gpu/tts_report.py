#!/usr/bin/env python3
"""GPU の TTS99 を FPGA・M4 CPU と並べる（docs/gpu.md の表）。

    python3 gpu/tts_report.py   （data/gpu/tts.json・data/m4/{m4.json,fpga.jsonl,m4_r1280.txt} を読む）

FPGA と M4 は sw/m4_report.py と同じ計算（FPGA: 896 レプリカ・DMA 込みの時間、M4: 1,280 レプリカ・10 コア）。
GPU は bench_tts.py の実測（目標・τ・エンジンごとに見積もりが最小の R で走らせたもの）から最良を選ぶ。
"""
import json
import math
import os
from collections import defaultdict

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def tts(t, p, R):
    if p <= 0 or math.isnan(t):
        return math.inf
    P = 1 - (1 - p) ** R
    return t if P >= 0.99 else t * math.log(0.01) / math.log(1 - P)


def fmt(x):
    if math.isinf(x):
        return "—"
    if x >= 100:
        return f"{x:,.0f} s"
    if x >= 1:
        return f"{x:.2f} s"
    return f"{x * 1e3:.1f} ms"


def main():
    m4 = json.load(open(os.path.join(ROOT, "data/m4/m4.json")))
    fpga = defaultdict(list)
    for line in open(os.path.join(ROOT, "data/m4/fpga.jsonl")):
        r = json.loads(line)
        fpga[r["name"]].append(r)
    m4t = {}
    for line in open(os.path.join(ROOT, "data/m4/m4_r1280.txt")):
        f = line.split()
        if len(f) >= 3:
            m4t[f"{f[0]}_t{f[1]}"] = float(f[2])
    gpu = json.load(open(os.path.join(ROOT, "data/gpu/tts.json")))

    best = {}
    for rec in m4:
        key = f"{rec['name']}_t{rec['tau']}"
        t_fpga = min(r["wall"] for r in fpga[key]) if fpga[key] else math.nan
        for t in rec["targets"]:
            p = sum(1 for h in rec["hits"][str(t)] if h >= 0) / rec["R_m4"]
            b = best.setdefault((rec["name"], t), dict(fpga=(math.inf, 0), m4=(math.inf, 0)))
            b["fpga"] = min(b["fpga"], (tts(t_fpga, p, 896), rec["tau"]))
            b["m4"] = min(b["m4"], (tts(m4t[key], p, 1280), rec["tau"]))
    for m in gpu["meas"]:
        if not m["match"]:
            raise SystemExit(f"GPU の実行が M4 と食い違う: {m}")
        b = best[(m["name"], m["target"])]
        k = "gpu_" + m["engine"]
        b[k] = min(b.get(k, (math.inf, 0, 0)), (m["tts"], m["tau"], m["R"]))

    print("| 問題 | 目標 | FPGA K=56（DMA 込み） | M4 CPU 10 コア | GPU a | GPU b | GPU（良いほう） / M4 | GPU / FPGA |")
    print("|---|---:|---:|---:|---:|---:|---:|---:|")
    for (name, t), b in best.items():
        ga, gb = b.get("gpu_a", (math.inf, 0, 0)), b.get("gpu_b", (math.inf, 0, 0))
        g = min(ga[0], gb[0])
        cell = lambda x: f"{fmt(x[0])}（τ={x[1]:,}, R={x[2]:,}）" if not math.isinf(x[0]) else "—"
        print(f"| {name} | {t} | {fmt(b['fpga'][0])}（τ={b['fpga'][1]:,}） | {fmt(b['m4'][0])}（τ={b['m4'][1]:,}） | "
              f"{cell(ga)} | {cell(gb)} | {g / b['m4'][0]:.2f} | {g / b['fpga'][0]:.2f} |")

    print("\n## GPU の実測（目標・τ・エンジンごと。R は見積もりの TTS99 が最小のもの）\n")
    print("| 問題 | 目標 | τ | p | エンジン | R | 1 回の時間 | TTS99 | 見積もり（較正から） |")
    print("|---|---:|---:|---:|---|---:|---:|---:|---:|")
    est = {(e["name"], e["target"], e["tau"], e["engine"]): e for e in gpu["est"]}
    for m in gpu["meas"]:
        e = est[(m["name"], m["target"], m["tau"], m["engine"])]
        if e["R"] != m["R"]:  # ほかの目標のために走らせた実行（同じグラフ・τ）
            continue
        print(f"| {m['name']} | {m['target']} | {m['tau']:,} | {m['p']:.4f} | {m['engine']} | {m['R']:,} | {fmt(m['t'])} | "
              f"{fmt(m['tts'])} | {fmt(e['tts_est'])} |")


if __name__ == "__main__":
    main()
