# micro_forge library: the shared machine kernel. Sources live here so
# the list grows beside the files it lists; the root CMakeLists stays
# free of directory structure.
add_library(micro_forge
    machine.cpp
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
    kernel/bus/bus.cpp
    kernel/device/acpi_tables.cpp
    kernel/device/ahci.cpp
    kernel/device/debug_exit.cpp
    kernel/device/fw_cfg.cpp
    kernel/device/i8042.cpp
    kernel/device/io_apic.cpp
    kernel/device/lapic.cpp
    kernel/device/pic_8259.cpp
    kernel/device/pci_config.cpp
    kernel/device/pit_8254.cpp
    kernel/bus/port_space.cpp
    kernel/bus/port_stubs.cpp
    kernel/memory/register_region.cpp
    kernel/device/rtc.cpp
    kernel/memory/ram_region.cpp
    kernel/device/serial_16550.cpp
    kernel/scheduler.cpp
    kernel/snapshot.cpp
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

# The generated headers must exist before the library compiles.
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
    target_compile_options(micro_forge PUBLIC -fno-exceptions -fno-rtti)
endif()
