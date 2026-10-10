#!/bin/bash
# xsim で機能検証する（hw/ で叩く）:  vivado.sh sh 'sim/run.sh 1'   （引数はレーンの数 K）
# 照合ベクタは先に Mac で作っておく: python3 sw/mkvec.py --K 1
# 作業ファイルは sim/work_k<K>/ にできる。
set -eo pipefail
K=${1:-1}
cd "$(dirname "$0")"
mkdir -p work_k$K && cd work_k$K
xvlog -i ../vec_k$K ../../rtl/mc_lane.v ../../rtl/mc_core.v ../tb_mc_core.v > xvlog.out || { cat xvlog.out; exit 1; }
xelab tb -s tb -timescale 1ns/1ps > xelab.out || { cat xelab.out; exit 1; }
xsim tb -R | tee xsim.out | grep -E "PASS|FAIL|MISMATCH|TIMEOUT|job|ERROR"
grep -q PASS xsim.out
