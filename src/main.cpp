#include "versions.hpp"
#include "bench.hpp"

int main() {
    const long sizes[] = {1'000'000, 10'000'000, 100'000'000, 1'000'000'000};
    const Version versions[] = {
        {"v0", mc_pi_v0},
        {"v1", mc_pi_v1},
        {"v2", mc_pi_v2},

    };

    run_table(versions, 3, sizes, 4);
    return 0;
}
