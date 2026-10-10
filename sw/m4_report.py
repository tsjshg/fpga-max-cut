#!/usr/bin/env python3
"""M4 の集計: 3 つの機械の 1 回の実行時間と、目標に届く割合 p から TTS99 を出す。

    python3 sw/m4_report.py data/m4/m4.json data/m4/fpga.jsonl data/m4/a9.txt data/m4/m4_r1280.txt

機械を「R 個のレプリカを同時に走らせる1台」とみなす。1 回（長さ τ 周回）の実行時間を t、
1 レプリカが届く割合を p とすると、1 回で少なくとも1つ届く確率は P = 1 − (1 − p)^R で、
    TTS99 = t · ln(0.01) / ln(1 − P)     （P ≥ 0.99 なら t）
p はアルゴリズムの性質（全機械で同じ探索）なので、M4 の大きな実行（10,240 レプリカ）で数えたものを全機械に使う。
τ は機械ごとに TTS99 が最小になるものを選ぶ。
M4 の時間は 1,280 レプリカ（10 スレッド × ビットスライスの 128）で測ったものを使う。CPU はレプリカを減らせば
1 回の時間が縮むので、10,240 のまま比べると「1 回の時間」が下限になって不利になるため。
FPGA は 896 レプリカ（K = 56 で固定）、A9 は 256（2 スレッド × 128）。
"""
import json
import math
import sys
from collections import defaultdict


def tts(t, p, R):
    if p <= 0:
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
    m4 = json.load(open(sys.argv[1]))
    fpga = {}
    for line in open(sys.argv[2]):
        r = json.loads(line)
        fpga.setdefault(r["name"], []).append(r)
    def times(path):
        out = {}
        for line in open(path):
            f = line.split()
            if len(f) >= 3:
                out[f"{f[0]}_t{f[1]}"] = float(f[2])
        return out
    a9 = times(sys.argv[3])
    m4t = times(sys.argv[4])

    R_FPGA, R_A9, R_M4 = 896, 256, 1280
    rows = defaultdict(list)
    for rec in m4:
        key = f"{rec['name']}_t{rec['tau']}"
        fr = fpga.get(key, [])
        assert all(r["bad"] == 0 for r in fr), f"{key}: FPGA の出力が期待値と違う"
        t_fpga = min(r["wall"] for r in fr) if fr else math.nan
        t_circ = min(r["cycles"] for r in fr) / (fr[0]["fclk_mhz"] * 1e6) if fr else math.nan
        for t in rec["targets"]:
            h = rec["hits"][str(t)]
            k = sum(1 for x in h if x >= 0)
            p = k / rec["R_m4"]
            k896 = sum(1 for x in h[:R_FPGA] if x >= 0)
            rows[(rec["name"], t)].append(dict(
                tau=rec["tau"], k=k, R=rec["R_m4"], p=p, k896=k896,
                t_m4=m4t[key], t_fpga=t_fpga, t_circ=t_circ, t_a9=a9.get(key, math.nan),
                tts_m4=tts(m4t[key], p, R_M4),
                tts_fpga=tts(t_fpga, p, R_FPGA), tts_circ=tts(t_circ, p, R_FPGA),
                tts_a9=tts(a9.get(key, math.nan), p, R_A9),
                n=rec["n"]))

    print("## τ ごとの値（実測）\n")
    print("| 問題 | 目標 | τ（周回） | 届いた数 / 10,240 | p | 1 回の時間: FPGA 896（DMA 込み） | M4 1,280 | A9 256 | TTS99: FPGA | M4 | A9 |")
    print("|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|")
    for (name, t), rs in rows.items():
        for r in rs:
            print(f"| {name} | {t} | {r['tau']:,} | {r['k']:,} | {r['p']:.4f} | {fmt(r['t_fpga'])} | {fmt(r['t_m4'])} | "
                  f"{fmt(r['t_a9'])} | {fmt(r['tts_fpga'])} | {fmt(r['tts_m4'])} | {fmt(r['tts_a9'])} |")
    print("\n## 機械ごとの最良の TTS99（τ を機械ごとに選ぶ）\n")
    print("| 問題 | 目標 | FPGA K=56（DMA 込み） | FPGA（回路の時間だけ） | M4 10 コア | A9 2 コア | FPGA / M4 | A9 / FPGA |")
    print("|---|---:|---:|---:|---:|---:|---:|---:|")
    for (name, t), rs in rows.items():
        def best(key):
            r = min(rs, key=lambda r: r[key])
            return r[key], r["tau"]
        f, ft = best("tts_fpga")
        c, _ = best("tts_circ")
        m, mt = best("tts_m4")
        a, at = best("tts_a9")
        ratio = f / m if not (math.isinf(f) or math.isinf(m)) else math.nan
        ratio2 = a / f if not (math.isinf(f) or math.isinf(a)) else math.nan
        print(f"| {name} | {t} | {fmt(f)}（τ={ft:,}） | {fmt(c)} | {fmt(m)}（τ={mt:,}） | {fmt(a)}（τ={at:,}） | "
              f"{ratio:.2f} | {ratio2:.0f} |")
    # 試行/秒は、いちばん長い τ の実行で測る（各機械の計算部分の時間。FPGA は DMA 込み）
    big = [r for rs in rows.values() for r in rs]
    def thr(key, R):
        r = max(big, key=lambda r: r["tau"] * r["n"])
        return R * r["n"] * r["tau"] / r[key]
    th = dict(fpga=thr("t_fpga", R_FPGA), m4=thr("t_m4", R_M4), a9=thr("t_a9", R_A9))
    print(f"\n試行/秒（いちばん長い実行から）: FPGA {th['fpga']:.3g}、M4 {th['m4']:.3g}、A9 {th['a9']:.3g}"
          f"（FPGA / M4 = {th['fpga'] / th['m4']:.2f}、FPGA / A9 = {th['fpga'] / th['a9']:.0f}）")
    trials_tts(rows)


def trials_tts(rows):
    """試行回数で数えた TTS99（1 レプリカを続けて走らせたとき。アルゴリズムの性質で、機械によらない）。"""
    print("\n## 試行回数で数えた TTS99（アルゴリズムの性質。機械によらない）\n")
    print("| 問題 | 目標 | 1 レプリカあたりの試行回数 | そのときの τ | p |")
    print("|---|---:|---:|---:|---:|")
    for (name, t), rs in rows.items():
        def T(r):
            if r["p"] <= 0:
                return math.inf
            return r["tau"] * r["n"] * (math.log(0.01) / math.log(1 - r["p"]) if r["p"] < 0.99 else 1)
        best = min(rs, key=T)
        print(f"| {name} | {t} | {T(best):.3g} | {best['tau']:,} | {best['p']:.4f} |")


if __name__ == "__main__":
    main()
