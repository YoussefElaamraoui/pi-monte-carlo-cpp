#include <cstdlib>
#include <omp.h>
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
    randValues.seed(42, 54); //matching srand
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

        count += (x * x + y * y <= 1.0);
    }

    return 4.0 * count / n_samples;
}


// Version 3 :

// Mixer for my states ( copied, just know what it does however you don't need to know how to write it
// it's a fixed, known-good bit-manipulation recipe). Just notices z gets passed by reference
static inline uint32_t splitmix32(uint64_t &z) {
    z += 0x9E3779B97F4A7C15ULL; // advance the counter by a fixed odd step
    uint64_t t = z;
    t = (t ^ (t >> 30)) * 0xBF58476D1CE4E5B9ULL; // mix bits
    t = (t ^ (t >> 27)) * 0x94D049BB133111EBULL; // mix more
    return (uint32_t) (t ^ (t >> 31)); // final mix, return low 32 bits
}


// Values of next state
static inline float32x4_t next_float4(uint32x4_t &state,
                                      uint32x4_t M,
                                      uint32x4_t C,
                                      float32x4_t inv_scale) {
    // multiply then add, in one single operation -> state = state * M + C
    state = vmlaq_u32(C, state, M);

    // a float holds up just 2^24 values, this is the reason behind the shift.
    // why ? -> float32 = 1 sign bit | 8 exponent bits | 23 mantissa (1 implicit leading bit)
    uint32x4_t top24x = vshrq_n_u32(state, 8);

    // converting into float
    float32x4_t f = vcvtq_f32_u32(top24x);


    // instead of dividing by 2^24 multiply, by the reciprocal,
    //  it's faster, the multiplication by floats is fully pipelined while the division is not
    return vmulq_f32(f, inv_scale);
}


double mc_pi_v3(long n_samples) {
    const float32x4_t one = vdupq_n_f32(1.0f);
    const float32x4_t inv_scale = vdupq_n_f32(0x1p-24f);

    // populating the registers with the same number in the 4 lanes.
    // dup -> duplication, broadcasts the same values in all the lanes
    const uint32x4_t M = vdupq_n_u32(1664525);
    const uint32x4_t C = vdupq_n_u32(1013904223);


    // seeding
    uint64_t z = 42;
    uint32_t sx[4] = {splitmix32(z), splitmix32(z), splitmix32(z), splitmix32(z)};

    // Seeding in the same function with same seed to ensure it advances through the NEXT 4 steps
    // Ex : x has 0,1,2,3  ---(by using same seeding y advances  ) --- y has 4,5,6,7
    uint32_t sy[4] = {splitmix32(z), splitmix32(z), splitmix32(z), splitmix32(z)};


    // take all the values and put them in their lanes in the registry
    uint32x4_t statex = vld1q_u32(sx);
    uint32x4_t statey = vld1q_u32(sy);


    const long vec_iters = n_samples / 4; // processing 4 at the same time, this is the reason behind / 4
    uint32x4_t hits = vdupq_n_u32(0);


    // advancing on the next values and making comparisons to count the hits
    for (long i = 0; i < vec_iters; ++i) {
        float32x4_t x = next_float4(statex, M, C, inv_scale);
        float32x4_t y = next_float4(statey, M, C, inv_scale);

        float32x4_t r = vmulq_f32(x, x);
        r = vmlaq_f32(r, y, y);

        // Comparison: r <= 1.0f gives 0xFFFFFFFF for inside, 0 for outside
        uint32x4_t mask = vcleq_f32(r, one);

        // Accumulate: in two's complement, 0xFFFFFFFF is -1.
        // Subtracting -1 is equivalent to +1!
        hits = vsubq_u32(hits, mask);
    }


    uint64_t total_hits = vaddvq_u32(hits);

    return 4.0 * total_hits / n_samples;
}


// Version 4 : Multi core (OpenMP Multi-Core)
// As already said, simple because the cores do not share data, just in the end there is the sum of hits
double mc_pi_v4(long n_samples) {
    const float32x4_t one = vdupq_n_f32(1.0f);
    const float32x4_t inv_scale = vdupq_n_f32(0x1p-24f);
    const uint32x4_t M = vdupq_n_u32(1664525);
    const uint32x4_t C = vdupq_n_u32(1013904223);

    const long total_vec_iters = n_samples / 4;
    uint64_t global_hits = 0;

#pragma omp parallel reduction(+:global_hits)
    {
        int tid = omp_get_thread_num();

        uint64_t z = 42 + tid;

        uint32_t sx[4] = {splitmix32(z), splitmix32(z), splitmix32(z), splitmix32(z)};
        uint32_t sy[4] = {splitmix32(z), splitmix32(z), splitmix32(z), splitmix32(z)};

        uint32x4_t statex = vld1q_u32(sx);
        uint32x4_t statey = vld1q_u32(sy);

        uint32x4_t local_hits = vdupq_n_u32(0);

#pragma omp for schedule(static)
        for (long i = 0; i < total_vec_iters; ++i) {
            float32x4_t x = next_float4(statex, M, C, inv_scale);
            float32x4_t y = next_float4(statey, M, C, inv_scale);

            float32x4_t r = vmulq_f32(x, x);
            r = vfmaq_f32(r, y, y);

            uint32x4_t mask = vcleq_f32(r, one);
            local_hits = vsubq_u32(local_hits, mask);
        }

        global_hits += vaddvq_u32(local_hits);
    }

    return 4.0 * (double) global_hits / n_samples;
}


// Wrappers to call the Version 4 with different number of threads
double mc_pi_v4_1t(long n) {
    omp_set_num_threads(1);
    return mc_pi_v4(n);
}

double mc_pi_v4_2t(long n) {
    omp_set_num_threads(2);
    return mc_pi_v4(n);
}

double mc_pi_v4_4t(long n) {
    omp_set_num_threads(4);
    return mc_pi_v4(n);
}

double mc_pi_v4_8t(long n) {
    omp_set_num_threads(8);
    return mc_pi_v4(n);
}
