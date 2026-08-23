#pragma once
#include <vector>
#include <algorithm>
#include <chrono>

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

inline void run_table(const Version* versions, int n_versions,const long* sizes, int n_sizes) {

    for (int si = 0; si < n_sizes; ++si) {
        const long n = sizes[si];

        // Time the baseline (versions[0]) first, to compute speedups.
        const Stats base = time_it(versions[0].fn, n);

        std::printf("\n--- n = %ld ---\n", n);
        std::printf("%-10s %12s %12s %10s %12s %9s\n",
            "version", "approx", "min ns/smp", "time (s)", "spread ns", "speedup");

        // Comparing the stats with baseline
        for (int vi = 0; vi < n_versions; ++vi) {
            const Stats s = time_it(versions[vi].fn, n);
            const double min_time_s = s.min_s;   // the fastest run's wall-clock seconds
            const double min_ns    = s.min_s    * 1e9 / n;
            const double med_ns    = s.median_s * 1e9 / n;
            const double spread_ns = s.spread_s * 1e9 / n;
            const double speedup   = base.min_s / s.min_s;   // >1 means faster than v0
            const double approx = s.approximation;


            std::printf("%-10s %12.6f %12.3f %10.3f %12.3f %12.4f %8.2fx\n",
                        versions[vi].name, approx, min_ns, min_time_s,med_ns, spread_ns, speedup);
        }


    }
}