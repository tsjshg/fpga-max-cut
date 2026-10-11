#!/usr/bin/env python3
"""M5 の集計: G50 で、FPGA のレプリカ交換法（K = 48）と、焼きなまし（FPGA K = 56・M4）の TTS99 を比べる。

    python3 sw/m5_report.py

到達の割合はアルゴリズムの性質として大きな CPU の実行で数えたもの（交換法 256 系、焼きなまし 10,240 レプリカ）。
時間は各機械で測ったもの（FPGA は DMA 込み、M4 は 1,280 レプリカの sa_fast）。TTS の式は m4_report.py と同じ。
"""
import json
import math
import os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def tts(t, p, R):
    if p <= 0:
        return math.inf
    P = 1 - (1 - p) ** R
    return t if P >= 0.99 else t * math.log(0.01) / math.log(1 - P)


def main():
    m5 = json.load(open(os.path.join(ROOT, "data/m5/m5.json")))
    m4 = json.load(open(os.path.join(ROOT, "data/m4/m4.json")))
    fpt = {json.loads(l)["name"]: json.loads(l) for l in open(os.path.join(ROOT, "data/m5/fpga_pt.jsonl"))}
    fsa = {json.loads(l)["name"]: json.loads(l) for l in open(os.path.join(ROOT, "data/m4/fpga.jsonl"))}
    m4t = {}
    for line in open(os.path.join(ROOT, "data/m4/m4_r1280.txt")):
        f = line.split()
        m4t[f"{f[0]}_t{f[1]}"] = float(f[2])
    rows = []
    for target in (5868, 5872, 5876):
        # 交換法（FPGA・48 系）
        pt = []
        for r in sorted(m5["pt"], key=lambda r: r["tau"]):
            P = sum(r["sys_hit"][str(target)]) / r["systems"]
            t = fpt[r["name"]]["wall"]
            pt.append((tts(t, P, 48), r["tau"], P))
        # 焼きなまし（FPGA 896 レプリカ・M4 1,280 レプリカ）
        fs, ms = [], []
        for r in m4:
            if r["name"] != "G50":
                continue
            key = f"G50_t{r['tau']}"
            if str(target) in r["hits"]:
                p = sum(1 for x in r["hits"][str(target)] if x >= 0) / r["R_m4"]
            elif target == 5876 and r["tau"] == 100000:
                p = m5["sa_5876_tau100000"] / 10240
            else:
                continue
            fs.append((tts(fsa[key]["wall"], p, 896), r["tau"], p))
            ms.append((tts(m4t[key], p, 1280), r["tau"], p))
        rows.append((target, min(pt), min(fs), min(ms)))
    print("| 目標 | FPGA 交換法（K = 48） | FPGA 焼きなまし（K = 56） | M4 焼きなまし（10 コア） | 交換法 / 焼きなまし（FPGA） | FPGA 交換法 / M4 焼きなまし |")
    print("|---:|---:|---:|---:|---:|---:|")
    for target, a, b, c in rows:
        print(f"| {target} | {a[0]:.2f} s（τ={a[1]:,}） | {b[0]:.2f} s（τ={b[1]:,}） | {c[0]:.2f} s（τ={c[1]:,}） | "
              f"{b[0] / a[0]:.1f} 倍速い | {a[0] / c[0]:.2f} |")


if __name__ == "__main__":
    main()
