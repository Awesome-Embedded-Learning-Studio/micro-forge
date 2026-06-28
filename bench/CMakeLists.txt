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
endif()
