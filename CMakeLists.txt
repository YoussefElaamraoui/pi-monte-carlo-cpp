cmake_minimum_required(VERSION 3.20)
project(Pi-Montecarlo-optimisation CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(pi_bench src/main.cpp src/versions.cpp
        src/versions.hpp
        src/bench.hpp)

# Turning warnings on
target_compile_options(pi_bench PRIVATE -Wall -Wextra)