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


def pack_input(n, d, P, adj, rows, target, seeds, c0s, init_words):
    assert d <= 4 and all(len(nb) == d for nb in adj)
    beats = [n | (d << 16) | (P << 24) | (len(rows) << 32), target]
    for nb in adj:
        w = 0
        for i, u in enumerate(nb):
            w |= u << (16 * i)
        beats.append(w)
    beats += init_words
    beats += seeds
    beats += c0s
    for sweeps, th in rows:
        assert sweeps > 0
        beats.append(sweeps)
        beats.append(th[0] | (th[1] << 32))
    return beats


def expected_output(n, d, P, rows, reps, final_words):
    WT = sum(m for m, _ in rows) * n
    beats = [n | (d << 16) | (P << 24) | (len(rows) << 32) | (MAGIC << 48),
             (WT + 1) * P,          # 走ったサイクル数（設計値。docs/m1-design.md）
             WT]
    for r in reps:
        beats.append(r["best"] | (r["final"] << 16))
        beats.append(NONE48 if r["best_t"] < 0 else r["best_t"])
        beats.append(NONE48 if r["hit_t"] < 0 else r["hit_t"])
    beats += final_words
    return beats


def make_job(sa, graph, sched, K, P, seed, target, tmpdir):
    """(入力のビート列, 期待する出力のビート列, レプリカの結果) を返す。"""
    n, d, adj = read_graph(graph)
    sd, rows = read_sched(sched)
    assert sd == d
    R = K * P
    reps, cols0, cols1 = run_sa(sa, graph, sched, R, seed, target, tmpdir)
    inp = pack_input(n, d, P, adj, rows, target, [r["seed"] for r in reps], [r["c0"] for r in reps],
                     color_words(cols0, n))
    exp = expected_output(n, d, P, rows, reps, color_words(cols1, n))
    return inp, exp, reps
