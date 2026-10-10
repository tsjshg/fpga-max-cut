#!/bin/bash
# G-set（Helmberg–Rendl の Max-Cut の問題集）から、このプロジェクトで使うものを取ってきて .adj に直す。
#   sw/get_gset.sh
# 入手先は Yinyu Ye の Stanford のページ。配布の条件が書かれていないので、ファイルそのものは
# リポジトリに入れず（data/gset/ は .gitignore）、ここに取得の手順と SHA-256 だけを置く。
set -euo pipefail
cd "$(dirname "$0")/.."
URL=https://web.stanford.edu/~yyye/yyye/Gset
mkdir -p data/gset
while read -r sum name; do
    f=data/gset/$name
    [ -f "$f" ] || curl -sSf --max-time 60 -o "$f" "$URL/$name"
    echo "$sum  $f" | shasum -a 256 -c --quiet || { echo "SHA-256 が違う: $f" >&2; exit 1; }
    python3 -I sw/gset2adj.py "$f" -o "$f.adj"
done <<'SUMS'
2c2daba39d1f1ff1657e015f58b1a8c1cbef198fa25175c182c50eae40269b6d G48
01733a64e25ea975b8f0f2e02be68e126b2bd8e69d2fe3553742bd2d5d66d4a3 G49
d3f5f31c50898e99e3d12fe4c8b6e4768e16eaa9016bc62fc181cabf7cf55c8b G50
SUMS
