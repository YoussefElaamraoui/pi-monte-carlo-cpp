# Accelerating Monte Carlo $\pi$ Step-by-Step

[![C++17](https://img.shields.io/badge/Language-C%2B%2B17-blue.svg)](https://en.cppreference.com/w/cpp/17)
[![Platform](https://img.shields.io/badge/Architecture-ARM64%20NEON%20%7C%20Apple%20Silicon-orange.svg)](https://developer.arm.com/architectures/instruction-sets/simd-isas/neon)
[![Build](https://img.shields.io/badge/Build-CMake%20Release-green.svg)](https://cmake.org)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

Inspired by Mike Croucher’s lecture at **EUMASTER4HPC (Luxembourg)**, this repository documents a deliberate, empirical
journey through low-level performance engineering. Rather than presenting only final code, every step documents: **the
hypothesis $\to$ the profiler trace $\to$ the assembly reality $\to$ the benchmark delta**.

---

## 📊 Benchmark Results at a Glance

Tested on an **Apple M1 (MacBook Air, 8-core CPU)** with **Apple Clang (Release, `-O3`)** across $10^{9}$ (1 billion) samples. Figures are **nanoseconds per sample**; *spread* is (max − min) across the 10 timed runs.

| Version | Change | Min | Median | Spread | Speedup | Approx π | \|error\| |
|:-------:|:-------|-----:|-------:|-------:|:-------:|:--------:|:--------:|
| **v0** | libc `rand()` | 23.391 | 24.042 | 2.861 | **1.00×** | 3.141610 | 1.7 × 10⁻⁵ |
| **v1** | Inlinable PCG32 (header) | 3.402 | 3.467 | 0.479 | **6.89×** | 3.141617 | 2.4 × 10⁻⁵ |
| **v2** | Branchless hit test | 3.393 | 3.419 | 0.306 | **6.91×** | 3.141617 | 2.4 × 10⁻⁵ |
| **v3** | SIMD NEON · 4-lane LCG | **0.505** | **0.505** | **0.001** | **46.4×** | 3.141582 | 1.1 × 10⁻⁵ |



![Figure 1: Monte Carlo pi cost per sample across optimization stages](docs/images/benchmark_scientific.svg)
**Figure 1 — cost per sample by optimization stage (log scale, lower is better).**

> **⚖️ Note on the v3 generator.** v0 → v2 hold the PRNG fixed (PCG32) and change one variable at a time. **v3 deliberately changes three at once** — 4-wide SIMD, `float32` instead of `double`, and a cheaper 4-lane LCG in place of PCG32 — so its gain over v2 is *not* attributable to NEON alone. Vectorizing PCG32 would require cross-lane shifts and its permutation step; to isolate raw arithmetic throughput, v3 benchmarks a vectorized 32-bit LCG, which is both computationally cheaper and statistically weaker (LCGs have known low-bit and lattice / spectral defects). For cryptographic or high-dimensional Monte Carlo where spectral quality matters, a 4-lane xoshiro128+ or a vectorized PCG should be substituted.

### ⏱️ Supporting figures



![1-Billion Sample Execution Race](docs/images/speed_race.svg)

![v3 benchmark output in CLion](docs/images/V3_benchmarks.png)

![v0 profile in Instruments](docs/images/V0_benchmarks.png)


---

## 🗺️ Architectural Roadmap

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
[v3 SIMD NEON] ──► 4-wide vectorization, FMA, IEEE-754 mantissa scaling (46.4×)
      │
      ▼
[v4 Multi-core]  ──► planned: cache-padded std::jthread / OpenMP fan-out
      │              (target ≈ near-linear scaling across the 4 performance cores)
      ▼
[v5 Microarch]   ──► planned: manual loop unrolling & ILP (dual-issue NEON pipelines)
```
-------


## 🛠️ Build & Reproduce

### Prerequisites

* **CMake ≥ 3.20**
* **C++17 Compiler** (Apple Clang on macOS, GCC/Clang on Linux with ARM64 NEON)

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
> **Always benchmark Release builds (`-DCMAKE_BUILD_TYPE=Release`)**. In Debug mode, inlining is disabled and SIMD
> registers are continuously spilled to the stack, producing unrepresentative timings.

---

## 📂 Repository Layout

```
├── CMakeLists.txt         # Warnings (-Wall -Wextra); -O3 comes from the Release build type
├── src/
│   ├── main.cpp           # Benchmark runner
│   ├── versions.hpp       # Function interfaces & scalar PRNG definitions
│   ├── versions.cpp       # Kernels (v0 through v3 SIMD)
│   └── bench.hpp          # Statistical timing harness (warmups, min, median, spread)
├── docs/
│   ├── Approach.md        # The measure-first engineering methodology
│   ├── Versions.md        # Detailed engineering log, predictions, & profiler traces
│   ├── Theory.md          # PRNG mathematics, IEEE-754 bits, and vector pipelines
│   └── images/            # Profiler screenshots & benchmark evidence
```

---

## 🔬 Experimental Rig & Known Constraints

* **Host Hardware**: Apple M1 (MacBook Air, 8 GB Unified Memory)
* **Frequency Scaling**: macOS does not allow manual pinning of CPU clock frequencies or disabling Turbo Boost. To
  ensure statistical integrity, the benchmark harness discards 2 warmup runs, conducts 10 repeated runs, and reports the
  **minimum runtime** (the run least perturbed by background OS noise).
* **Dispersion (and its limits here)**: the results table reports *spread* = (max − min) over the 10 runs, **not** a
  standard deviation — the harness keeps only min / median / spread, so a true σ or IQR would require logging every run.
  Even so, the tight spread on the optimized kernels (0.001 ns on v3 vs 2.861 ns on v0) shows the speedups are not the
  artefact of a single lucky run.

---

## 📜 License

MIT License — see [`LICENSE`](LICENSE). Feel free to fork, benchmark, and build on this!
