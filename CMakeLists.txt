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
)

add_library(micro_forge STATIC ${MICRO_FORGE_SOURCES})
target_include_directories(micro_forge PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_BINARY_DIR}
)

# Unified CLI runner.
add_executable(micro-forge src/cli/main.cpp src/cli/snapshot.cpp)
target_link_libraries(micro-forge PRIVATE micro_forge)

enable_testing()
add_subdirectory(examples)
add_subdirectory(test)
add_subdirectory(bench)
