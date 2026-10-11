#!/bin/bash
# GPU 版（gpu/sa_gpu、エンジン a・b）と sw/sa を突き合わせる（標準出力・--init・--final の塗り分け）。
# グラフは sw/check.sh の sa_fast と同じ 7 つ（d = 3・4・5・6、循環グラフ、64 の倍数でない n）× 2 つのスケジュール。
# レプリカ数は組に満たない 8・割り切れない 200・threadgroup を何個も使う 1000。
# 設定: 既定（R ≤ 2048 なのでエンジン a は先読みあり）/ エンジン a の先読みなし（--pf 0）/
# エンジン a の語を device メモリに（--amem dev）/
# dispatch を細かく分ける（--chunk 3、段の途中で切れる）・threadgroup を小さく /
# エンジン b の g の下の桁を 3（2^3 ≥ d の最小）にして桁あふれを頻繁に起こす / g を全部の桁で持つ（--gl 0）。
# 全部一致すれば 0 で終わる。
set -euo pipefail
cd "$(dirname "$0")/.."
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
make -s -C sw sa
make -s -C gpu sa_gpu
fail=0
python3 sw/gen_regular.py 128 5 --seed 3 -o "$tmp/rr_n128_d5.adj" 2>/dev/null
python3 sw/gen_regular.py 100 6 --seed 4 -o "$tmp/rr_n100_d6.adj" 2>/dev/null
for case in "data/rr_n256_d4_s1 4 410 420" "data/circ_n64_d4 4 76 86" "data/rr_n100_d4_s1 4 160 170" \
            "data/rr_n64_d3_s1 3 75 80" "data/rr_n1024_d3_s1 3 1200 1260" \
            "$tmp/rr_n128_d5 5 230 250" "$tmp/rr_n100_d6 6 210 230"; do
    read -r g d t1 t2 <<<"$case"
    for sched in "3 0.3 20 2" "0.8 0.2 4 5"; do
        read -r T0 T1 S m <<<"$sched"
        python3 sw/make_sched.py "$d" --T0 "$T0" --T1 "$T1" --stages "$S" --sweeps "$m" -o "$tmp/s"
        for R in 8 200 1000; do
            args=(-g "$g.adj" -s "$tmp/s" -R "$R" -S 11 -t 0 -t "$t1" -t "$t2")
            sw/sa "${args[@]}" -j 4 --init "$tmp/i0" --final "$tmp/f0" >"$tmp/o0" 2>/dev/null
            ng=""
            for opt in "-e a" "-e a --chunk 3 --tg 32" "-e a --pf 0" "-e a --amem dev" "-e b" "-e b --chunk 3 --tg 32" "-e b --gl 3" "-e b --gl 0"; do
                # shellcheck disable=SC2086
                gpu/sa_gpu "${args[@]}" $opt --init "$tmp/i1" --final "$tmp/f1" >"$tmp/o1" 2>/dev/null \
                    || { ng="$ng [$opt](失敗)"; continue; }
                cmp -s "$tmp/o0" "$tmp/o1" && cmp -s "$tmp/i0" "$tmp/i1" && cmp -s "$tmp/f0" "$tmp/f1" || ng="$ng [$opt]"
            done
            hits=$(grep "summary target $t2:" "$tmp/o0" | cut -d' ' -f4)
            if [ -n "$ng" ]; then echo "NG sa_gpu $(basename "$g") T0=$T0 R=$R:$ng"; fail=1
            else echo "OK sa_gpu $(basename "$g") T0=$T0 R=$R a・b × 設定 8 通り（目標 $t2 に $hits）"; fi
        done
    done
done
exit $fail
