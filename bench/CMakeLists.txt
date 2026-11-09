# ips bench: guest loop images assembled with GNU as, one target per
# scenario plus the harness. Baselines live in bench/baseline.txt and are
# recorded from RelWithDebInfo builds on pinned cores (see the file).

function(assemble_guest name)
    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${name}.bin
        COMMAND as --64 -o ${CMAKE_CURRENT_BINARY_DIR}/${name}.o
                ${CMAKE_CURRENT_SOURCE_DIR}/${name}.s
        COMMAND objcopy -O binary -j .text ${CMAKE_CURRENT_BINARY_DIR}/${name}.o
                ${CMAKE_CURRENT_BINARY_DIR}/${name}.bin
        DEPENDS ${name}.s
        COMMENT "assembling bench guest ${name}"
        VERBATIM
    )
    add_custom_target(${name}_bin DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/${name}.bin)
endfunction()

assemble_guest(guest-int-alu)
assemble_guest(guest-mem-mix)

add_executable(mf-bench bench.cpp)
target_link_libraries(mf-bench PRIVATE micro_forge)
add_dependencies(mf-bench guest-int-alu_bin guest-mem-mix_bin)

# Keep a non-PIE baseline for comparison with a separately linked PIE probe.
add_executable(mf-jit-kernel-probe jit_kernel_probe.cpp)
target_link_libraries(mf-jit-kernel-probe PRIVATE micro_forge)
target_link_options(mf-jit-kernel-probe PRIVATE -no-pie)
