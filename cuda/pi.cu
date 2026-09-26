// Monte Carlo pi on the GPU (v5). Developed in Google Colab on an NVIDIA Tesla T4.
// Build & run:  nvcc -arch=sm_75 -O3 pi.cu -o pi && ./pi

// Cuda code
#include <cstdio>
#include <cstdint>

// runs on the GPU, called by the kernel. Same LCG as v3, scalar.
__device__ float rng(uint32_t &state) {
    state = state * 1664525u + 1013904223u;
    return (state >> 8) * 0x1p-24f;
}

// global -> keyword that marks function being run on GPU
__global__ void mc_kernel(long n_samples, unsigned long long *global_count) {
    int id = blockIdx.x * blockDim.x + threadIdx.x;   // this thread's global id
    int total_threads = gridDim.x * blockDim.x;       // how many threads exist

    uint32_t state = 42u + id * 2654435761u;   // seed: different per thread
    long local = 0;                            // this thread's own hit count

    // grid-stride loop
    for (long i = id; i < n_samples; i += total_threads) {
        float x = rng(state);
        float y = rng(state);
        if (x*x + y*y <= 1.0f) local++;
    }

    // add this thread's hits into the shared global total
    atomicAdd(global_count, (unsigned long long) local);
}

int main() {

  // Mental model : in orderd to understand this code better
    // you have to think about the CPU and GPU working with their own variables
    // in order to have them work on the same ones, you have to explicitly
    // share them

    long sizes[] = {1'000'000, 10'000'000, 100'000'000, 1'000'000'000};
    int n_sizes = 4;

    // allocate ONE counter in GPU memory ( naming habit d_ = "device" = GPU)
    unsigned long long *d_count;
    cudaMalloc(&d_count, sizeof(unsigned long long));

    // create two GPU timestamps ("events")
    cudaEvent_t start, stop;
    cudaEventCreate(&start);
    cudaEventCreate(&stop);

    // warmup: one throwaway run so the timed runs don't pay setup cost
    cudaMemset(d_count, 0, sizeof(unsigned long long));

    // lunch the functino, specifying the
    // number of threads needed 256 blocks x 256 threads
    mc_kernel<<<256, 256>>>(1'000'000, d_count);


    cudaDeviceSynchronize();// wait for the GPU to finish

    printf("%-14s %14s %12s %12s\n", "samples", "pi", "time (ms)", "ns/sample");

    for (int s = 0; s < n_sizes; ++s) {
        long n = sizes[s];

        cudaMemset(d_count, 0, sizeof(unsigned long long));  // reset counter to 0

        cudaEventRecord(start);                    // timestamp: GPU start
        mc_kernel<<<256, 256>>>(n, d_count);
        cudaEventRecord(stop);                     // timestamp: GPU stop
        cudaEventSynchronize(stop);                // wait until 'stop' is reached

        float ms = 0.0f;
        cudaEventElapsedTime(&ms, start, stop);    // ms between the two timestamps

        unsigned long long h_count;
        cudaMemcpy(&h_count, d_count, sizeof(unsigned long long), cudaMemcpyDeviceToHost);

        double pi = 4.0 * (double) h_count / (double) n;
        double ns_per_sample = (ms * 1e6) / (double) n;   // ms -> ns, per sample

        printf("%-14ld %14.6f %12.3f %12.4f\n", n, pi, ms, ns_per_sample);
    }

    cudaEventDestroy(start);
    cudaEventDestroy(stop);
    cudaFree(d_count);
    return 0;
}
