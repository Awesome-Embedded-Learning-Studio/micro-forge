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

# Style gate: per-file line limit (default 600, see scripts/style/).
add_test(NAME style_line_limit
         COMMAND ${CMAKE_COMMAND} -E env PYTHONIOENCODING=utf-8
                 python3 ${CMAKE_CURRENT_SOURCE_DIR}/../scripts/style/check_line_limit.py)
set_tests_properties(style_line_limit PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
