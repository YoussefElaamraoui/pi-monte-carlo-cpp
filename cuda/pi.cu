/* Cuda code with versions 5.1
 *
 * The idea is simple, the threads have a global and a shared memory for each block of threads
 * The global is accessed by different threads in a serial manner, by using atomicAdd, this prevents memory errors
 * however this 
 *
 */

#include <cstdio>
#include <cstdint>

__device__ float rng(uint32_t &state) {
    state = state * 1664525u + 1013904223u;
    return (state >> 8) * 0x1p-24f;
}

// 1) baseline: one atomic per thread
__global__ void mc_atomic(long n, unsigned long long *count) {
    int id = blockIdx.x*blockDim.x + threadIdx.x, stride = gridDim.x*blockDim.x;
    uint32_t state = 42u + id*2654435761u;
    unsigned int local = 0;
    for (long i = id; i < n; i += stride) {
        float x = rng(state), y = rng(state);
        if (x*x + y*y <= 1.0f) local++;
    }
    atomicAdd(count, (unsigned long long)local);
}

// 2) v5.1: block reduction in shared memory, one atomic per BLOCK
__global__ void mc_reduce(long n, unsigned long long *count) {
    int id = blockIdx.x*blockDim.x + threadIdx.x, stride = gridDim.x*blockDim.x;
    uint32_t state = 42u + id*2654435761u;
    unsigned int local = 0;
    for (long i = id; i < n; i += stride) {
        float x = rng(state), y = rng(state);
        if (x*x + y*y <= 1.0f) local++;
    }
    __shared__ unsigned int tile[256];           // block size = 256
    tile[threadIdx.x] = local;
    __syncthreads();
    for (int s = blockDim.x/2; s > 0; s >>= 1) { // tree reduction
        if (threadIdx.x < s) tile[threadIdx.x] += tile[threadIdx.x + s];
        __syncthreads();
    }
    if (threadIdx.x == 0) atomicAdd(count, (unsigned long long)tile[0]);
}

// 3) ablation:  each thread writes its own global slot
__global__ void mc_ablation(long n, unsigned long long *out) {
    int id = blockIdx.x*blockDim.x + threadIdx.x, stride = gridDim.x*blockDim.x;
    uint32_t state = 42u + id*2654435761u;
    unsigned int local = 0;
    for (long i = id; i < n; i += stride) {
        float x = rng(state), y = rng(state);
        if (x*x + y*y <= 1.0f) local++;
    }
    out[id] = local;                             // one write per thread, no contention
}

int main() {
    const int BLOCKS = 256, THREADS = 256, TOTAL = BLOCKS*THREADS;
    unsigned long long *d_count, *d_out;
    cudaMalloc(&d_count, sizeof(unsigned long long));
    cudaMalloc(&d_out, TOTAL * sizeof(unsigned long long));

    cudaEvent_t s, e; cudaEventCreate(&s); cudaEventCreate(&e);

    cudaMemset(d_count, 0, sizeof(unsigned long long));
    mc_atomic<<<BLOCKS,THREADS>>>(1'000'000, d_count);
    cudaDeviceSynchronize();

    long sizes[] = {1'000'000, 10'000'000, 100'000'000, 1'000'000'000};
    printf("%-14s %10s %12s %12s %12s\n", "N", "pi", "atomic ms", "reduce ms", "ablat ms");

    for (long n : sizes) {
        float t_atomic, t_reduce, t_ablat;
        unsigned long long h_count;

        cudaMemset(d_count, 0, sizeof(unsigned long long));
        cudaEventRecord(s); mc_atomic<<<BLOCKS,THREADS>>>(n, d_count);
        cudaEventRecord(e); cudaEventSynchronize(e); cudaEventElapsedTime(&t_atomic, s, e);
        cudaMemcpy(&h_count, d_count, sizeof(unsigned long long), cudaMemcpyDeviceToHost);
        double pi = 4.0 * (double)h_count / (double)n;

        cudaMemset(d_count, 0, sizeof(unsigned long long));
        cudaEventRecord(s); mc_reduce<<<BLOCKS,THREADS>>>(n, d_count);
        cudaEventRecord(e); cudaEventSynchronize(e); cudaEventElapsedTime(&t_reduce, s, e);

        cudaEventRecord(s); mc_ablation<<<BLOCKS,THREADS>>>(n, d_out);
        cudaEventRecord(e); cudaEventSynchronize(e); cudaEventElapsedTime(&t_ablat, s, e);

        printf("%-14ld %10.5f %12.3f %12.3f %12.3f\n", n, pi, t_atomic, t_reduce, t_ablat);
    }

    cudaEventDestroy(s); cudaEventDestroy(e);
    cudaFree(d_count); cudaFree(d_out);
    return 0;
}