# tools/: host executables that consume the library (src/ holds the
# library itself; example/ holds guest-specific front ends).

# Decode-dump tool for the objdump differential harness (batch 2).
add_executable(mf-decode-dump dec_dump.cpp)
target_link_libraries(mf-decode-dump PRIVATE micro_forge)

# ips bench (batch 3 onward): advisory perf baselines, live in
# tools/bench/baseline.txt.
add_subdirectory(bench)

# STM32 runner: load an ELF/bin, run it, report LED edges and console.
add_executable(mf-stm32-run stm32-run/stm32_run.cpp)
target_link_libraries(mf-stm32-run PRIVATE micro_forge_stm32)
