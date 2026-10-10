# Max-Cut by simulated annealing on a PYNQ-Z1

[日本語版はこちら (Japanese)](README_ja.md)

How fast can a small FPGA run simulated annealing for **unweighted Max-Cut**? This project builds the
annealing engine by hand in Verilog on a **PYNQ-Z1** (Zynq XC7Z020) and compares it with the
**same algorithm, optimized just as hard, on CPUs**: the board's own Cortex-A9 and an Apple M4 Mac mini.

The goal is high-quality cuts found fast, not proofs of optimality. "The FPGA must be faster" is not
assumed: speed, solution quality and resources are measured separately, and the result is reported
whichever way it falls.

**Explainer: <https://tsjshg.github.io/fpga-max-cut/>.** It covers what Max-Cut is, the methods
for it, simulated annealing, and how the annealer was built as a circuit, with figures, in English and Japanese.

## Results so far

Random 4-regular graph, n = 1024 (2048 edges), 1000 sweeps per run. All numbers are **measured**.

| Engine | Replicas | Trials per second | vs. FPGA |
|---|---:|---:|---|
| **FPGA**, 56 lanes at 125 MHz | 896 | **7.0 × 10⁹** | — |
| Cortex-A9 × 2 on the same board (`sw/sa_fast.c`, 2 threads) | 256 | 1.12 × 10⁸ | FPGA is **63× faster** |
| Apple M4 Mac mini, 10 cores (`sw/sa_fast.c`) | 10,240 | 2.98 × 10¹⁰ | FPGA is **4.3× slower** |

- **Every engine follows exactly the same search path.** The FPGA, the CPU reference
  (`sw/sa.c`) and the optimized CPU version give bit-identical results for the same seeds,
  and every output word from the board is checked against the CPU.
  So the number of trials needed to reach a target cut is the same everywhere, and the
  time-to-target ratio equals the throughput ratio.
- The best cut found for this graph is 1774. It is not proven optimal.
- **Why the M4 wins.** The optimized CPU version bit-slices 128 replicas, so one 128-bit XOR
  advances 128 xorshift64 generators at once. The FPGA spends about 450 LUTs per lane to do one
  trial per clock, and the XC7Z020 (28 nm, 2012) is at 90% of its slices with 56 lanes. Matching the M4
  (3 nm, 2024) at 125 MHz would take about 240 lanes (estimate).
- **Time to target (TTS99, M4).** On the hard target (G50, cut 5872), the FPGA needs 8.1 s, the M4
  2.0 s and the Cortex-A9 211 s, so the ratios follow throughput. On easy targets (G48/G49 at their
  optimum 6000, the random graph at 1774), one short run of 100 sweeps is enough on every engine. Then
  the length of one run decides it, and the FPGA is only about 2× slower than the M4. The FPGA's run
  time is n × 16 clocks per sweep and does not shrink with fewer replicas. Details in
  [`docs/m4.md`](docs/m4.md).
- **Energy per trial has not been measured yet.** The board draws a few watts and the Mac mini a
  few tens of watts under load, so the comparison may come out differently there.

## How it works

**Algorithm.** Single-spin-flip Metropolis annealing on a d-regular graph. Vertices are visited in
a fixed order (sequential sweeps; this measured about 4× better in time-to-target than picking
vertices at random). Only integers are used:

- Each replica has its own xorshift64 generator, advanced exactly once per trial.
- Flipping vertex v changes the cut by ΔC = 2·n_same − d, where n_same is the number of
  neighbours with the same colour. The flip is accepted if ΔC ≥ 0 or r < TH[n_same].
- The threshold table TH = ⌊exp(ΔC/T)·2³²⌋ is computed **once** on the host (`sw/make_sched.py`)
  and handed to every engine as integers. No floating point runs anywhere else, which is what
  makes the results bit-exact across the Mac, the Cortex-A9 and the FPGA.

**Circuit** ([`hw/rtl/`](hw/rtl/), details in [`docs/m1-design.md`](docs/m1-design.md)):

```
          one window = 16 clocks, all replicas work on the same vertex v
 lane 0:  replica 0  1  2 ... 15 | 0  1  2 ... 15 | ...     5-stage pipeline,
 lane 1:  replica 16 17 ...   31 | 16 17 ...   31 | ...     one trial per clock per lane
  ...                                                       (P = 16 replicas interleaved,
 lane K-1                                                    so no read/write hazards)
          |<---- vertex v ----->|<--- vertex v+1 --->|

 colouring in BRAM: one (16·K)-bit word per vertex, bit p = colour in replica p
 while the window for v runs, the words of v+1's neighbours are prefetched;
 if a neighbour is v itself (still being updated), its bits come from a register instead
```

- Each lane interleaves 16 replicas, so a replica's next trial is 16 clocks later. Read/write
  hazards are ruled out by construction.
- All K lanes visit the same vertex in lockstep. The colouring of all 16·K replicas is stored
  as one word per vertex, so a single BRAM port serves every lane, and BRAM stops being the
  bottleneck.
- Everything runs from on-chip memory. One AXI DMA stream carries the job in, one carries the
  results out. The results are best cut, the trial at which it was found, the trial at which
  the target was first reached, the final cut and the final colouring.
- Speed is measured with a cycle counter inside the circuit. It always reads exactly
  (windows + 1) × 16 cycles.

**Scaling on the XC7Z020** (125 MHz, whole design including DMA, all measured,
[`docs/m3.md`](docs/m3.md)). The last two rows are after trimming each lane's LUTRAM (32-bit trial
numbers, results read through the write port), which took a lane from about 600 to about 450 LUTs:

| Lanes K | Replicas | WNS (ns) | LUTs | BRAM tiles | Trials/s on the board |
|---:|---:|---:|---:|---:|---:|
| 1 | 16 | +0.196 | 6,172 (12%) | 16.5 | 1.25 × 10⁸ |
| 8 | 128 | +0.294 | 11,352 | 29 | 1.00 × 10⁹ |
| 16 | 256 | +0.070 | 17,318 | 43 | 2.00 × 10⁹ |
| 32 | 512 | +0.285 | 31,395 (59%) | 71.5 | 4.00 × 10⁹ |
| 48 | 768 | +0.124 | 44,652 (84%) | 100 (71%) | 6.00 × 10⁹ |
| 64 | 1,024 | placement fails: out of slices | | | |
| 48, trimmed | 768 | +0.167 | 35,235 (66%) | 100 | 6.00 × 10⁹ |
| **56, trimmed** | **896** | **+0.135** | **40,490 (76%)** | **114.5 (82%)** | **7.00 × 10⁹** |

142.86 MHz did not close (WNS −0.09 to −0.44 ns; at 56 lanes the worst path is inside the AXI DMA),
so the design stays at 125 MHz.

## Verification

- The CPU reference `sw/sa.c` is checked against an independent Python implementation
  (`sw/sa_check.py`) and gives identical output on the Mac and on the Cortex-A9.
- **xsim**: for K = 1, 2, 4, 8, 48 and 56 lanes, four jobs run back to back, with random gaps in the
  input stream and back-pressure on the output. Every output word matches the CPU reference.
  Deliberately broken variants fail as they should: forwarding disabled, write-back one clock
  late, or prefetched words captured one clock early.
- **On the board**: every output word matches for K = 1, 2, 4, 8, 16, 32, 48 and 56.
  The expected values are generated on the board by the CPU reference.

## Repository layout

```
sw/sa.c              CPU reference: the rules the circuit must match bit for bit
sw/sa_fast.c         optimized CPU version for the comparison (bit-sliced replicas), same results
sw/gen_regular.py    random d-regular graphs (and circulant graphs for tests)
sw/make_sched.py     temperature schedule -> integer threshold table (.sched)
sw/job.py            stream format: job in, expected results out (used by xsim and the board)
sw/mkvec.py          xsim test vectors from the CPU reference
sw/check.sh          CPU-side consistency checks (sa vs sa_check.py, sa_fast vs sa)
hw/rtl/              mc_core.v (load, window control, output), mc_lane.v (one pipeline lane)
hw/sim/              testbench with stalls and back-pressure; run.sh <K>
hw/build.tcl         PS7 + AXI DMA + mc_core. Args: bd|all [FCLK MHz] [K] [parallel jobs]
host/mc.py           on the board: run a job, compare every word with the CPU, report trials/s
data/                the fixed test graphs and the standard schedule
docs/                design notes and measurements (in Japanese)
```

## Try it yourself

You need a PYNQ-Z1 with the PYNQ v3.1 image and Vivado 2024.1. The tooling for running Vivado
from a Mac (in Docker, or on a Linux build machine), preparing the board and pushing files to it
comes from **[fpga-mac](https://github.com/tsjshg/fpga-mac)**. Set that up first. This repository
started from its PYNQ-Z1 template, and `vivado.sh` and `pynq.sh` below are its scripts.

No prebuilt bitstream is included yet.

**1. CPU side** (any Unix with a C compiler and Python 3; numpy is only needed on the board)

```bash
make -C sw
sw/check.sh                       # sa vs the Python re-implementation, sa_fast vs sa
sw/sa -g data/rr_n1024_d4_s1.adj -s data/std_d4.sched -R 256 -j 8 -t 1774 -q
sw/sa_fast -g data/rr_n1024_d4_s1.adj -s data/std_d4.sched -R 1024 -j 8 -t 1774 -q
```

**2. Simulation** (vectors are generated on the Mac, the simulation runs where Vivado is)

```bash
python3 sw/mkvec.py --K 1
cd hw
vivado.sh sh 'sim/run.sh 1'       # -> PASS: K=1, 4 jobs, ... all match
```

**3. Bitstream**

```bash
vivado.sh build build.tcl bd 125 56 2     # block design check first (about 30 s)
vivado.sh build build.tcl all 125 56 2    # -> out/mc125_k56.bit and .hwh
```

The last argument is the number of parallel Vivado jobs. Use 2 in Docker with 15 GB of memory,
or more on a bigger Linux build machine.

**4. On the board.** Put `sw/` in `~/<dir>/sw` and run `make` there. Then put `host/*`,
`sw/job.py` and the `.bit`/`.hwh` in `~/<dir>/host`, and `data/*` in `~/<dir>/data`.

```bash
pynq.sh run <dir>/host mc.py mc125_k56.bit ../data/rr_n1024_d4_s1.adj ../data/std_d4.sched --K 56 --target 1774
```

This prints whether every word matched the CPU, the cycle count and trials per second. The CPU
reference on the board takes about 50 s to produce the expected values for 896 replicas.

## Status

| Stage | | |
|---|---|---|
| M0 | CPU reference and graph generator | done ([docs/m0.md](docs/m0.md)) |
| M1 | the circuit, bit-exact in xsim | done ([docs/m1-design.md](docs/m1-design.md)) |
| M2 | bit-exact on the board, trials/s | done |
| M3 | scaling the number of lanes, then trimming each lane | done ([docs/m3.md](docs/m3.md)) |
| — | optimized CPU version | done ([docs/cpu-fast.md](docs/cpu-fast.md)) |
| M4 | time-to-target (TTS99) comparison; G-set G48–G50 | done ([docs/m4.md](docs/m4.md)) |
| | energy per trial | waiting for a power meter |
| M5 | replica exchange | later |

## License

BSD 3-Clause ([LICENSE](LICENSE)). The PS7 configuration block in `hw/ps7.tcl` comes from
[Xilinx/PYNQ](https://github.com/Xilinx/PYNQ) (BSD 3-Clause, Copyright Xilinx, Inc.), via
fpga-mac. Its notice is kept in the file header.
