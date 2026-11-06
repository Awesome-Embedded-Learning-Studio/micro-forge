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
