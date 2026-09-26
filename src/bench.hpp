#pragma once
#include <vector>
#include <algorithm>
#include <chrono>
#include <cstdio>

struct Stats {
    double min_s;
    double median_s;
    double spread_s;
    double approximation;
};

// ---------------------------------------------------------------------------
// THE TIMING FUNCTION
// ---------------------------------------------------------------------------
inline Stats time_it(double (*f)(long), long n, int warmup = 2, int runs = 10) {

    // Warmup: call and ignore. Their only job is to "warm" the machine.
    for (int i = 0; i < warmup; ++i) {
        volatile double sink = f(n);
        (void) sink;
    }

    std::vector<double> durations;
    durations.reserve(runs);

    double pi=0.0;
    for (int i = 0; i < runs; ++i) {
        const auto t0 = std::chrono::steady_clock::now();
        pi = f(n);        // version call
        const auto t1 = std::chrono::steady_clock::now();

        durations.push_back(std::chrono::duration<double>(t1 - t0).count());
    }



    // Sorting the stats, in order to pull them efficiently
    std::sort(durations.begin(), durations.end());

    Stats s;
    s.min_s    = durations.front();
    s.spread_s = durations.back() - durations.front();
    // median of an even count = average of the two middle elements
    s.median_s = (durations[runs / 2 - 1] + durations[runs / 2]) / 2.0;
    s.approximation = pi;
    return s;
}

// ---------------------------------------------------------------------------
// THE TABLE
// ---------------------------------------------------------------------------

// Struct in order to pass the values to the function run_table
struct Version {
    const char*      name;
    double (*fn)(long);
};

inline void run_table(const Version* versions, int n_versions, const long* sizes, int n_sizes) {

    // Legend — printed once so the columns are self-explanatory.
    std::printf("\nColumns:\n");
    std::printf("  approx  : the pi estimate (should sit near 3.14159)\n");
    std::printf("  ns/smp  : nanoseconds to process ONE sample  (min of the timed runs; lower is better)\n");
    std::printf("  Gsmp/s  : throughput, billions of samples per second  (= 1 / ns-per-sample; higher is better)\n");
    std::printf("  speedup : throughput relative to v0 (the baseline)\n");
    std::printf("  spread  : (max - min) ns/sample across the runs  (small spread = a stable measurement)\n");

    for (int si = 0; si < n_sizes; ++si) {
        const long n = sizes[si];

        // Time the baseline (versions[0]) first, to compute speedups.
        const Stats base = time_it(versions[0].fn, n);

        std::printf("\n--- n = %ld samples ---\n", n);
        std::printf("%-8s %10s %11s %10s %9s %9s\n",
                    "version", "approx", "ns/smp", "Gsmp/s", "speedup", "spread");

        for (int vi = 0; vi < n_versions; ++vi) {
            const Stats s = time_it(versions[vi].fn, n);
            const double min_ns    = s.min_s    * 1e9 / n;   // nanoseconds per sample (the fastest run)
            const double gsamples  = 1.0 / min_ns;           // throughput in billions/sec (= 1 / ns-per-sample)
            const double spread_ns = s.spread_s * 1e9 / n;   // run-to-run spread, same units
            const double speedup   = base.min_s / s.min_s;   // >1 means faster than v0

            std::printf("%-8s %10.6f %11.4f %10.3f %8.2fx %9.4f\n",
                        versions[vi].name, s.approximation, min_ns, gsamples, speedup, spread_ns);
            std::fflush(stdout);
        }
    }
}
