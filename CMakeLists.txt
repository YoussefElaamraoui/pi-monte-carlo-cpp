cmake_minimum_required(VERSION 3.20)
project(Pi-Montecarlo-optimisation CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(LIBOMP /opt/homebrew/opt/libomp)

add_executable(pi_bench
        src/main.cpp
        src/versions.cpp
        src/versions.hpp
        src/bench.hpp)


target_compile_options(pi_bench PRIVATE -Xclang -fopenmp -Wall -Wextra)
target_include_directories(pi_bench PRIVATE ${LIBOMP}/include)
target_link_libraries(pi_bench PRIVATE ${LIBOMP}/lib/libomp.dylib)