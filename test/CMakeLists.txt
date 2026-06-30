include(FetchContent)

FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.14.0
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

include(GoogleTest)

# ── 基础设施测试（types + memory） ──
add_executable(test_infra
    test_types.cpp
    test_flat_memory.cpp
    test_memory_bus.cpp
)
target_link_libraries(test_infra
    PRIVATE micro_forge GTest::gtest_main
)
gtest_discover_tests(test_infra)

# ── CPU 抽象层测试（ICore + RegisterFile + ToyCore） ──
add_executable(test_cpu
    test_cpu.cpp
)
target_link_libraries(test_cpu
    PRIVATE micro_forge GTest::gtest_main
)
gtest_discover_tests(test_cpu)

# ── ARM Cortex-M3 测试 ──
add_executable(test_cortex_m3
    test_cortex_m3_lifecycle.cpp
    test_cortex_m3_basic.cpp
    test_cortex_m3_faults.cpp
    test_cortex_m3_advanced.cpp
    test_cortex_m3_loadstore.cpp
)
target_link_libraries(test_cortex_m3
    PRIVATE micro_forge GTest::gtest_main
)
gtest_discover_tests(test_cortex_m3)

# ── 芯片/平台测试（具体芯片地址映射等） ──
add_executable(test_chip
    test_stm32f103_map.cpp
)
target_link_libraries(test_chip
    PRIVATE micro_forge GTest::gtest_main
)
gtest_discover_tests(test_chip)

# ── 仿真时钟测试 ──
add_executable(test_sim
    test_virtual_clock.cpp
    test_coordinator.cpp
)
target_link_libraries(test_sim
    PRIVATE micro_forge GTest::gtest_main
)
gtest_discover_tests(test_sim)

# ── NVIC 测试 ──
add_executable(test_nvic
    test_nvic.cpp
)
target_link_libraries(test_nvic
    PRIVATE micro_forge GTest::gtest_main
)
gtest_discover_tests(test_nvic)

# ── SysTick 测试 ──
add_executable(test_systick
    test_systick.cpp
)
target_link_libraries(test_systick
    PRIVATE micro_forge GTest::gtest_main
)
gtest_discover_tests(test_systick)

# ── 中断集成测试 ──
add_executable(test_interrupt
    test_interrupt_roundtrip.cpp
)
target_link_libraries(test_interrupt
    PRIVATE micro_forge GTest::gtest_main
)
gtest_discover_tests(test_interrupt)

# ── 加载器测试 ──
add_executable(test_loader
    test_elf_loader.cpp
)
target_link_libraries(test_loader
    PRIVATE micro_forge GTest::gtest_main
)
gtest_discover_tests(test_loader)

# ── STM32F1 外设测试 ──
add_executable(test_stm32f1_periph
    test_stm32f1_periph.cpp
)
target_link_libraries(test_stm32f1_periph
    PRIVATE micro_forge GTest::gtest_main
)
gtest_discover_tests(test_stm32f1_periph)

# ── SoC 集成测试 ──
add_executable(test_soc
    test_stm32f103_soc.cpp
)
target_link_libraries(test_soc
    PRIVATE micro_forge GTest::gtest_main
)
gtest_discover_tests(test_soc)

# ── 工具测试（memory_dump + mmio_trace） ──
add_executable(test_tools
    test_tools.cpp
)
target_link_libraries(test_tools
    PRIVATE micro_forge GTest::gtest_main
)
gtest_discover_tests(test_tools)

# ── E2E 测试 ──
if(TARGET hello_firmware AND TARGET blink_firmware AND TARGET systick_firmware)
    add_executable(test_e2e
        test_e2e.cpp
    )
    target_link_libraries(test_e2e
        PRIVATE micro_forge GTest::gtest_main
    )
    target_compile_definitions(test_e2e PRIVATE
        E2E_HELLO_ELF="${CMAKE_CURRENT_BINARY_DIR}/../examples/hello_world/hello.elf"
        E2E_BLINK_ELF="${CMAKE_CURRENT_BINARY_DIR}/../examples/gpio_blink/blink.elf"
        E2E_SYSTICK_ELF="${CMAKE_CURRENT_BINARY_DIR}/../examples/systick/systick.elf"
    )
    add_dependencies(test_e2e hello_firmware blink_firmware systick_firmware)

    # Copy firmware next to test binary for easy path resolution
    add_custom_command(TARGET test_e2e POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_CURRENT_BINARY_DIR}/../examples/hello_world/hello.elf
            ${CMAKE_CURRENT_BINARY_DIR}/hello.elf
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_CURRENT_BINARY_DIR}/../examples/gpio_blink/blink.elf
            ${CMAKE_CURRENT_BINARY_DIR}/blink.elf
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_CURRENT_BINARY_DIR}/../examples/systick/systick.elf
            ${CMAKE_CURRENT_BINARY_DIR}/systick.elf
    )

    # HAL UART E2E test — requires STM32CubeF1 submodule
    if(TARGET hal_uart_firmware)
        message(STATUS "STM32CubeF1 found — enabling E2E.HalUartTransmit")
        target_compile_definitions(test_e2e PRIVATE
            E2E_HAL_UART_ELF="${CMAKE_CURRENT_BINARY_DIR}/../examples/hal_uart/firmware/hal_uart.elf"
        )
        add_dependencies(test_e2e hal_uart_firmware)
        add_custom_command(TARGET test_e2e POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${CMAKE_CURRENT_BINARY_DIR}/../examples/hal_uart/firmware/hal_uart.elf
                ${CMAKE_CURRENT_BINARY_DIR}/hal_uart.elf
        )
    else()
        message(STATUS "STM32CubeF1 submodule not found — skipping E2E.HalUartTransmit")
    endif()

    gtest_discover_tests(test_e2e)
endif()

# ── armcc/AC6 固件语料 E2E(T5c)──
# 预编译 .axf fixture 提交进 test/firmware/armcc/;CI 无 Keil 故不重建。
add_executable(test_firmware_armcc
    test_firmware_armcc.cpp
)
target_link_libraries(test_firmware_armcc
    PRIVATE micro_forge GTest::gtest_main
)
target_compile_definitions(test_firmware_armcc PRIVATE
    ARMCC_FW_DIR="${CMAKE_CURRENT_SOURCE_DIR}/firmware/armcc"
)
gtest_discover_tests(test_firmware_armcc)

# ── CLI 测试(drives the `micro-forge` binary) ──
if(TARGET micro-forge AND TARGET hello_firmware)
    add_executable(test_cli
        test_cli.cpp
    )
    target_link_libraries(test_cli
        PRIVATE GTest::gtest_main
    )
    target_compile_definitions(test_cli PRIVATE
        MICRO_FORGE_BIN="${CMAKE_BINARY_DIR}/micro-forge"
        CLI_HELLO_ELF="${CMAKE_BINARY_DIR}/examples/hello_world/hello.elf"
    )
    add_dependencies(test_cli micro-forge hello_firmware)
    gtest_discover_tests(test_cli)
endif()

# ── Introspection API 测试(结构化 snapshot,GUI/CLI 单一事实来源,milestone 04 前置)──
add_executable(test_introspection
    test_introspection.cpp
)
target_link_libraries(test_introspection
    PRIVATE micro_forge GTest::gtest_main
)
gtest_discover_tests(test_introspection)
if(TARGET hello_firmware)
    target_compile_definitions(test_introspection PRIVATE
        INTROSPECTION_HELLO_ELF="${CMAKE_CURRENT_BINARY_DIR}/hello.elf"
    )
    add_dependencies(test_introspection hello_firmware)
    add_custom_command(TARGET test_introspection POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_CURRENT_BINARY_DIR}/../examples/hello_world/hello.elf
            ${CMAKE_CURRENT_BINARY_DIR}/hello.elf
    )
endif()

# ── Meta-guard: test-discovery regression floor (COVERAGE-METHODOLOGY §3) ──
# Cheap `ctest -N` count check — fails if a refactor silently drops test
# discovery (build stays green, coverage evaporates). Bash on the Linux test
# host (WSL2); there is no Windows test path here. The script self-skips
# (exit 77) when the build dir is absent.
add_test(
    NAME test_count_floor
    COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/../scripts/check_test_count.sh ${CMAKE_BINARY_DIR}
)

# ── Meta-guard: QEMU Cortex-M3 differential oracle (COVERAGE-METHODOLOGY §4) ──
# Hardened oracle: real exit code (actual vs verified-expected), self-skips
# (exit 77) when the arm/qemu toolchain is absent, RUN_SERIAL so the gdbstub
# port never collides under `ctest -j`. A local Tier-1 reference anchor for
# taste/perf refactors — micro-forge unit tests are checked against these values.
find_program(QEMU_SYSTEM_ARM qemu-system-arm)
if(QEMU_SYSTEM_ARM)
    add_test(
        NAME oracle_cortex_m3
        COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/../scripts/qemu_cortex_m3_oracle.sh
    )
    set_tests_properties(oracle_cortex_m3 PROPERTIES RUN_SERIAL TRUE)
else()
    message(STATUS "qemu-system-arm not found — skipping oracle_cortex_m3")
endif()
