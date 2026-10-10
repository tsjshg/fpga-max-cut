"""回路へ渡すジョブのストリームと、回路が返すはずのストリームを作る（xsim とボードで共通）。

形は docs/m1-design.md の「ストリームの形」。1 ビート = 64 bit の整数。
期待値は CPU 版 sw/sa.c の出力から作る（回路とは別の書き方）。
"""
import os
import subprocess

NONE48 = (1 << 48) - 1
MAGIC = 0x4D43


def read_ints(path):
    out = []
    for line in open(path):
        if line.lstrip().startswith("#"):
            continue
        out.extend(int(w) for w in line.split())
    return out


def read_graph(path):
    g = read_ints(path)
    n, d = g[0], g[1]
    return n, d, [g[2 + v * d: 2 + (v + 1) * d] for v in range(n)]


def read_sched(path):
    s = read_ints(path)
    d, S = s[0], s[1]
    rows = []
    for k in range(S):
        row = s[2 + k * (d + 2): 2 + (k + 1) * (d + 2)]
        rows.append((row[0], row[1:]))   # (周回数, TH[0..d])
    return d, rows


def run_sa(sa, graph, sched, R, seed, target, tmpdir, extra=()):
    """sa.c を回して、レプリカごとの結果と初期・最後の塗り分けを返す。"""
    init = os.path.join(tmpdir, "init.txt")
    final = os.path.join(tmpdir, "final.txt")
    out = subprocess.run([sa, "-g", graph, "-s", sched, "-R", str(R), "-S", str(seed),
                          "-t", str(target), "--init", init, "--final", final, *extra],
                         check=True, capture_output=True, text=True).stdout
    reps = []
    for line in out.splitlines():
        if line.startswith("#") or line.startswith("summary"):
            continue
        f = line.split()
        reps.append(dict(p=int(f[0]), seed=int(f[1], 16), c0=int(f[2]), best=int(f[3]),
                         best_t=int(f[4]), final=int(f[5]), hit_t=int(f[6])))
    assert len(reps) == R
    cols0 = [line.strip() for line in open(init)]
    cols1 = [line.strip() for line in open(final)]
    return reps, cols0, cols1


def color_words(cols, n):
    """レプリカごとの塗り分け（'0'/'1' の文字列 × R）を、頂点ごとの語（ビット p = レプリカ p）にする。"""
    words = []
    for v in range(n):
        w = 0
        for p, c in enumerate(cols):
            if c[v] == "1":
                w |= 1 << p
        words.append(w)
    return words


def color_beats(words, R):
    """頂点ごとの語（R bit）を ⌈R/64⌉ ビートずつに分ける（ビート b = ビット 64b..64b+63）。"""
    cb = (R + 63) // 64
    return [(w >> (64 * b)) & ((1 << 64) - 1) for w in words for b in range(cb)]


def pack_input(n, d, P, adj, rows, target, seeds, c0s, init_words):
    assert d <= 4 and all(len(nb) == d for nb in adj)
    beats = [n | (d << 16) | (P << 24) | (len(rows) << 32), target]
    for nb in adj:
        w = 0
        for i, u in enumerate(nb):
            w |= u << (16 * i)
        beats.append(w)
    beats += color_beats(init_words, len(seeds))
    beats += seeds
    beats += c0s
    for sweeps, th in rows:
        assert sweeps > 0
        beats.append(sweeps)
        beats.append(th[0] | (th[1] << 32))
    return beats


def expected_output(n, d, P, rows, reps, final_words):
    WT = sum(m for m, _ in rows) * n
    assert WT < (1 << 32) - 1, "回路の試行番号は 32 bit（全 1 は「無し」）。1 ジョブの窓の数を減らす"
    K = len(reps) // P
    beats = [n | (d << 16) | (P << 24) | (len(rows) << 32) | (MAGIC << 48),
             (WT + 1) * P,          # 走ったサイクル数（設計値。docs/m1-design.md）
             WT | (K << 48)]
    for r in reps:
        beats.append(r["best"] | (r["final"] << 16))
        beats.append(NONE48 if r["best_t"] < 0 else r["best_t"])
        beats.append(NONE48 if r["hit_t"] < 0 else r["hit_t"])
    beats += color_beats(final_words, len(reps))
    return beats


def make_job(sa, graph, sched, K, P, seed, target, tmpdir, extra=()):
    """(入力のビート列, 期待する出力のビート列, レプリカの結果) を返す。extra は sa への追加の引数（-j など）。"""
    n, d, adj = read_graph(graph)
    sd, rows = read_sched(sched)
    assert sd == d
    R = K * P
    reps, cols0, cols1 = run_sa(sa, graph, sched, R, seed, target, tmpdir, extra)
    inp = pack_input(n, d, P, adj, rows, target, [r["seed"] for r in reps], [r["c0"] for r in reps],
                     color_words(cols0, n))
    exp = expected_output(n, d, P, rows, reps, color_words(cols1, n))
    return inp, exp, reps


# ---------------- レプリカ交換法（M5） ----------------
# 入力: 見出し0 の [48] = 1（交換法）、見出し1 = 目標 | 周回数 << 16 | DMAX << 48。
# ADJ・COL・種・C0 は焼きなましと同じ。続けて、レーンごとの交換用の種（K ビート）、
# 温度ごとのしきい値（16 ビート: TH[k][0] | TH[k][1] << 32）、交換の表 SW（k = 0..14、D = 1..DMAX の順に
# 1 ビート 2 つずつ。下位が先、奇数なら最後の上位は 0）。
# 出力: 焼きなましと同じ形で、レプリカの語 A の [35:32] に最後の温度の番号。

GOLDEN = 0x9E3779B97F4A7C15
M64 = (1 << 64) - 1
DMAX_HW = 512   # 回路の SW の表の 1 行の長さ


def _sm64(state):
    state = (state + GOLDEN) & M64
    z = state
    z = ((z ^ (z >> 30)) * 0xBF58476D1CE4E5B9) & M64
    z = ((z ^ (z >> 27)) * 0x94D049BB133111EB) & M64
    return state, z ^ (z >> 31)


def exchange_seed(master, L):
    """pt.c と同じ: 主種から始まる splitmix64 の列の、区間 2^31 + L の最初の 0 でない値。"""
    st = (master + (((0x80000000 + L) << 32) * GOLDEN)) & M64
    while True:
        st, z = _sm64(st)
        if z:
            return z


def read_ladder(path):
    v = read_ints(path)
    d, M, dmax = v[0], v[1], v[2]
    th = [v[3 + k * (d + 1): 3 + (k + 1) * (d + 1)] for k in range(M)]
    o = 3 + M * (d + 1)
    sw = [v[o + k * dmax: o + (k + 1) * dmax] for k in range(M - 1)]
    return d, M, dmax, th, sw


def run_pt(pt, graph, ladder, R_sys, seed, target, sweeps, tmpdir, extra=()):
    init = os.path.join(tmpdir, "init.txt")
    final = os.path.join(tmpdir, "final.txt")
    out = subprocess.run([pt, "-g", graph, "-l", ladder, "-w", str(sweeps), "-R", str(R_sys), "-S", str(seed),
                          "-e", "1", "--lag", "1", "-t", str(target), "--init", init, "--final", final, *extra],
                         check=True, capture_output=True, text=True).stdout
    reps = []
    for line in out.splitlines():
        if line.startswith("#") or line.startswith("summary"):
            continue
        f = line.split()
        reps.append(dict(p=int(f[0]), seed=int(f[1], 16), c0=int(f[2]), best=int(f[3]), best_t=int(f[4]),
                         final=int(f[5]), hit_t=int(f[6]), tidx=int(f[7])))
    return reps, [l.strip() for l in open(init)], [l.strip() for l in open(final)]


def pt_engine_cycles(K, P=16):
    """回路の交換の仕組みが 1 周回ぶんの判断にかかるクロック数（mc_core.v の状態の並びから）。"""
    return K * (1 + 9 * (P // 2) + P + 1)


def pt_engine_ok(n, K, P=16):
    """交換の判断は、周回 s+1 の頂点 0 の窓の o = 4 から始まり、周回 s+1 の終わり（n·P クロック）までに終わる必要がある。"""
    return pt_engine_cycles(K, P) + 4 + 8 < n * P


def make_job_pt(pt, graph, ladder, K, P, seed, target, sweeps, tmpdir, extra=()):
    n, d, adj = read_graph(graph)
    ld, M, dmax, th, sw = read_ladder(ladder)
    assert ld == d and M == P and dmax <= DMAX_HW
    assert pt_engine_ok(n, K, P), f"n = {n} では K = {K} の交換の判断が 1 周回に収まらない"
    assert sweeps * n < (1 << 32) - 1
    reps, cols0, cols1 = run_pt(pt, graph, ladder, K, seed, target, sweeps, tmpdir, extra)
    R = K * P
    assert len(reps) == R
    beats = [n | (d << 16) | (P << 24) | (1 << 48), target | (sweeps << 16) | (dmax << 48)]
    for nb in adj:
        w = 0
        for i, u in enumerate(nb):
            w |= u << (16 * i)
        beats.append(w)
    beats += color_beats(color_words(cols0, n), R)
    beats += [r["seed"] for r in reps]
    beats += [r["c0"] for r in reps]
    beats += [exchange_seed(seed, L) for L in range(K)]
    beats += [row[0] | (row[1] << 32) for row in th]
    for row in sw:   # 1 行 512 個にそろえる（回路は番地 {k, (D−1)/2} で引く）
        row = row + [0] * (DMAX_HW - len(row))
        beats += [row[i] | (row[i + 1] << 32) for i in range(0, DMAX_HW, 2)]
    WT = sweeps * n
    exp = [n | (d << 16) | (P << 24) | (MAGIC << 48), (WT + 1) * P, WT | (K << 48)]
    for r in reps:
        exp.append(r["best"] | (r["final"] << 16) | (r["tidx"] << 32))
        exp.append(NONE48 if r["best_t"] < 0 else r["best_t"])
        exp.append(NONE48 if r["hit_t"] < 0 else r["hit_t"])
    exp += color_beats(color_words(cols1, n), R)
    return beats, exp, reps
