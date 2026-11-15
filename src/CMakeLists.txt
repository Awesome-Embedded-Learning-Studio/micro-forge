# Architectural kernels are shared; each concrete machine has its own library.
add_library(micro_forge_kernel
    kernel/bus/bus.cpp
    kernel/memory/register_region.cpp
    kernel/memory/ram_region.cpp
    kernel/scheduler.cpp
    kernel/snapshot.cpp
)
add_library(micro_forge::kernel ALIAS micro_forge_kernel)
set_target_properties(micro_forge_kernel PROPERTIES EXPORT_NAME kernel)
target_include_directories(micro_forge_kernel PUBLIC
    $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/generated>
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

# Device-family engines shared by every machine's device models.
add_library(micro_forge_device_base
    device/base/uart.cpp
    device/base/countdown.cpp
    device/base/device_region.cpp
)
add_library(micro_forge::device_base ALIAS micro_forge_device_base)
set_target_properties(micro_forge_device_base PROPERTIES EXPORT_NAME device_base)
target_link_libraries(micro_forge_device_base PUBLIC micro_forge_kernel)
target_include_directories(micro_forge_device_base PUBLIC
    $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/generated>
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

# Compatibility target: existing callers still link micro_forge for the PC.
add_library(micro_forge
    machines/x86_pc.cpp
    base/active_machine.cpp
    engine.cpp
    jit/jit.cpp
    jit/jit_translate.cpp
    jit/jit_translate_alu.cpp
    jit/jit_translate_mov.cpp
    jit/jit_translate_movx.cpp
    wasm/wasm_emit.cpp
    wasm/wasm_block.cpp
    wasm/wasm_block_mem.cpp
    wasm/wasm_block_walk.cpp
    wasm/wasm_block_stack.cpp
    device/pc/acpi_tables.cpp
    device/pc/ahci.cpp
    device/pc/debug_exit.cpp
    device/pc/fw_cfg.cpp
    device/pc/i8042.cpp
    device/pc/io_apic.cpp
    device/pc/lapic.cpp
    device/pc/pic_8259.cpp
    device/pc/pci_config.cpp
    device/pc/pit_8254.cpp
    arch/x86_64/port_space.cpp
    device/pc/port_stubs.cpp
    device/pc/rtc.cpp
    device/pc/serial_16550.cpp
    arch/x86_64/decoder.cpp
    arch/x86_64/tables.cpp
    arch/x86_64/render.cpp
    arch/x86_64/cpu.cpp
    arch/x86_64/memory.cpp
    arch/x86_64/fast_dispatch.cpp
    arch/x86_64/jit_alu.cpp
    arch/x86_64/jit_chain.cpp
    arch/x86_64/jit_control.cpp
    arch/x86_64/jit_cpu.cpp
    arch/x86_64/jit_cpu_alu.cpp
    arch/x86_64/jit_helpers.cpp
    arch/x86_64/jit_stats.cpp
    arch/x86_64/execute.cpp
    arch/x86_64/execute_dispatch.cpp
    arch/x86_64/dispatch.cpp
    arch/x86_64/interrupt.cpp
    arch/x86_64/io.cpp
    arch/x86_64/grp3.cpp
    arch/x86_64/sysregs.cpp
    arch/x86_64/sse.cpp
    arch/x86_64/sse_ext.cpp
    arch/x86_64/sse_dispatch.cpp
    arch/x86_64/syscall_impl.cpp
    arch/x86_64/mmu.cpp
)

target_link_libraries(micro_forge PUBLIC micro_forge_device_base)

# This target can be linked without any PC device or x86 execution object.
add_library(micro_forge_stm32
    machines/stm32f103.cpp
    machines/stm32f103_elf.cpp
    machines/stm32f103_semihost.cpp
    arch/base/memory_access.cpp
    arch/arm/cortex_m3/cortex_m3.cpp
    arch/arm/cortex_m3/cortex_m3_interrupt.cpp
    arch/arm/cortex_m3/cortex_m3_reset.cpp
    arch/arm/cortex_m3/cortex_m3_thumb16.cpp
    arch/arm/cortex_m3/cortex_m3_thumb16_dataproc.cpp
    arch/arm/cortex_m3/cortex_m3_thumb16_loadstore.cpp
    arch/arm/cortex_m3/cortex_m3_thumb32.cpp
    arch/arm/cortex_m3/cortex_m3_thumb32_dataproc.cpp
    arch/arm/cortex_m3/cortex_m3_thumb32_loadstore.cpp
    device/arm/nvic.cpp
    device/arm/scb.cpp
    device/arm/systick.cpp
    device/stm32f1/stm32f1_afio.cpp
    device/stm32f1/stm32f1_exti.cpp
    device/stm32f1/stm32f1_flash.cpp
    device/stm32f1/stm32f1_gpio.cpp
    device/stm32f1/stm32f1_rcc.cpp
    device/stm32f1/stm32f1_timer.cpp
    device/stm32f1/stm32f1_usart.cpp
)
add_library(micro_forge::stm32 ALIAS micro_forge_stm32)
set_target_properties(micro_forge_stm32 PROPERTIES EXPORT_NAME stm32)
target_link_libraries(micro_forge_stm32 PUBLIC micro_forge_device_base)

# The generated headers must exist before the PC library compiles.
add_dependencies(micro_forge jit_templates_gen machine_config_gen)
target_include_directories(micro_forge PUBLIC
    $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/generated>
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
# Internal headers (src/**) are PRIVATE: consumers must not include them.
target_include_directories(micro_forge PRIVATE ${CMAKE_SOURCE_DIR}/src)

# Embedded profile: the engine already avoids exceptions and RTTI
# internally (std::expected everywhere), so the flags mainly guard
# against regressions and let the same sources build freestanding-ish.
option(MICRO_FORGE_EMBEDDED "Freestanding-ish build: no exceptions, no RTTI" OFF)
if(MICRO_FORGE_EMBEDDED)
    foreach(target micro_forge_kernel micro_forge micro_forge_stm32)
        target_compile_options(${target} PUBLIC -fno-exceptions -fno-rtti)
    endforeach()
endif()
