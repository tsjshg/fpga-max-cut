#!/usr/bin/env python3
"""sa.c を別の書き方でなぞり、結果が全部一致するかを確かめる（遅いので小さい問題で使う）。

sa.c とわざと書き方を変えている所:
  - 乱数は Python の多倍長整数を 2^64 で切って作る（C の符号なし整数の桁あふれに頼らない）
  - ΔC は「反転の前後で、v につながる辺のうちカットになっている本数」の差として数える
  - カット値は毎回数え直さず足し込むが、最後に辺の一覧から数え直して突き合わせる
  - 受け入れの判定はしきい値の表の読み方を「ΔC から n_same を逆算」して引く

使い方: sa_check.py graph.adj sched.sched R seed [target ...]
sa.c と同じ形の行を標準出力に出すので、`sa -g … -s … -R … -S … -t …` の出力と diff する。
"""
import sys

M64 = (1 << 64) - 1
GOLDEN = 0x9E3779B97F4A7C15


def ints(path):
    out = []
    for line in open(path):
        if line.lstrip().startswith("#"):
            continue
        out.extend(int(w) for w in line.split())
    return out


def splitmix_stream(state):
    while True:
        state = (state + GOLDEN) & M64
        z = state
        z = ((z ^ (z >> 30)) * 0xBF58476D1CE4E5B9) & M64
        z = ((z ^ (z >> 27)) * 0x94D049BB133111EB) & M64
        yield z ^ (z >> 31)


def xorshift_stream(x):
    while True:
        x ^= (x << 13) & M64
        x ^= x >> 7
        x ^= (x << 17) & M64
        yield x


def main():
    gpath, spath, R, seed = sys.argv[1], sys.argv[2], int(sys.argv[3]), int(sys.argv[4], 0)
    targets = [int(a) for a in sys.argv[5:]]
    g = ints(gpath)
    n, d = g[0], g[1]
    nbrs = [g[2 + v * d: 2 + (v + 1) * d] for v in range(n)]
    edges = {(min(v, u), max(v, u)) for v in range(n) for u in nbrs[v]}
    sc = ints(spath)
    S = sc[1]
    stages = [(sc[2 + k * (d + 2)], sc[3 + k * (d + 2): 2 + (k + 1) * (d + 2)]) for k in range(S)]
    total = sum(m for m, _ in stages)

    print(f"# n={n} d={d} edges={len(edges)} R={R} seed={seed} stages={S} sweeps={total} "
          f"order=seq rng=xs64")
    print("# p seed c0 best best_t final" + "".join(f" hit_t[{t}]" for t in targets))
    bests = []
    hits = [0] * len(targets)
    for p in range(R):
        sm = splitmix_stream((seed + p * (1 << 32) * GOLDEN) & M64)
        xseed = next(sm)
        while xseed == 0:
            xseed = next(sm)
        bits = 0
        for w in range((n + 63) // 64):
            bits |= next(sm) << (64 * w)
        side = [(bits >> v) & 1 for v in range(n)]

        def cut_at(v):
            return sum(1 for u in nbrs[v] if side[u] != side[v])

        c = sum(1 for a, b in edges if side[a] != side[b])
        c0 = best = c
        best_t = -1
        hit = [-1] * len(targets)
        rng = xorshift_stream(xseed)
        t = 0
        for m, th in stages:
            for _ in range(m):
                for v in range(n):
                    r = next(rng) >> 32
                    before = cut_at(v)
                    delta = (d - before) - before        # 反転するとカットと非カットが入れ替わる
                    n_same = (delta + d) // 2
                    if delta >= 0 or r < th[n_same]:
                        side[v] ^= 1
                        c += delta
                        if c > best:
                            best, best_t = c, t
                        for j, tg in enumerate(targets):
                            if hit[j] < 0 and c >= tg:
                                hit[j] = t
                    t += 1
        assert c == sum(1 for a, b in edges if side[a] != side[b]), "カット値の足し込みが合わない"
        bests.append(best)
        for j in range(len(targets)):
            hits[j] += hit[j] >= 0
        print(f"{p} {xseed:016x} {c0} {best} {best_t} {c}" + "".join(f" {h}" for h in hit))
    print(f"summary best: mean={sum(bests) / R:.3f} min={min(bests)} max={max(bests)}")
    for j, tg in enumerate(targets):
        print(f"summary target {tg}: {hits[j]}/{R}")


if __name__ == "__main__":
    main()
