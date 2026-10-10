#!/bin/bash
# xsim で機能検証する。コンテナの中で動かす:  vivado.sh sh 'sim/run.sh'   （hw/ で叩く）
# 作業ファイル（xsim.dir/ や *.log）は sim/work/ にできる。
set -eo pipefail
cd "$(dirname "$0")"
mkdir -p work && cd work
xvlog ../../rtl/axis_cumsum.v ../tb_axis_cumsum.v > xvlog.out
xelab tb -s tb > xelab.out
xsim tb -R | tee xsim.out | grep -E "PASS|FAIL|MISMATCH|TIMEOUT"
grep -q PASS xsim.out
