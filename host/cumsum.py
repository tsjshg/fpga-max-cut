#!/usr/bin/env python3
"""ひな形の回路（axis_cumsum）をボードで動かし、numpy と照合して帯域を測る。

    ./run.sh cumsum.py [cumsum100.bit] [MB]

root・ログインシェルで動かす必要があるので、直接 python3 で叩かず run.sh を通す。
.bit と同じ basename の .hwh が同じフォルダに要る。

帯域は「1回の時間で割る」のではなく、全量と半量の時間差（傾き）で出す。
PYNQ の transfer/wait には1回あたり 1 ms 弱の固定費があり、割り算だとそれが
帯域の目減りに見えるため。
"""
import os, sys, time
import numpy as np
from pynq import Overlay, allocate

D = os.path.dirname(os.path.abspath(__file__))
BIT = sys.argv[1] if len(sys.argv) > 1 else os.path.join(D, "cumsum100.bit")
MB = int(sys.argv[2]) if len(sys.argv) > 2 else 16
REP = 5

if os.geteuid() != 0:
    sys.exit("Run as root in a login shell: ./run.sh cumsum.py ...")

ol = Overlay(BIT)
dma = ol.axi_dma_0
try:
    from pynq.ps import Clocks
    fclk = Clocks.fclk0_mhz
except Exception:
    fclk = None
print(f"bitstream: {os.path.basename(BIT)}  FCLK0: {fclk:.2f} MHz" if fclk else f"bitstream: {BIT}")

n = MB * 1024 * 1024 // 8                      # ビート数（1ビート = 32bit × 2 レーン）
src = allocate(shape=(n, 2), dtype=np.uint32)
dst = allocate(shape=(n, 2), dtype=np.uint32)
src[:] = np.random.default_rng(1).integers(0, 2**32, size=(n, 2), dtype=np.uint32)
src.flush()


def run(beats):
    """先頭 beats ビートを1回の転送で流し、かかった時間を返す。"""
    s, d = src[:beats], dst[:beats]
    t0 = time.perf_counter()
    dma.recvchannel.transfer(d)                # 受け側を先に構える
    dma.sendchannel.transfer(s)
    dma.sendchannel.wait()
    dma.recvchannel.wait()
    return time.perf_counter() - t0


def check(beats):
    dst.invalidate()
    exp = np.cumsum(src[:beats], axis=0, dtype=np.uint32)   # 32bit で桁あふれは捨てる
    bad = np.count_nonzero(dst[:beats] != exp)
    print(f"  check {beats:>9,} beats: " + ("all match" if bad == 0 else f"{bad:,} words differ"))
    return bad == 0


ok = True
for beats in (1, 7, n // 2, n):                # 長さ 1 の列や半端な長さも踏む
    dst[:] = 0; dst.flush()
    run(beats)
    ok &= check(beats)

full = min(run(n) for _ in range(REP))
half = min(run(n // 2) for _ in range(REP))
slope = (n * 8 - (n // 2) * 8) / (full - half) / 1e9
print(f"\n{MB} MB: one transfer {full * 1e3:.2f} ms, half {half * 1e3:.2f} ms")
print(f"stream throughput (slope): {slope:.3f} GB/s in, the same out")
if fclk:
    lane = 8 * fclk / 1e3
    print(f"  64-bit stream limit at {fclk:.2f} MHz: {lane:.3f} GB/s  ({100 * slope / lane:.1f}%)")
print(f"fixed cost per call: {(full - n * 8 / slope / 1e9) * 1e3:.2f} ms")

src.freebuffer(); dst.freebuffer()
sys.exit(0 if ok else 1)
