# The approach I took

This is the method I use throughout the whole project. These are also some
concepts I learned (or learned properly) while working on it.

>See [`Versions.md`](../Versions.md) for how I turn this into actual benchmarking
practice: warmup runs, enough work per measurement, and controlling the
conditions I can.


## 1. Measure first, trust the measurement second

I don't want to optimize based on guesses. The process is always the same:

1. Establish a trustworthy baseline.
2. Profile it and find where the time *actually* goes.
3. Change one thing.
4. Measure again.
5. Repeat until I hit a wall I actually understand.

The profiler is what keeps steps 2–4 honest. My intuition about "what's slow" can
be wrong pretty easily, and optimizing based on it is a good way to spend a day
making something faster that was never the bottleneck in the first place.

## 2. A single run is noisy

Two identical runs don't necessarily take the same time. Frequency scaling, the
OS scheduler, and background processes can easily interfere, so I can get
different results even when I didn't change anything in the code.

The solution is simple: run each case several times and report the **minimum**.

I use the minimum because the program can't really run *faster* than its actual
cost. It can only be slowed down by something external. So the fastest run is
the one that was probably disturbed the least.

The average has a different problem: every slow outlier pushes it up. One
scheduler hiccup, some background process, or a temporary frequency change can
pollute the result even though none of those things are part of the code I'm
trying to measure.


