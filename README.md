# Performance Engineering: Accelerating Monte Carlo Pi Step-by-Step

A Monte Carlo π approximation in C++ that I keep making faster, one deliberate step
at a time — and write down the reasoning for. I'm teaching myself performance
engineering, so instead of just dumping the final code, I kept a log of what I
measured, what I guessed wrong, and what the profiler actually showed. If you're
starting out too, you can follow along and learn with me.

This project was inspired by a lecture from Mike Croucher (MathWorks) at
EUMASTER4HPC in Luxembourg, 2026.

## What is performance engineering

That lecture was my first real contact with performance engineering. I watched the
speaker take a π computation that ran for something like 27 days and bring it down
to a few minutes. A big part of that gap came from how parallel the problem is — but
the jump is what stuck with me. I found it fascinating, and maybe
you will too. Think about how many programs out there could be made faster: it's
good for energy use and the environment, it makes real projects actually usable,
and honestly it's just a puzzling, addictive kind of fun.

So, let's talk about the project.

## Why This Project Exists

Estimating $\pi$ via Monte Carlo is mathematically simple: generate random points $(x, y)$ in a
unit square and count how many land inside the inscribed quarter circle.

Because the algorithm requires almost no complex domain logic, it removes code noise and isolates hardware execution.

This repository walks through the fundamentals of performance engineering step-by-step:

1) Establishing a baseline: Measuring unoptimized, single-threaded execution and
   identifying where CPU time is actually spent.
2) Hardware-aware improvements: Moving from scalar computation to SIMD
   vectorization, improving branch predictability, and choosing efficient Pseudo-Random Number Generators (PRNGs).
3) Parallel
   execution: Scaling across CPU cores while managing thread synchronization and cache coherence (avoiding false
   sharing).
4) Empirical validation: Using profilers to verify speedup using hardware counters (IPC, cache misses, branch misses).

Every step includes the rationale, code changes, and performance deltas so you can reproduce and
observe the hardware effects on your own machine.

## Who this is for

Basically, the person I was when I started. So:

- You can read some C++ and run a compiler, but you've never sat down and
  deliberately *optimized* anything.
- You've heard "profile before you optimize" a hundred times and want to see what
  that actually looks like when someone does it.
- You'd rather learn from a tiny example where you can see cause and effect than from
  a giant codebase where the lesson is buried.

You don't need to know SIMD, PRNGs, or how a profiler works. Those are the things
you'll pick up along the way — I didn't know them either.

## How to follow along

Go through it in this order. Each doc is short, and each one leans on the last.

1. **[`docs/Approach.md`](docs/images/Approach.md)** —:the measure-first method the whole
   project runs on. This is the mindset, and if you take away one file, take this one.
2. **[`docs/Versions.md`](docs/Versions.md)** : the engineering log. Every version changes, and I wrote down a
   prediction
   *before* measuring. Try to guess each outcome before you read mine.
3. **Build it and run it yourself** (see below). Watch the table print, and get a feel for the baseline before you
   change a single line.

And really, don't just read my answers. Before each
version, guess the speedup, then check. Then run the whole thing on your machine
your numbers won't match mine, though the improvements will be there. Thus try it and figure out why you had different
results (different CPU, compiler, noise) it could be a fun challenge on its own.

## The versions

Each step isolates one idea, so whatever changes in the numbers, you know what caused
it. The full reasoning and the profiler screenshots live in
[`docs/Versions.md`](docs/Versions.md).

| Version | Change                                                | What you learn from it                                                                      |
|---------|-------------------------------------------------------|---------------------------------------------------------------------------------------------|
| **v0**  | Baseline using libc `rand()`                          | You can't improve anything until you have something correct and measured to compare against |
| **v1**  | Swap `rand()` for a small inlinable PCG32 in a header | How a plain function call can freeze the optimizer, and what inlining sets free             |
| **v2**  | Rewrite the `if` as branchless `count += (…)`         | That the compiler has often *already* done the "obvious" trick — so verify, don't assume    |

## Build & run

You'll need **CMake ≥ 3.20** and a **C++17** compiler (I use Apple Clang; GCC or Clang
on Linux are fine too).

```bash
git clone <your-clone-url>
cd Pi-Montecarlo-optimisation
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/pi_bench
```

`pi_bench` runs every version across a few sample sizes (1e6, 1e7, 1e8) and prints,
for each size, the π estimate and the min / median / spread in ns/sample, plus the
speedup over v0.

> **Always benchmark the Release build.** A Debug build isn't optimized, so its
> timings describe code the real compiler would never emit. It's the most common
> beginner mistake, and I go into why in [`docs/Versions.md`](docs/Versions.md).

### Profiling it yourself

Total time tells you *whether* you got faster; a profiler tells you *where* the time
actually goes, which is what you need to know what to fix next. The exact tools and
steps I used are in
[`docs/Versions.md` → How to profile](docs/Versions.md#how-to-profile-on-a-mac):
Xcode **Instruments** on macOS, or **`perf`** on Linux.

## Results

Apple M1 (MacBook Air, 8 GB), CLion Release build.

> Monte Carlo error shrinks like 1/√n. n = 1B → error ≈ 1/√(10⁹) ≈ 0.00003 → ~4 digits


| Version | Change             | ns/sample | vs v0 |
|---------|--------------------|-----------|-------|
| v0      | baseline, `rand()` | 14.74     | 1.00× |
| v1      | PCG32 (inlinable)  | 2.15      | 6.77× |
| v2      | branchless         | 2.15      | 6.78× |

Timing = the minimum of 10 runs
after warmup. Lower ns/sample is better.

## Repository layout

```
src/
  main.cpp       # runs every version across a few sample sizes, prints a table
  versions.cpp   
  versions.hpp   
  bench.hpp      # timing harness (warmup, repeated runs, min / median / spread)
docs/
  Approach.md    # the measure-first method 
  Versions.md    # the engineering log: what changed each version and why
CMakeLists.txt
```

## Honest limitations

Some limitations I want to acknowledge

- **It's one laptop, not a controlled rig.**:  On a MacBook I can't pin the CPU
  frequency or turn off turbo, so I do what I can, close other apps, run on wall
  power, and report the minimum of repeated runs to get as close to the true cost
  as possible. So treat the numbers as indicative, not publishable, and expect your
  own machine to give different ones.

> I'm learning as I go. If you spot something wrong, that probably means you're
> reading it properly, open an issue and I'll learn from it too.feel free to reach out and give some feedback it is
> really
> appreciataed, thank you all :)

## Tools

- **Hardware:** MacBook Air, Apple M1, 8 GB RAM
- **Profiler:** Xcode Instruments (Time Profiler), `perf` is Linux-only
- **AI (Claude):** used as a sounding board to lay out options (like the PRNG
  comparison table in the docs). The decisions and the code are mine.

## License

MIT — see [`LICENSE`](LICENSE). Use it, fork it, learn from it.
