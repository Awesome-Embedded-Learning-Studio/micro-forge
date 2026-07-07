cmake_minimum_required(VERSION 3.25)

project(micro-forge
    VERSION 0.1.0
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(MICRO_FORGE_ARCH_BIT 32 CACHE STRING "Target architecture bit width (32 or 64)")
set(ARCH_BIT ${MICRO_FORGE_ARCH_BIT})

option(MICRO_FORGE_COVERAGE "Instrument micro_forge with gcov for coverage (COVERAGE-METHODOLOGY §2; build in a dedicated dir, e.g. -B build-cov -DMICRO_FORGE_COVERAGE=ON)" OFF)

add_compile_options(-Wall -Wextra -Werror)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/include/autogen/arch_details.hpp.in
    ${CMAKE_CURRENT_BINARY_DIR}/autogen/arch_details.hpp
    @ONLY
)

set(MICRO_FORGE_SOURCES
    src/arch/arm/cortex_m3/cortex_m3.cpp
    src/arch/arm/cortex_m3/cortex_m3_thumb16.cpp
    src/arch/arm/cortex_m3/cortex_m3_thumb16_dataproc.cpp
    src/arch/arm/cortex_m3/cortex_m3_thumb16_loadstore.cpp
    src/arch/arm/cortex_m3/cortex_m3_thumb32.cpp
    src/arch/arm/cortex_m3/cortex_m3_thumb32_dataproc.cpp
    src/arch/arm/cortex_m3/cortex_m3_thumb32_loadstore.cpp
    src/arch/arm/cortex_m3/cortex_m3_interrupt.cpp
    src/arch/arm/cortex_m3/cortex_m3_reset.cpp
    src/arch/toy/cpu.cpp
    src/chips/machine.cpp
    src/chips/stm32f1/interrupt_config.cpp
    src/chips/stm32f1/memory_bus.cpp
    src/chips/stm32f1/peripheral_config.cpp
    src/chips/stm32f1/stm32f103_soc.cpp
    src/chips/stm32f1/stm32f1_afio.cpp
    src/chips/stm32f1/stm32f1_exti.cpp
    src/chips/stm32f1/stm32f1_flash.cpp
    src/chips/stm32f1/stm32f1_gpio.cpp
    src/chips/stm32f1/stm32f1_rcc.cpp
    src/chips/stm32f1/stm32f1_timer.cpp
    src/chips/stm32f1/stm32f1_usart.cpp
    src/cpu/cpu.cpp
    src/loader/binary_loader.cpp
    src/loader/elf_loader.cpp
    src/loader/utils/elf_parser.cpp
    src/memory/bus.cpp
    src/memory/flat_memory.cpp
    src/periph/nvic.cpp
    src/periph/scb.cpp
    src/periph/systick.cpp
    src/sim/coordinator.cpp
    src/sim/virtual_clock.cpp
    src/tools/memory_dump.cpp
    src/tools/mmio_trace.cpp
    src/util/logger.cpp
    src/cli/introspection.cpp
)

add_library(micro_forge STATIC ${MICRO_FORGE_SOURCES})
target_include_directories(micro_forge PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_BINARY_DIR}
)

# gcov instrumentation (COVERAGE-METHODOLOGY §2): instrument the library under
# test only — PRIVATE compile flags stay off gtest and the test drivers, while an
# INTERFACE link option pulls libgcov into every consumer so the test exes flush
# .gcda at exit. Opt-in (default OFF) to keep the day-to-day build fast.
if(MICRO_FORGE_COVERAGE)
    target_compile_options(micro_forge PRIVATE -fprofile-arcs -ftest-coverage)
    target_link_options(micro_forge INTERFACE --coverage)
    message(STATUS "micro_forge: gcov instrumentation ON — report via 'source scripts/use-python.sh && gcovr'")
endif()

# Unified CLI runner.
add_executable(micro-forge src/cli/main.cpp src/cli/snapshot.cpp)
target_link_libraries(micro-forge PRIVATE micro_forge)

enable_testing()
add_subdirectory(examples)
add_subdirectory(test)
add_subdirectory(bench)

# ── GUI dashboard (milestone 04; see DIRECTIVES §E) ──
# Opt-in: Qt6 is a heavy runtime dep, so the GUI is a separate target that
# defaults OFF and never enters the core `micro_forge` library. The core and
# CLI stay zero-dep; only micro-forge-gui links Qt6::Widgets. Build in a
# dedicated dir, e.g. -B build-gui -DMICRO_FORGE_GUI=ON.
option(MICRO_FORGE_GUI "Build the Qt6 dashboard (milestone 04)" OFF)
if(MICRO_FORGE_GUI)
    find_package(Qt6 REQUIRED COMPONENTS Widgets)
    set(CMAKE_AUTOMOC ON)
    add_executable(micro-forge-gui src/gui/main.cpp)
    target_link_libraries(micro-forge-gui PRIVATE micro_forge Qt6::Widgets)
endif()
