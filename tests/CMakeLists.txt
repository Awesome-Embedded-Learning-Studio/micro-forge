include(FetchContent)

FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.14.0
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

include(GoogleTest)

add_executable(test_smoke test_smoke.cpp)
target_link_libraries(test_smoke
    PRIVATE micro_forge GTest::gtest_main
)
# Second channel of the same single source of truth: the test asserts the
# generated header against a compile definition taken from project VERSION.
target_compile_definitions(test_smoke PRIVATE
    TEST_EXPECTED_VERSION="${PROJECT_VERSION}"
)
gtest_discover_tests(test_smoke)

add_executable(test_bus test_bus.cpp)
target_link_libraries(test_bus
    PRIVATE micro_forge GTest::gtest_main
)
gtest_discover_tests(test_bus)

add_executable(test_scheduler test_scheduler.cpp)
target_link_libraries(test_scheduler
    PRIVATE micro_forge GTest::gtest_main
)
gtest_discover_tests(test_scheduler)

add_executable(test_bench test_bench.cpp)
target_link_libraries(test_bench
    PRIVATE micro_forge GTest::gtest_main
)
gtest_discover_tests(test_bench)

add_executable(test_decode test_decode.cpp)
target_link_libraries(test_decode
    PRIVATE micro_forge GTest::gtest_main
)
gtest_discover_tests(test_decode)

add_executable(test_cpu test_cpu.cpp)
target_link_libraries(test_cpu
    PRIVATE micro_forge GTest::gtest_main
)
gtest_discover_tests(test_cpu)

add_executable(test_fast_dispatch test_fast_dispatch.cpp)
target_link_libraries(test_fast_dispatch PRIVATE micro_forge GTest::gtest_main)
gtest_discover_tests(test_fast_dispatch)

add_executable(test_system test_system.cpp)
target_link_libraries(test_system
    PRIVATE micro_forge GTest::gtest_main
)
gtest_discover_tests(test_system)

add_executable(test_interrupt test_interrupt.cpp)
target_link_libraries(test_interrupt
    PRIVATE micro_forge GTest::gtest_main
)
gtest_discover_tests(test_interrupt)

add_executable(test_mmu test_mmu.cpp)
target_link_libraries(test_mmu
    PRIVATE micro_forge GTest::gtest_main
)
gtest_discover_tests(test_mmu)

# Batch-2 acceptance corpus: assemble the bare client with GNU as and
# flatten it to a raw binary at build time. -j .text matters: endbr64
# makes as emit a .note.gnu.property section, and without the section
# filter objcopy flattens the note ahead of _start, so the corpus starts
# mid-program (found by the batch-3 executor, invisible to the decode
# gate because note bytes also decode).
add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/bare-client.bin
    COMMAND as --64 -o ${CMAKE_CURRENT_BINARY_DIR}/bare-client.o
            ${CMAKE_CURRENT_SOURCE_DIR}/data/bare-client.s
    COMMAND objcopy -O binary -j .text ${CMAKE_CURRENT_BINARY_DIR}/bare-client.o
            ${CMAKE_CURRENT_BINARY_DIR}/bare-client.bin
    DEPENDS data/bare-client.s
    COMMENT "assembling bare client corpus"
    VERBATIM
)
add_custom_target(bare_client_bin DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/bare-client.bin)

add_executable(test_bare_client test_bare_client.cpp)
target_link_libraries(test_bare_client
    PRIVATE micro_forge GTest::gtest_main
)
target_compile_definitions(test_bare_client PRIVATE
    BARE_CLIENT_BIN="${CMAKE_CURRENT_BINARY_DIR}/bare-client.bin"
)
add_dependencies(test_bare_client bare_client_bin)
gtest_discover_tests(test_bare_client)

# Batch-3 acceptance: the same corpus, executed end to end.
add_executable(test_execute test_execute.cpp)
target_link_libraries(test_execute
    PRIVATE micro_forge GTest::gtest_main
)
target_compile_definitions(test_execute PRIVATE
    BARE_CLIENT_BIN="${CMAKE_CURRENT_BINARY_DIR}/bare-client.bin"
)
add_dependencies(test_execute bare_client_bin)
gtest_discover_tests(test_execute)

# Batch-5 acceptance (native side): the Machine assembly.
add_executable(test_engine test_engine.cpp)
target_link_libraries(test_engine PRIVATE micro_forge GTest::gtest_main)
gtest_discover_tests(test_engine)

add_executable(test_wasm_block test_wasm_block.cpp)
target_link_libraries(test_wasm_block PRIVATE micro_forge GTest::gtest_main)
gtest_discover_tests(test_wasm_block)

add_executable(test_wasm_emit test_wasm_emit.cpp)
target_link_libraries(test_wasm_emit PRIVATE micro_forge GTest::gtest_main)
gtest_discover_tests(test_wasm_emit)

add_executable(test_jit test_jit.cpp)
target_link_libraries(test_jit PRIVATE micro_forge GTest::gtest_main)
gtest_discover_tests(test_jit)

add_executable(test_jit_diff test_jit_diff.cpp jit_diff_cases.cpp test_jit_alu_layout.cpp)
target_link_libraries(test_jit_diff PRIVATE micro_forge GTest::gtest_main)
# Keep the native differential baseline non-PIE; PIE also supports helpers.
target_link_options(test_jit_diff PRIVATE -no-pie)
gtest_discover_tests(test_jit_diff)

add_executable(test_cache test_cache.cpp)
target_link_libraries(test_cache PRIVATE micro_forge GTest::gtest_main)
gtest_discover_tests(test_cache)

add_executable(test_machine test_machine.cpp)
target_link_libraries(test_machine
    PRIVATE micro_forge GTest::gtest_main
)
target_compile_definitions(test_machine PRIVATE
    BARE_CLIENT_BIN="${CMAKE_CURRENT_BINARY_DIR}/bare-client.bin"
)
add_dependencies(test_machine bare_client_bin)
gtest_discover_tests(test_machine)

# Batch-4 acceptance: interrupt line and snapshot round-trip.
add_executable(test_irq test_irq.cpp)
target_link_libraries(test_irq
    PRIVATE micro_forge GTest::gtest_main
)
gtest_discover_tests(test_irq)

add_executable(test_snapshot test_snapshot.cpp)
target_link_libraries(test_snapshot
    PRIVATE micro_forge GTest::gtest_main
)
target_compile_definitions(test_snapshot PRIVATE
    BARE_CLIENT_BIN="${CMAKE_CURRENT_BINARY_DIR}/bare-client.bin"
)
add_dependencies(test_snapshot bare_client_bin)
gtest_discover_tests(test_snapshot)

# Style gate: per-file line limit (default 600, see scripts/style/).
add_test(NAME style_line_limit
         COMMAND ${CMAKE_COMMAND} -E env PYTHONIOENCODING=utf-8
                 python3 ${CMAKE_CURRENT_SOURCE_DIR}/../scripts/style/check_line_limit.py)
set_tests_properties(style_line_limit PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
