#!/bin/bash
# 1) sa.c と sa_check.py（別の書き方）を小さい問題で突き合わせる。
# 2) sa_fast.c の全エンジンと sa.c を突き合わせる（標準出力・--init・--final の塗り分け）。
# 3) pt_fast.c と pt.c を突き合わせる（同じく）。
# 全部一致すれば 0 で終わる。
set -euo pipefail
cd "$(dirname "$0")/.."
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
make -s -C sw sa sa_fast pt pt_fast
fail=0
# グラフ, d, 目標
for case in "rr_n64_d4_s1 4 100 120" "rr_n256_d4_s1 4 400 420" "rr_n64_d3_s1 3 75 80"; do
    read -r g d t1 t2 <<<"$case"
    for sched in "3 0.3 20 2" "1.0 0.2 5 3"; do
        read -r T0 T1 S m <<<"$sched"
        python3 sw/make_sched.py "$d" --T0 "$T0" --T1 "$T1" --stages "$S" --sweeps "$m" -o "$tmp/s"
        sw/sa -g "data/$g.adj" -s "$tmp/s" -R 8 -S 7 -t "$t1" -t "$t2" >"$tmp/c" 2>/dev/null
        python3 sw/sa_check.py "data/$g.adj" "$tmp/s" 8 7 "$t1" "$t2" >"$tmp/py"
        lines=$(wc -l <"$tmp/c")
        if [ "$lines" -lt 10 ]; then echo "NG $g T0=$T0: 出力が短い"; fail=1
        elif diff -q "$tmp/c" "$tmp/py" >/dev/null; then echo "OK $g T0=$T0 T1=$T1 S=$S m=$m（$lines 行）"
        else echo "NG $g T0=$T0"; diff "$tmp/c" "$tmp/py" | head; fail=1; fi
    done
done

# sa_fast: 次数 3・4（特殊化した版）と 5・6（一般の版）、頂点が前の頂点とつながる循環グラフ、
# 64 の倍数でない n。レプリカ数は組の幅（64・128・256）に満たない 8 と、割り切れない 200。
# 目標 0 は「初期のカット値より小さい目標は最初の改善で届く」の確認
python3 sw/gen_regular.py 128 5 --seed 3 -o "$tmp/rr_n128_d5.adj" 2>/dev/null
python3 sw/gen_regular.py 100 6 --seed 4 -o "$tmp/rr_n100_d6.adj" 2>/dev/null
engines=$( (sw/sa_fast 2>&1 || true) | sed -n 's/^  エンジン: *\(.*\)（.*/\1/p')
for case in "data/rr_n256_d4_s1 4 410 420" "data/circ_n64_d4 4 76 86" "data/rr_n100_d4_s1 4 160 170" \
            "data/rr_n64_d3_s1 3 75 80" "data/rr_n1024_d3_s1 3 1200 1260" \
            "$tmp/rr_n128_d5 5 230 250" "$tmp/rr_n100_d6 6 210 230"; do
    read -r g d t1 t2 <<<"$case"
    for sched in "3 0.3 20 2" "0.8 0.2 4 5"; do
        read -r T0 T1 S m <<<"$sched"
        python3 sw/make_sched.py "$d" --T0 "$T0" --T1 "$T1" --stages "$S" --sweeps "$m" -o "$tmp/s"
        for R in 8 200; do
            args=(-g "$g.adj" -s "$tmp/s" -R "$R" -S 11 -t 0 -t "$t1" -t "$t2")
            sw/sa "${args[@]}" -j 2 --init "$tmp/i0" --final "$tmp/f0" >"$tmp/o0" 2>/dev/null
            ng=""
            for e in $engines; do
                sw/sa_fast "${args[@]}" -e "$e" -j 3 --init "$tmp/i1" --final "$tmp/f1" >"$tmp/o1" 2>/dev/null || { ng="$ng $e(失敗)"; continue; }
                cmp -s "$tmp/o0" "$tmp/o1" && cmp -s "$tmp/i0" "$tmp/i1" && cmp -s "$tmp/f0" "$tmp/f1" || ng="$ng $e"
            done
            hits=$(grep "summary target $t2:" "$tmp/o0" | cut -d' ' -f4)
            if [ -n "$ng" ]; then echo "NG sa_fast $(basename "$g") T0=$T0 R=$R:$ng"; fail=1
            else echo "OK sa_fast $(basename "$g") T0=$T0 R=$R 全エンジン（目標 $t2 に $hits）"; fi
        done
    done
done
# pt_fast: グラフは sa_fast と同じ 7 つ。はしごは M = 16（回路と同じ、1 組 8 系）・12（128 を割り切れない）・
# 2・64（1 組の系の数の両端）。系の数は 1 と、組に満たず割り切れない 13。lag 0・1、交換の間隔 1・3
for case in "data/rr_n256_d4_s1 4 410 420" "data/circ_n64_d4 4 76 86" "data/rr_n100_d4_s1 4 160 170" \
            "data/rr_n64_d3_s1 3 75 80" "data/rr_n1024_d3_s1 3 1200 1260" \
            "$tmp/rr_n128_d5 5 230 250" "$tmp/rr_n100_d6 6 210 230"; do
    read -r g d t1 t2 <<<"$case"
    for lad in "0.35 1.0 16" "0.2 3.0 12" "0.3 0.8 2" "0.25 2.0 64"; do
        read -r Tmin Tmax M <<<"$lad"
        python3 sw/make_ladder.py "$d" --Tmin "$Tmin" --Tmax "$Tmax" --M "$M" -o "$tmp/l" 2>/dev/null
        ng=""
        for R in 1 13; do
            for le in "0 1" "1 1" "1 3"; do
                read -r lag e <<<"$le"
                args=(-g "$g.adj" -l "$tmp/l" -w 40 -R "$R" -S 5 -e "$e" --lag "$lag" -t 0 -t "$t1" -t "$t2")
                sw/pt "${args[@]}" -j 2 --init "$tmp/i0" --final "$tmp/f0" >"$tmp/o0" 2>/dev/null
                sw/pt_fast "${args[@]}" -j 3 --init "$tmp/i1" --final "$tmp/f1" >"$tmp/o1" 2>/dev/null || { ng="$ng R=$R,lag=$lag,e=$e(失敗)"; continue; }
                cmp -s "$tmp/o0" "$tmp/o1" && cmp -s "$tmp/i0" "$tmp/i1" && cmp -s "$tmp/f0" "$tmp/f1" || ng="$ng R=$R,lag=$lag,e=$e"
            done
        done
        # 温度が実際に動いたか（最後の温度の番号が最初と違うレプリカの数、R = 13 の最後の実行）
        moved=$(awk '$1 ~ /^[0-9]+$/ && $NF != $1 % '"$M"' {k++} END {print k+0}' "$tmp/o0")
        if [ -n "$ng" ]; then echo "NG pt_fast $(basename "$g") M=$M T=$Tmin〜$Tmax:$ng"; fail=1
        else echo "OK pt_fast $(basename "$g") M=$M T=$Tmin〜$Tmax（R=1・13 × lag/e 3 通り、温度が動いたレプリカ $moved/$((13 * M))）"; fi
    done
done
exit $fail
