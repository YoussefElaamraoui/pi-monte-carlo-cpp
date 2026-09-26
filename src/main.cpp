#include "versions.hpp"
#include "bench.hpp"

int main() {
    const long sizes[] = {1'000'000, 10'000'000, 100'000'000, 1'000'000'000};
    const Version versions[] = {
        {"v0", mc_pi_v0}, // Baseline
        {"v1", mc_pi_v1}, // PCG32
        {"v2", mc_pi_v2}, // No branch
        {"v3", mc_pi_v3},
        {"v4-1t", mc_pi_v4_1t},
        {"v4-2t", mc_pi_v4_2t},
        {"v4-4t", mc_pi_v4_4t},
        {"v4-8t", mc_pi_v4_8t},

    };

    // size of trick -> total bytes / bytes per version (versions are all equal in size) -> therefore it
    // return just the number of elements. On c++ version >17 you can include "#include <iterator>"
    // and just use size(versions)
    run_table(versions, sizeof(versions) / sizeof(versions[0]), sizes, sizeof(sizes) / sizeof(sizes[0]));
    return 0;
}
