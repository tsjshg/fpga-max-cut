#!/bin/bash
# sa_fast のエンジンごとの速さ（試行/秒）を測る。各 N 回走らせて最良を出す。
#   sw/bench_fast.sh [スレッド数] [レプリカ数] [回数] [エンジン...]
# 問題は n = 1024・d = 4 のランダム正則グラフ、標準のスケジュール（T0 = 0.8・T1 = 0.2・100 段 × 10 周回）。
set -euo pipefail
cd "$(dirname "$0")/.."
j=${1:-1}
R=${2:-256}
N=${3:-5}
shift 3 || true
make -s -C sw sa sa_fast
engines=${*:-$( (sw/sa_fast 2>&1 || true) | sed -n 's/^  エンジン: *\(.*\)（.*/\1/p')}
best() { sort -g | tail -1; }
rate() { sed -n 's/.*s, \([0-9.e+]*\) trials.*/\1/p'; }
echo "# threads=$j R=$R runs=$N $(uname -m)"
x=$(for i in $(seq "$N"); do sw/sa -g data/rr_n1024_d4_s1.adj -s data/std_d4.sched -R "$R" -q -j "$j" 2>&1 >/dev/null | rate; done | best)
printf "%-8s %s\n" sa "$x"
for e in $engines; do
    x=$(for i in $(seq "$N"); do sw/sa_fast -e "$e" -g data/rr_n1024_d4_s1.adj -s data/std_d4.sched -R "$R" -q -j "$j" 2>&1 >/dev/null | rate; done | best)
    printf "%-8s %s\n" "$e" "$x"
done
