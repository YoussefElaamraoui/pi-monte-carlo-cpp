#pragma once
#include <arm_neon.h>
#include<cstdint>

double mc_pi_v0(long n_samples);
double mc_pi_v1(long n_samples);
double mc_pi_v2(long n_samples);
double mc_pi_v3(long n_samples); // NEON SIMD, Apple Silicon (ARM64) only




class PCG32 {
private:
    const uint64_t N = 6364136223846793005;
    uint64_t state = 0x853c49e6748fea9b;
    uint64_t inc = 0xda3e39cb94b95bdb;

public:
    uint32_t nextInt() {
        uint64_t old = state;
        state = old * N + inc;
        uint32_t shifted = (uint32_t) (((old >> 18) ^ old) >> 27);
        uint32_t rot = old >> 59;
        return (shifted >> rot) | (shifted << ((~rot + 1) & 31));
    }

    double nextFloat() {
        return ((double) nextInt()) / (1LL << 32);
    }

    void seed(uint64_t seed_state, uint64_t seed_sequence) {
        state = 0;
        inc = (seed_sequence << 1) | 1;
        nextInt();
        state = state + seed_state;
        nextInt();
    }
};


