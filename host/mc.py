#!/usr/bin/env python3
"""ボードで mc_core を動かし、CPU 版（同じボードの sw/sa）と全語照合して、速さを測る。

    ./run.sh mc.py <bit> <graph.adj> <sched.sched> [--K 1] [--seed 1] [--target 0] [--rep 3]

ボードの置き方: ~/max_cut/host/ に host/* と sw/job.py、~/max_cut/sw/ で sa を make しておく。

速さは回路の中のサイクルカウンタ（窓 0 の始めから最後の窓の後まで）で出す。
CPU から測る時間（DMA の起動・待ち・読み込み込み）も並べて出すが、そちらは固定費を含む。
"""
import argparse
import os
import sys
import tempfile
import time

import numpy as np
from pynq import Overlay, allocate

D = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, D)
import job  # noqa: E402

P = 16

ap = argparse.ArgumentParser()
ap.add_argument("bit")
ap.add_argument("graph")
ap.add_argument("sched")
ap.add_argument("--K", type=int, default=1)
ap.add_argument("--seed", type=int, default=1)
ap.add_argument("--target", type=int, default=0)
ap.add_argument("--rep", type=int, default=3)
ap.add_argument("--sa", default=os.path.join(D, "..", "sw", "sa"))
a = ap.parse_args()

if os.geteuid() != 0:
    sys.exit("Run as root in a login shell: ./run.sh mc.py ...")

with tempfile.TemporaryDirectory() as tmp:
    t0 = time.perf_counter()
    inp, exp, reps = job.make_job(a.sa, a.graph, a.sched, a.K, P, a.seed, a.target, tmp)
    t_cpu = time.perf_counter() - t0

ol = Overlay(a.bit)
dma = ol.axi_dma_0
from pynq.ps import Clocks  # noqa: E402
fclk = Clocks.fclk0_mhz
print(f"bitstream {os.path.basename(a.bit)}  FCLK0 {fclk:.3f} MHz  K={a.K} R={a.K * P}")

src = allocate(shape=(len(inp),), dtype=np.uint64)
dst = allocate(shape=(len(exp),), dtype=np.uint64)
src[:] = np.array(inp, dtype=np.uint64)
src.flush()
want = np.array(exp, dtype=np.uint64)

ok = True
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
    bad = np.nonzero(got != want)[0]
    cycles, WT = int(got[1]), int(got[2])
    trials = WT * a.K * P
    sec = cycles / (fclk * 1e6)
    print(f"run {r}: {'all %d words match' % len(want) if len(bad) == 0 else '%d words differ' % len(bad)}"
          f" | cycles {cycles:,} (design {(WT + 1) * P:,}) → {sec * 1e3:.3f} ms, {trials / sec:.4g} trials/s"
          f" | wall {wall * 1e3:.3f} ms")
    if len(bad):
        ok = False
        for i in bad[:10]:
            print(f"  word {i}: got {int(got[i]):016x} want {int(want[i]):016x}")

best = [rp["best"] for rp in reps]
print(f"best: mean {np.mean(best):.2f} max {max(best)}; CPU (sa, 1 thread, incl. startup) {t_cpu * 1e3:.1f} ms")
src.freebuffer()
dst.freebuffer()
sys.exit(0 if ok else 1)
