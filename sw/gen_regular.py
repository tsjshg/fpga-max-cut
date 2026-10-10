#!/usr/bin/env python3
"""ランダム d 正則グラフ（自己ループ・多重辺なし）を作って隣接リストの形で書き出す。

組み合わせモデル（各頂点に d 個の「手」を出し、ランダムに対にする）で作り、
自己ループか多重辺ができたら最初からやり直す。d = 4 なら平均 40 回ほどで通る
（成功確率 ≒ exp(-(d^2-1)/4)）。やり直すので、出てくるグラフは一様分布になる。

書き出す形（.adj）:
    # コメント行（先頭が #）
    n d
    u_1 u_2 ... u_d        ← 頂点 0 の隣（昇順）
    ...                    ← n 行
"""
import argparse
import random
import sys


def random_regular(n, d, seed):
    if n * d % 2:
        raise ValueError("n*d は偶数でなければならない")
    if d >= n:
        raise ValueError("d < n でなければならない")
    rng = random.Random(seed)
    points = [v for v in range(n) for _ in range(d)]
    for tries in range(1, 100000):
        rng.shuffle(points)
        adj = [set() for _ in range(n)]
        ok = True
        for i in range(0, len(points), 2):
            a, b = points[i], points[i + 1]
            if a == b or b in adj[a]:
                ok = False
                break
            adj[a].add(b)
            adj[b].add(a)
        if ok:
            return [sorted(s) for s in adj], tries
    raise RuntimeError("やり直しの回数が上限に達した")


def write_adj(f, adj, d, comment):
    f.write(f"# {comment}\n")
    f.write(f"{len(adj)} {d}\n")
    for nb in adj:
        f.write(" ".join(map(str, nb)) + "\n")


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("n", type=int)
    ap.add_argument("d", type=int)
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("-o", "--out", help="書き出し先（省略時は標準出力）")
    a = ap.parse_args()
    adj, tries = random_regular(a.n, a.d, a.seed)
    comment = f"random {a.d}-regular graph n={a.n} seed={a.seed} (gen_regular.py, {tries} tries)"
    if a.out:
        with open(a.out, "w") as f:
            write_adj(f, adj, a.d, comment)
    else:
        write_adj(sys.stdout, adj, a.d, comment)
    print(f"n={a.n} d={a.d} seed={a.seed}: {tries} tries", file=sys.stderr)


if __name__ == "__main__":
    main()
