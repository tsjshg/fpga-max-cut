#!/bin/bash
# M4 のボード側（Cortex-A9）: sa_fast を 2 スレッド・256 レプリカで回して時間を測る。1 実行 1 行。
#   ./a9_bench.sh <sa_fast> <data のフォルダ>     （data/m4/*.sched と、グラフの .adj を置いておく）
# 出力: 名前 τ 秒 （sa_fast が出す計算部分の時間）と、目標ごとの到達数
set -euo pipefail
SA=$1; D=$2
run() {  # 名前 グラフ τ 目標...
    local name=$1 g=$2 tau=$3; shift 3
    local ts=(); for t in "$@"; do ts+=(-t "$t"); done
    out=$("$SA" -g "$D/$g" -s "$D/m4/${name}_t${tau}.sched" -R 256 -S 1 -j 2 -q "${ts[@]}" 2>&1)
    sec=$(sed -n 's/^time \([0-9.]*\) s.*/\1/p' <<<"$out")
    hits=$(sed -n 's/^summary target \([0-9]*\): \([0-9]*\)\/.*/\1:\2/p' <<<"$out" | tr '\n' ' ')
    echo "$name $tau $sec $hits"
}
for tau in 300 1000 3000 10000; do run rr1024 rr_n1024_d4_s1.adj $tau 1766 1774; done
for g in G48 G49; do for tau in 300 1000 3000 10000; do run $g $g.adj $tau 6000; done; done
for tau in 3000 10000 30000 100000; do run G50 G50.adj $tau 5868 5872; done
