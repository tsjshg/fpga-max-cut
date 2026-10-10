#!/usr/bin/env python3
"""M4 のボード側（FPGA）: Mac で作ったジョブ（in.bin）を流し、期待値（exp.bin）と全語を照合し、時間を測る。

    ./run.sh mc_bench.py <bit> <jobs のフォルダ> [名前 ...] [--rep 1]

期待値は Mac の sa_fast（sa とビット単位で同じ）が作ったもの。1 ジョブごとに 1 行の JSON を出す:
  cycles: 回路のサイクルカウンタ（窓 0 の始めから最後の窓の後まで）
  wall:   CPU から測った時間（DMA の起動・読み込み・結果の返送を含む）
"""
import argparse
import json
import os
import sys
import time

import numpy as np
from pynq import Overlay, allocate
from pynq.ps import Clocks

ap = argparse.ArgumentParser()
ap.add_argument("bit")
ap.add_argument("jobs")
ap.add_argument("names", nargs="*")
ap.add_argument("--rep", type=int, default=1)
a = ap.parse_args()
if os.geteuid() != 0:
    sys.exit("Run as root in a login shell: ./run.sh mc_bench.py ...")

ol = Overlay(a.bit)
dma = ol.axi_dma_0
fclk = Clocks.fclk0_mhz
names = a.names or sorted(os.listdir(a.jobs))
ok = True
for name in names:
    d = os.path.join(a.jobs, name)
    inp = np.fromfile(os.path.join(d, "in.bin"), dtype=np.uint64)
    exp = np.fromfile(os.path.join(d, "exp.bin"), dtype=np.uint64)
    src = allocate(shape=inp.shape, dtype=np.uint64)
    dst = allocate(shape=exp.shape, dtype=np.uint64)
    src[:] = inp
    src.flush()
    for r in range(a.rep):
        dst[:] = 0
        dst.flush()
        t0 = time.perf_counter()
        dma.recvchannel.transfer(dst)
        dma.sendchannel.transfer(src)
        dma.sendchannel.wait()
        dma.recvchannel.wait()
        wall = time.perf_counter() - t0
        dst.invalidate()
        got = np.array(dst)
        bad = int(np.count_nonzero(got != exp))
        ok &= bad == 0
        print(json.dumps(dict(name=name, rep=r, fclk_mhz=fclk, words=len(exp), bad=bad,
                              cycles=int(got[1]), windows=int(got[2]) & ((1 << 48) - 1),
                              K=int(got[2]) >> 48, wall=wall)), flush=True)
    src.freebuffer()
    dst.freebuffer()
sys.exit(0 if ok else 1)
