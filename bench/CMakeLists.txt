# bench/ — perf measurement infra (PERF-METHODOLOGY.md §3 Phase 0).
# Gated behind MICRO_FORGE_BUILD_BENCH (default OFF) so it never bloats the
# normal Debug/test build. Built separately as RelWithDebInfo in build-rel/.
option(MICRO_FORGE_BUILD_BENCH "Build the perf bench target" OFF)

if(MICRO_FORGE_BUILD_BENCH)
    # The counter inc/dec points live inside the library (bus.cpp,
    # cortex_m3.cpp), so MF_PERF_STATS must be defined while compiling the
    # library too — not just the bench translation unit. build-rel is dedicated
    # to measurement, so defining it PUBLIC here propagates to the lib without
    # touching the Debug/test build.
    target_compile_definitions(micro_forge PUBLIC MF_PERF_STATS)

    add_executable(micro-forge-bench bench_sim.cpp)
    target_link_libraries(micro-forge-bench PRIVATE micro_forge)
    # Point the bench at the committed armcc/AC6 corpus.
    target_compile_definitions(micro-forge-bench PRIVATE
        MF_BENCH_FW_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../test/firmware/armcc")

    # Google Benchmark — micro-benchmarks that localize the per-cycle cost
    # breakdown (dispatch / bus / peripheral tick) which bench_sim.cpp only
    # measures end-to-end with hand-rolled chrono. Fetched the same way test/
    # pulls GoogleTest; GB's own tests/install are disabled so they never
    # enter our build or ctest.
    include(FetchContent)
    FetchContent_Declare(
        googlebenchmark
        GIT_REPOSITORY https://github.com/google/benchmark.git
        GIT_TAG        v1.9.0
    )
    set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
    set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googlebenchmark)

    add_executable(micro-forge-bench-micro bench_micro.cpp)
    target_link_libraries(micro-forge-bench-micro PRIVATE
        micro_forge benchmark::benchmark)
endif()
