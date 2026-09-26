# Accelerating Monte Carlo $\pi$ Step-by-Step

[![C++20](https://img.shields.io/badge/Language-C%2B%2B20-blue.svg)](https://en.cppreference.com/w/cpp/20)
[![Platform](https://img.shields.io/badge/Architecture-ARM64%20NEON%20%7C%20Apple%20Silicon-orange.svg)](https://developer.arm.com/architectures/instruction-sets/simd-isas/neon)
[![Build](https://img.shields.io/badge/Build-CMake%20Release-green.svg)](https://cmake.org)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

This project estimates $\pi$ with a Monte Carlo method and then optimizes the same calculation step by step.

While estimating $\pi$ is a simple, embarrassingly parallel toy problem, it serves as a perfect sandbox for exploring
low-level hardware optimization.

Inspired by Mike Croucher’s lecture at EUMASTER4HPC (Luxembourg), this repository documents a deliberate, empirical
journey through low-level performance engineering.

I documented every step — what I changed, why I changed it, and the ideas I tried along the way. If you're learning
performance engineering too, I hope you can follow the reasoning and take something useful from it, the way I did.

Have fun :)

---

## The method

Monte Carlo $\pi$ works like this. Throw random points into the unit square. Count how many land inside the quarter
circle ($x^2 + y^2 \le 1$). The ratio of inside-points to total points approaches $\pi/4$, so multiply by 4.

The work is one tight loop: generate two random numbers, square them, compare, count. Every version optimizes that loop.

---

## 📊 Benchmark Results at a Glance

Tested at $10^{9}$ (1 billion) samples. v0–v4 run on an Apple M1 (MacBook Air, 8-core, Apple Clang Release `-O3`, a
single controlled run); v5 runs on a Tesla T4 GPU (Google Colab, CUDA 12.8, kernel time). Throughput is the headline
metric — how many samples the kernel processes per second (legend below the table).

| Version | Change                         | Device           | ns/sample ↓ | Throughput ↑ |          vs v0 |    π error |
|:-------:|:-------------------------------|:-----------------|------------:|-------------:|---------------:|-----------:|
| **v0**  | libc `rand()`                  | M1 · 1 core      |      14.713 |    0.068 G/s |             1× | 1.7 × 10⁻⁵ |
| **v1**  | inlinable PCG32                | M1 · 1 core      |       2.144 |     0.47 G/s |           6.8× | 2.4 × 10⁻⁵ |
| **v2**  | branchless hit test            | M1 · 1 core      |       2.127 |     0.47 G/s |           6.9× | 2.4 × 10⁻⁵ |
| **v3**  | SIMD (NEON, 4-lane LCG)        | M1 · 1 core      |       0.316 |     3.16 G/s |            46× | 1.1 × 10⁻⁵ |
| **v4**  | v3 kernel × 8 threads (OpenMP) | M1 · 8 cores     |       0.067 |     14.9 G/s |           217× | 1.5 × 10⁻⁵ |
| **v5**  | CUDA grid-stride               | **Tesla T4 GPU** |  **0.0055** |  **180 G/s** | **~2,650×** \* | 7.4 × 10⁻⁵ |

> Reading the table:
> - ns/sample: time to process one sample (lower is better). Derived from the minimum of 10 runs (v5 is a single run).
> - Throughput: samples processed per second (higher is better). The reciprocal of time (`1 / ns per sample`), an
    intuitive measure of "work per second." 1 Gsample/s = $10^9$ samples/s.
> - vs v0: relative speedup over the baseline scalar implementation.
> - π error: the absolute error $\vert{}\text{estimate} - \pi\vert{}$. Every version lands within $\approx 10^{-4}$ of
    $\pi$, shrinking proportionally to $1/\sqrt{N}$. Estimates are identical run-to-run because the random seeds are fixed.
>
> Cross-hardware context: v0–v4 execute on an M1 CPU; v5 runs on a datacenter-class Tesla T4 GPU. The ~2,650× figure
> compares distinct silicon architectures. The fairer algorithmic comparison is best-CPU vs GPU: v5 vs v4 yields a ~12×
> speedup.

![Figure 1: Monte Carlo pi cost per sample across optimization stages](docs/images/benchmark_scientific.svg)
Figure 1 — cost per sample: v0→v4 (Apple M1) and v5 (Tesla T4 GPU). Log scale, lower is better.

> Note on the v3 generator. v0 → v2 hold the PRNG fixed (PCG32) and change one variable at a time. v3 deliberately
> changes three at once — 4-wide SIMD, `float32` instead of `double`, and a cheaper 4-lane LCG in place of PCG32 — so its
> gain over v2 is not attributable to NEON alone. Vectorizing PCG32 would require cross-lane shifts and its permutation
> step; to isolate raw arithmetic throughput, v3 benchmarks a vectorized 32-bit LCG, which is both computationally
> cheaper and statistically weaker (LCGs have known low-bit and lattice / spectral defects). For cryptographic or
> high-dimensional Monte Carlo where spectral quality matters, a 4-lane xoshiro128+ or a vectorized PCG should be
> substituted.

### v4 — multi-core scaling (OpenMP)

V4 is the same version as v3 but with one distinction: it runs the identical SIMD kernel on multiple cores (OpenMP
`parallel` + `for` + `reduction(+:hits)`). Because nothing else changed, the speedup is attributable purely to
parallelism — and v4 on one thread matches v3 (~0.32 ns/sample).

Since the Apple M1 uses two different types of cores — performance and efficiency cores — I wanted to show how this
physical difference is actually measurable. That is the purpose of the table below.

Parallel speedup over the single-thread kernel (efficiency = speedup ÷ threads):

|   Threads   | $10^{8}$ speedup | eff. | $10^{9}$ speedup | eff. |
|:-----------:|:----------------:|:----:|:----------------:|:----:|
|      1      |      1.00×       |  —   |      1.00×       |  —   |
|      2      |      1.98×       | 99%  |      1.98×       | 99%  |
| 4 (P-cores) |      3.38×       | 85%  |    **3.74×**     | 94%  |
| 8 (4P + 4E) |      4.44×       | 55%  |    **4.70×**     | 59%  |

![Figure 2: v4 parallel scaling across threads](docs/images/v4_scaling.svg)
Figure 2 — parallel speedup vs. thread count.

> Two things to learn from the graph:
> 1. Near-linear scaling on P-cores: on the 4 performance cores, scaling is 3.74× at $10^9$ samples (94% efficiency),
     and 2 threads give a clean 1.98× (99% efficiency).
> 2. Sub-linear scaling with E-cores: the 4 efficiency cores add real throughput, but 8 threads reach 4.70× (not 8×)
     because E-cores are physically slower than P-cores.

### v5 — the GPU (CUDA)

v4 used every core the M1 has, so v5 changes the machine: a GPU trades a few fast cores for thousands of slow ones,
betting on parallel width. The same LCG kernel, rewritten in CUDA and run on a Tesla T4 (Google Colab), reaches
180 Gsample/s — about 12× the best CPU (v4). The full kernel is in [`cuda/pi.cu`](cuda/pi.cu).

A throughput device — watch it warm up:

| Samples | Kernel time |  Throughput |
|--------:|------------:|------------:|
|  $10^6$ |    0.109 ms | 9 Gsample/s |
|  $10^7$ |    0.121 ms | 83 Gsample/s |
|  $10^8$ |    0.565 ms | 177 Gsample/s |
|  $10^9$ |    5.549 ms | 180 Gsample/s |

At $10^6$ the GPU is slow (9 G/s): 65,536 threads over a fixed launch cost can't be amortized by so little work.
Throughput climbs with $N$ and only saturates near $10^8$–$10^9$ — the signature of a throughput device: the CPU wins
the small jobs (low latency), the GPU wins the big ones (high throughput).

> Note on v5. It runs on a different machine (a T4 GPU vs the M1 laptop), so the ~2,650× vs v0 compares devices, not the
> same silicon — the fair number is v5 vs v4, about 12×. Timing is kernel-only (excluding allocation and the single
> counter copy-back), and it is a single run rather than the min-of-10 used for the CPU.

### Supporting figures

![v3 benchmark output in CLion](docs/images/v3_clion_output.png)

![v0 profile in Instruments](docs/images/v0_instruments_profile.png)


---

## Architectural Roadmap

This project explores the modern compute hierarchy layer-by-layer:

```
[v0 Baseline] ──► Libc dynamic symbol overhead, optimization barriers
      │
      ▼
[v1 Inlining] ──► PCG32 state machine in header, register promotion
      │
      ▼
[v2 Branchless] ──► Compiler analysis, CMOV / conditional selects
      │
      ▼
[v3 SIMD NEON] ──► 4-wide vectorization, FMA, IEEE-754 mantissa scaling (46×)
      │
      ▼
[v4 Multi-core]  ──► OpenMP across 4P + 4E cores — 217× @ 1e9 (4.70× parallel)
      │              (near-linear on the P-cores; E-cores add a sub-linear boost)
      ▼
[v5 GPU (CUDA)]  ──► Tesla T4, grid-stride + atomics — 180 Gsample/s (~12× v4, ~2,650× v0)
                     (a throughput device: it needs large N to win)
```

-------

## Build & Reproduce

### Prerequisites

* CMake ≥ 3.20
* C++20 compiler (Apple Clang on macOS, GCC/Clang on Linux with ARM64 NEON)
* OpenMP runtime — required by v4. Apple Clang ships without it, so on macOS:
  ```bash
  brew install libomp
  ```
  `CMakeLists.txt` points at the Apple-Silicon Homebrew path (`/opt/homebrew/opt/libomp`); adjust it for Intel macOS (
  `/usr/local/opt/libomp`) or Linux (OpenMP usually needs no extra install).

### Compiling and Running

```bash
# Configure Release build
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release

# Compile
cmake --build build

# Execute benchmark
./build/pi_bench
```

> [!IMPORTANT]
> Always benchmark Release builds (`-DCMAKE_BUILD_TYPE=Release`). In Debug mode, inlining is disabled and SIMD
> registers are continuously spilled to the stack, producing unrepresentative timings.

### GPU version (v5)

The CUDA version is separate from the CMake build (it was developed on a Google Colab Tesla T4). With the CUDA toolkit
installed:

```bash
nvcc -arch=sm_75 -O3 cuda/pi.cu -o pi && ./pi
```

`-arch=sm_75` targets the T4 (Turing); change it to match your GPU.

---

## Repository Layout

```
├── CMakeLists.txt         # Warnings (-Wall -Wextra); -O3 comes from the Release build type
├── src/
│   ├── main.cpp           # Benchmark runner
│   ├── versions.hpp       # Function interfaces & scalar PRNG definitions
│   ├── versions.cpp       # Kernels: v0->v3 (scalar->SIMD) + v4 (OpenMP multi-core)
│   └── bench.hpp          # Timing harness (warmups, min / spread, throughput)
├── cuda/
│   └── pi.cu              # v5: the GPU kernel (grid-stride + atomics), built with nvcc
├── docs/
│   ├── Approach.md        # The measure-first engineering methodology
│   ├── Versions.md        # Detailed engineering log, predictions, & profiler traces
│   ├── Theory.md          # PRNG mathematics, IEEE-754 bits, and vector pipelines
│   └── images/            # Profiler screenshots & benchmark evidence
```

---

## Experimental Rig & Known Constraints

* Host hardware (v0–v4): Apple M1 (MacBook Air, 8 GB Unified Memory).
* GPU host (v5): NVIDIA Tesla T4 (16 GB), Google Colab — CUDA 12.8 / driver 13.0, `nvcc -arch=sm_75 -O3`; v5 is a
  single kernel-timed run, not the min-of-10 used for the CPU.
* Frequency scaling: macOS does not allow manual pinning of CPU clock frequencies or disabling Turbo Boost. To keep the
  measurement honest, the harness discards 2 warmup runs, times 10 repeated runs, and reports the minimum (the run least
  perturbed by background OS noise).
* Dispersion: the harness records spread = (max − min) over the 10 runs, not a standard deviation, so a true σ or IQR
  would need every run logged. Even so, the tight spread on the optimized kernels (0.001 ns on v3 vs 1.735 ns on v0)
  shows the speedups are not the artefact of a single lucky run.
* Multi-core and heterogeneous cores: v4 uses all 8 cores via OpenMP, but macOS gives no way to pin threads to cores,
  and the M1 is heterogeneous (4 performance + 4 efficiency cores). That is why 8 threads reaches ~4.7×, not 8×: the E-cores
  add real but sub-linear throughput. Timings can also drift with thermal state on a fanless laptop, so treat these as
  indicative laptop numbers, not a controlled HPC node.

---


## ⭐ Support & Feedback

If you enjoyed this empirical journey or found the documentation useful, please consider giving the repository a **star**!

I'm always looking to learn and improve. If you have feedback, spot an inaccuracy, or have ideas for new optimization steps, feel free to open an issue.

---

## License

MIT License — see [`LICENSE`](LICENSE). Feel free to fork, benchmark, and build on this.


