#!/bin/bash
# ボードの上で、Python スクリプトを root・ログインシェルで起動し直す。
#   ./run.sh cumsum.py cumsum100.bit 16
# PYNQ の venv と XILINX_XRT は /etc/profile.d でしか入らないので、sudo python3 … では
# pynq が見つからない。ビットストリームを書くには root が要る。だから sudo bash -lc。
# 引数は printf %q で引用し直すので、空白や引用符を含んでいてもそのまま渡る。
cd "$(dirname "$0")" || exit 1
exec sudo bash -lc "cd $(printf %q "$PWD") && exec python3 $(printf '%q ' "$@")"
