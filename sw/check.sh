#!/bin/bash
# sa.c と sa_check.py（別の書き方）を小さい問題で突き合わせる。全部一致すれば 0 で終わる。
set -euo pipefail
cd "$(dirname "$0")/.."
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
make -s -C sw sa
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
exit $fail
