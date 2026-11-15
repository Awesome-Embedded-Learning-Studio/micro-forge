# tools/: host executables that consume the library (src/ holds the
# library itself; example/ holds guest-specific front ends).

# Decode-dump tool for the objdump differential harness (batch 2).
add_executable(mf-decode-dump dec_dump.cpp)
target_link_libraries(mf-decode-dump PRIVATE micro_forge)

# ips bench (batch 3 onward): advisory perf baselines, live in
# tools/bench/baseline.txt.
add_subdirectory(bench)
