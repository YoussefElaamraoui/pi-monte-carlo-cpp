# Versions Log

Each version builds from the previous, with a prediction written *before* the
measurement so I can tell whether the change did what I expected. Baseline method
is in [`Approach.md`](images/Approach.md).

---

## Introduction : Profiling on macOS

### Tool Selection & macOS Constraints

The usual tool is `perf`, but that's Linux-only, so my options were:

- **CLion** → the three-dots menu next to Debug has a *Profile* action. On macOS it drives DTrace and shows a flame
  graph. Catch: System Integrity Protection makes DTrace finicky and it often needs elevated privileges, so it may not
  work cleanly first try.
- **Instruments** (ships with Xcode / the Command Line Tools) → the standard professional tool. **This is the one I went
  with.**
- **The `sample` command** → the quickest look, zero setup. Run the binary and, while it's churning through the 100M
  case, in another terminal run `sample <pid>` (or `sample pi_bench 5` for a 5-second sample). It prints a call tree
  with counts.

### Compilation Target Requirement

One non-negotiable, whichever you pick: **profile the Release build, not Debug.** Debug builds aren't optimized, you'd
be measuring code the real optimizer would never emit.

### Mechanics of Sampling Profilers

It doesn't measure your code directly. It pauses the program many times per second (~1000/sec by default) and records
which function was running at that instant. After thousands of pauses, a function caught running ~70% of the time was,
in fact, using ~70% of the CPU. So the number isn't "seconds spent in `rand()`", it's "the fraction of samples that
landed in `rand()`,".

### Sampling Implications & Duration

Consequence: the interesting part has to run *long enough* to collect enough samples.

---

## v0 — Baseline with `rand()`

### Finding the bottleneck:

![v0 profile in Instruments](images/v0_instruments_profile.png)

rand and its machinery are clearly the bottleneck. Reading the profile carefully, mc_pi_v0 takes 1.29 s of the 1.63 s
total (79%), and inside that the RNG cost is split across three rows:

1) rand itself → 711 ms
2) DYLD-STUB$$rand → 380 ms. The stub: because rand lives in a shared library, my code can't jump straight to it, it
   jumps
   into the library, and pays that indirection on every call( This ~380 ms is the
   total across all calls, not the cost of one)
3) a second rand entry → 339 ms

Together those three are ~1430 ms, about **88% of the total runtime**. Meanwhile mc_pi_v0 is only 201
ms, so only ~12% of the runtime.

### Solution & result

Prediction for v1: since my real work is only ~200 ms, if the RNG became essentially free I'd expect to land near
there, roughly a 3× speedup. I'm writing that down before measuring, so afterwards I can tell whether v1 succeeded or
whether I left performance on the table.

std::rand() lives in libc, compiled separately months before my program. My compiler only sees a declaration — a promise
that a function named rand exists somewhere — not its body. So the call is an optimization barrier. The compiler must
assume the worst: it can't reorder my arithmetic across the call, can't keep values in registers through it, and —
crucially — can't vectorize the loop (SIMD does several iterations at once, but each iteration calls rand, which must
run in sequence). The call freezes the optimizer.

Inlining is when the compiler copies a function's body straight into the call site, as if you'd typed it there. Then the
barrier vanishes and it can optimize across the whole loop. But inlining requires the compiler to see the source, which
is why a small PRNG in a header inlines beautifully and libc's rand() never can.




---

## v1 — Swap `rand()` for an Inlinable PCG32

### The Problem

Now that I verified also with the profiler, Instruments, the rand influence on the execution time, I have to decide
which generator to use instead. With Claude's help I came up with this list:

| Generator       | State  | Speed     | Quality                         | Get-it-wrong risk | Notes                                                                                           |
|-----------------|--------|-----------|---------------------------------|-------------------|-------------------------------------------------------------------------------------------------|
| xorshift32/64   | 4–8 B  | very fast | adequate, some known weaknesses | low               | Classic "simplest fast PRNG," ~3 shifts + XORs. Fails some strict tests but fine for π.         |
| splitmix64      | 8 B    | very fast | good                            | very low          | One multiply-shift-xor chain. Often used to seed others, but fine standalone here.              |
| **PCG32**       | 8–16 B | fast      | high                            | low-medium        | Well-documented, passes strong batteries. A multiply + a permutation step. The "solid default." |
| xoshiro256++/** | 32 B   | very fast | high                            | medium            | Modern, excellent quality/speed. Bigger state; must avoid an all-zero seed.                     |

> Note: All of these are **pseudo-random**, deterministic formulas imitating randomness.

### Solution & Results

**Decision: PCG32.** Quality isn't the deciding factor here (π is forgiving), so
the choice came down to the generator I'll actually reach for in real work *where
quality does matter* — PCG is well-documented, only a little more code, and
getting comfortable with it now is transferable. Its main risk is
seeding/constants, so my **correctness check** for v1 is: π must converge to
~3.14159 **and** the error must shrink roughly like `1/√n`. If that holds, the
seeding is right; if π drifts, I got it wrong.

### Questions

#### Why the PCG Code Lives in the Header

**Why the PCG code lives in the header, not the `.cpp`:** including a header copies
the code into the translation unit, so it can inline right at the call site — and
as the v0 profile showed, in this workload (small samples, lots of calls) the call
overhead is exactly what dominates.

> Rule of thumb I'm taking away: **small + called in a hot loop → inline it → put
> it in a header. Big function → don't inline → `.cpp`.**

#### What the Columns Mean

* **Quality** → how convincingly the sequence imitates true randomness:
  *uniformity* (are all values equally likely?), *independence* (can you predict
  the next from the previous? — matters here because I pair consecutive outputs
  into `(x, y)`, so 2D correlation would directly skew π), and *period* (finite
  state means it eventually loops; the period is how long until it does). Quality
  isn't opinion — it's measured by test batteries (TestU01, PractRand).
  For π specifically it's almost a non-issue; every generator here clears the bar.
  Quality would dominate for cryptography (predictability = broken) or
  high-dimensional physics (subtle correlations = wrong results). Knowing *when*
  quality matters is the real skill.
* **Get-it-wrong risk** → how easy it is to implement *incorrectly* in a way that
  still compiles, still runs, and still produces plausible-but-wrong numbers.
  Silent bugs, not crashes. E.g. xoshiro outputs zeros forever if seeded all-zero.
  For PCG the trap is the seeding step and its published constants — get one wrong
  and it still looks random-ish but isn't really PCG.

---

## v2 — Remove the Branch

### The Problem

This came from an assumption. Thanks to the ACA course at Politecnico di Milano I'd
learned how costly `if`s and branch mispredictions can be, so my first idea was to
refactor the hit test to drop the `if` (`count += (x*x + y*y <= 1.0)`).

But this runs into something I said earlier about profiling: a sampling profiler
won't show a branch cost ( misprediction, a cache miss, a stalled pipeline). The Time Profiler sees the symptom (time),
not the cause.

To *see* the branch cost you need a different tool: a **hardware-counter
profiler**. Modern CPUs have counters for low-level events, branch
mispredictions, cache misses, instructions retired. On Linux `perf stat` prints
these directly (a `branch-misses` line with a percentage), the standard way, and
part of why HPC people live on Linux. On my Mac, Instruments has a *CPU Counters*
instrument (a different template from the Time Profiler) that reads some of these,
though Apple Silicon exposes them less freely than Intel/Linux, so it's fiddlier.

### Solution & Results

**Result: the assumption was wrong.**

The time didn't change, I assume the compiler
had *already* compiled the `if` into branchless code, so my "optimization" was a
no-op. A useful negative result: worth knowing the compiler often gets there first.

---

## v3 — SIMD

### The Problem

Right now each instruction runs on one piece of data at a time. **SIMD** (Single
Instruction, Multiple Data) is exactly the situation I'm in: one instruction (ex: multiply) applied to multiple data
lanes at once. So instead of processing
sample `i`, I process samples `i, i+1, i+2, i+3` together.

Two ways to get there:

- **Auto-vectorization** : restructure the loop to be "compiler-friendly" and hope
  `-O3` vectorizes it. Easy, but often fails on RNG loops (the state dependency
  confuses the compiler), and you learn less because you can't see what happened.
- **Intrinsics** : write the vector operations explicitly with functions that map
  one-to-one to vector instructions. Harder, fully in your control, and you see
  exactly what the CPU does. This is the "learn it properly" path, and the one I
  want.

**The catch:** SIMD wants four `(x, y)` values at once, but a PRNG's outputs are
*sequential*, which means each state depends on the previous, so I can't get four independent
values in one step from the current generator. To feed four lanes I need a PRNG
variant that produces four streams in parallel.

### Solution: 4-Lane LCG with Decoupled $X$ and $Y$ Streams

To saturate a 128-bit NEON vector register (which holds $4 \times 32$-bit integers), we advance 4 independent
pseudo-random streams in lockstep using a Linear Congruential Generator (LCG):

$$\text{state}_{t+1} = (\text{state}_t \times M + C) \pmod{2^{32}}$$

With Numerical Recipes constants ($M = 1664525, C = 1013904223$), the multiplication overflow natively
computes $\pmod{2^{32}}$ for free in 32-bit registers.



#### Key Architectural Components:

1. **Decoupled Seeding (Avoiding 2D Correlation Collapse)**:
   If $X$ and $Y$ share the same seed or stream, all points collapse onto the line $y = x$, and $\pi$ erroneously
   converges to $4 / \sqrt{2} \approx 2.8284$.
   We use `splitmix32(z)` with $z$ passed by reference to generate 8 consecutive, non-overlapping initial seeds: 4 for
   `statex` and 4 for `statey`.

2. **Pipelined Vector Generator (`next_float4`)**:
   ```cpp
   static inline float32x4_t next_float4(uint32x4_t &state, uint32x4_t M, uint32x4_t C, float32x4_t inv_scale) {
       state = vmlaq_u32(C, state, M);                             // 1. Advance 4 states with fused multiply-add
       uint32x4_t top24x = vshrq_n_u32(state, 8);                  // 2. Extract top 24 bits
       float32x4_t f = vcvtq_f32_u32(top24x);                      // 3. Convert to 32-bit float
       return vmulq_f32(f, inv_scale);                             // 4. Multiply by reciprocal (2^-24)
   }
   ```
    * **Why shift 8 bits?** Single-precision IEEE-754 floats have 23 explicit mantissa bits + 1 implicit leading bit =
      24 bits of precision. Keeping the top 24 bits prevents rounding noise.
    * **Why reciprocal multiply?** Multiplication on ARM NEON is fully pipelined with a 1-cycle throughput.
      Floating-point division takes 10–15 cycles.

3. **Fused Multiply-Add (FMA) Geometry Check**:
   ```cpp
   float32x4_t r = vmulq_f32(x, x);
   r = vmlaq_f32(r, y, y); // r = x*x + y*y in a single fused instruction
   ```

4. **Branchless Two's-Complement Accumulation**:
   `vcleq_f32(r, 1.0f)` returns an all-ones bitmask (`0xFFFFFFFF` $\equiv -1$) for hits, and `0x0` for misses.
   Subtracting this mask (`hits = vsubq_u32(hits, mask)`) computes $\text{hits} - (-1) = \text{hits} + 1$ without any
   branch misprediction penalties.

5. **Horizontal Vector Reduction**:
   After the loop, `vaddvq_u32(hits)` performs a hardware horizontal reduction across the 4 lanes to yield the scalar
   total hit count.

---

## v4 — Multi-core (OpenMP)

v4 keeps the v3 SIMD kernel byte-for-byte and only spreads its iterations across cores with OpenMP, so any speedup is
*pure parallelism* — nothing else changed. (v4 on one thread matches v3 to within noise, which is the proof of that.)

**How it works.** `#pragma omp parallel` forks a team of threads from a reused pool (not new OS threads per call); each
thread seeds its own RNG from `omp_get_thread_num()`, runs its slice of the loop, and `reduction(+:hits)` gives every
thread a private counter that the runtime sums once at the end — accumulate locally, combine once, so there is no false
sharing on the global count.

**Prediction → result.** The M1 has **4 performance + 4 efficiency cores**. I hoped for ~8×; I measured ~4.7×. Instead of
guessing why, I benchmarked each thread count to locate the gap:

| Threads | speedup vs 1 thread | efficiency |
|:-------:|:-------------------:|:----------:|
| 2 | 1.98× | 99% |
| 4 (P-cores) | 3.74× | 94% |
| 8 (4P + 4E) | 4.70× | 59% |

**The finding.** Scaling is near-linear up to the 4 *performance* cores (94% efficiency). The 4 *efficiency* cores then
add real but **sub-linear** throughput — 8 threads gives 4.70×, not 8× — because an E-core is much slower than a P-core.
The "missing" speedup isn't a bug in the code; it's the heterogeneous chip. (macOS also gives no way to pin threads to
specific cores, so which thread lands on which core is the scheduler's call.)

---

## v5 — GPU (CUDA)

v4 saturated the CPU, so v5 changes the *machine*: a GPU trades a handful of fast cores for **thousands of slow ones**.
The same LCG kernel, rewritten in CUDA and run on a **Tesla T4** (Google Colab), reaches **180 Gsample/s** — about **12×
the best CPU (v4)** and ~2,650× v0.

### What makes it a GPU program (not just parallel)

- **SIMT, not SIMD.** Threads run in lock-step groups of 32 (a *warp*): I write plain scalar code and the hardware runs
  32 lanes at once. It's v3's SIMD idea, but the hardware does the vectorizing for me.
- **Latency hiding by oversubscription.** Each thread's LCG is a slow dependent chain. The GPU hides that by keeping
  thousands of threads resident and switching warps whenever one stalls — which is exactly why a GPU needs a *lot* of
  work to be fast.
- **Grid-stride loop.** A fixed grid (256 × 256 = 65,536 threads) walks the array in strides of `total_threads`, so one
  launch covers any `N`.
- **One `atomicAdd` per thread.** Each thread keeps a private `local` count and adds it to the global counter *once*, at
  the end — the same "combine once" idea as v4's reduction, so atomic contention stays negligible.

### What I chose to measure

There are two honest numbers. **Kernel-only time** (`cudaEvent` around the launch) is pure compute — the fair match to
the CPU `ns/sample`, which was also pure compute. **End-to-end time** would add `cudaMalloc` and the copy back (the
"real world" number). For Monte Carlo π they're nearly identical — the only host↔device transfer is a single counter —
so I report **kernel-only**. The first launch is slow because CUDA does one-time context setup, so I run a throwaway
**warm-up** first, then time.

### Results (Tesla T4, kernel time)

| Samples | Kernel time | Throughput | Approx π |
|--------:|------------:|-----------:|:--------:|
| $10^6$ | 0.109 ms | 9 Gsample/s | 3.141536 |
| $10^7$ | 0.121 ms | 83 Gsample/s | 3.141678 |
| $10^8$ | 0.565 ms | 177 Gsample/s | 3.141682 |
| $10^9$ | 5.549 ms | **180 Gsample/s** | 3.141667 |

Notice the **warm-up curve**: at $10^6$ the GPU manages only ~9 Gsample/s — 65,536 threads over a fixed launch cost have
too little work to amortize — and throughput climbs until it saturates near $10^8$–$10^9$. That is the defining trait of
a **throughput device**: the CPU wins the small jobs (low latency), the GPU wins the big ones. The per-thread seed here
is a single multiply (not `splitmix`), which is why the GPU's π error (~7×10⁻⁵) is a touch higher than the CPU's.


### Empirical Results

![v3 benchmark in CLion](images/v3_clion_output.png)

| Version | Device | Approx π | ns/sample | Throughput | Speedup |
|:-------:|:------:|:--------:|----------:|-----------:|:-------:|
| v0 | M1 (1 core) | 3.141610 | 14.713 | 0.068 G/s | 1.00× |
| v1 | M1 (1 core) | 3.141617 | 2.144 | 0.47 G/s | 6.8× |
| v2 | M1 (1 core) | 3.141617 | 2.127 | 0.47 G/s | 6.9× |
| v3 | M1 (1 core) | 3.141582 | 0.316 | 3.16 G/s | 46× |
| **v4** | M1 (8 cores) | 3.141578 | **0.067** | **14.9 G/s** | **217×** |
| **v5** | Tesla T4 GPU | 3.141667 | **0.0055** | **180 G/s** | **~2,650×** |

*All at $N = 10^9$. **Throughput** = 1 / (ns per sample) — the samples-per-second the kernel sustains, and the most
intuitive column: 68 million/s (v0) climbs to 180 billion/s (v5). ns/sample is min-of-10 for the CPU, single-run for the
GPU. v4's per-thread-count scaling (1/2/4/8) is in the v4 section; v5 across sample sizes is in the v5 section. v5 is a
different device (T4 GPU vs M1 laptop), so its speedup compares devices, not the same silicon.*

### Microarchitectural Analysis: Cracking the Sub-Nanosecond Barrier

* **Clock Cycle Budget**: On an Apple M1 Firestorm core running at $\approx 3.2\text{ GHz}$, 1
  cycle $\approx 0.3125\text{ ns}$.
  $$\text{Cycles per Sample} = \frac{0.316\text{ ns}}{0.3125\text{ ns}} \approx \mathbf{1.01\text{ cycles}}$$
  Because each iteration processes 4 samples, one loop iteration executes
  in $\approx \mathbf{4.05\text{ clock cycles}}$.
* **Vector IPC (Instructions Per Cycle)**: The hot loop executes ~12 vector instructions (2 LCG steps, 2 bit-shifts, 2
  conversions, 2 multiplications, 1 FMA, 1 comparison, 1 subtraction, loop overhead) in ~6.5 cycles, sustaining an
  impressive Vector IPC of $\approx \mathbf{3.0}$.
* **Zero Cache Interactions**: All 4 states, constants, and accumulator registers reside completely inside NEON
  registers `q0–q31`. The spread across 10 runs at $N=10^9$ is just **$0.0013\text{ ns}$**, demonstrating zero cache
  thrashing or pipeline stalls.

### Engineering Trade-Offs

* **LCG vs PCG32**: LCG is extremely cheap (a single fused multiply-add) and fits SIMD naturally. However, in
  higher-dimensional Monte Carlo problems, LCG suffers from the Marsaglia defect (points falling on parallel
  hyperplanes). For 2D $\pi$ with separate initial seeds, accuracy is preserved ($\epsilon \approx 10^{-5}$ at $10^9$),
  but scientific simulations often prefer counter-based PRNGs (like Philox).
* **Instruction Portability**: Intrinsics in `<arm_neon.h>` are specific to ARM64. Supporting x86 architectures requires
  mapping to AVX2/AVX-512 (`_mm256_*`) or utilizing cross-platform SIMD wrappers like Google Highway.

---

## Appendix — Concrete Benchmarking Practices

Repetition + reporting the minimum (see [`Approach.md`](images/Approach.md)) handles
random noise. There's also systematic noise you kill at the source:

- **Warmup runs.** The first run has cold caches, the CPU hasn't ramped to full
  clock, and memory pages aren't faulted in, so it runs always slower and unrepresentative.
  Run a few and throw them away, then measure. (My harness does 2 warmups.)
- **Enough work per measurement.** The timer has its own overhead and granularity.
  If a run takes microseconds, timer noise dominates; aim for tens of
  milliseconds+ per timed run so timer error is negligible.
- **Pin what you can.** Serious rigs disable frequency scaling ("turbo"), pin the
  process to one core, and run on an idle machine. On a Mac I can't fully control
  these, so the honest move is: close other apps, run on wall power (battery
  throttles), and *name* the fact that I couldn't pin frequency as a known
  limitation. Stating your limitations is itself part of rigor.
