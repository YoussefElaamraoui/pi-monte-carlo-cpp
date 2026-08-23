#include <cstdlib>
#include "versions.hpp"

/*Baseline version*/
double mc_pi_v0(long n_samples) {
    std::srand(42);
    long count = 0;

    for (long i = 0; i < n_samples; ++i) {
        // int / int would truncate to 0 or 1; the cast forces real division
        const double x = std::rand() / static_cast<double>(RAND_MAX);
        const double y = std::rand() / static_cast<double>(RAND_MAX);

        if (x * x + y * y <= 1.0) ++count;
    }

    // 4.0 (not 4) so the whole expression is floating-point
    return 4.0 * count / n_samples;
}



/* Changes from the previous vers: now using PCG32 instead of rand*/
double mc_pi_v1(long n_samples) {
    PCG32 randValues;
    randValues.seed(42, 54);//matching srand
    long count = 0;

    for (long i = 0; i < n_samples; ++i) {
        // int / int would truncate to 0 or 1; the cast forces real division
        const double x = randValues.nextFloat();
        const double y = randValues.nextFloat();

        if (x * x + y * y <= 1.0) ++count;
    }

    return 4.0 * count / n_samples;
}

/* Changes from the previous vers: No branch (if statement) inside the for */
double mc_pi_v2(long n_samples) {
    PCG32 randValues;
    randValues.seed(42, 54);
    long count = 0;

    for (long i = 0; i < n_samples; ++i) {
        const double x = randValues.nextFloat();
        const double y = randValues.nextFloat();

        count += (x*x + y*y <= 1.0);
    }

    return 4.0 * count / n_samples;
}

