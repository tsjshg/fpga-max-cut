#!/bin/bash
# レーン数 K を変えて順にビルドし、WNS と資源を1行ずつ出す（hw/ で叩く。remote モード前提）
#   ./build_many.sh 125 1 8 16 32
# ログは logs/ に置く。out/ に置くと、vivado.sh が送り先の（空の）写しを持ち帰って上書きする。
set -o pipefail
fclk=$1; shift
mkdir -p logs out
for k in "$@"; do
  log=logs/build_${fclk}_k$k.log
  util=out/mc${fclk}_k${k}_util.txt
  timing=out/mc${fclk}_k${k}_timing.txt
  rm -f $util $timing
  ~/claude/fpga-mac/vivado.sh build build.tcl all $fclk $k 8 > $log 2>&1
  if [ -f $util ] && [ -f $timing ]; then
    wns=$(grep -A 6 "Design Timing Summary" $timing | tail -1 | awk '{print $1}')
    lut=$(grep -E "^\| Slice LUTs " $util | awk -F'|' '{print $3}' | tr -d ' ')
    lutm=$(grep -E "^\|   LUT as Memory " $util | awk -F'|' '{print $3}' | tr -d ' ')
    ff=$(grep -E "^\| Slice Registers " $util | head -1 | awk -F'|' '{print $3}' | tr -d ' ')
    bram=$(grep -E "^\| Block RAM Tile " $util | awk -F'|' '{print $3}' | tr -d ' ')
    echo "K=$k WNS=$wns LUT=$lut LUTRAM=$lutm FF=$ff BRAM=$bram"
  else
    echo "K=$k FAILED: $(grep -m3 -E 'ERROR:|failed' $log | tr '\n' ' ')"
  fi
done
