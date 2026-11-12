cmake_minimum_required(VERSION 3.25)

project(micro-forge
    VERSION 0.1.0
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_compile_options(-Wall -Wextra -Werror)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Generate the version header from the project VERSION (single source of
# truth), following the legacy arch_details.hpp precedent.
set(MICRO_FORGE_VERSION       ${PROJECT_VERSION})
set(MICRO_FORGE_VERSION_MAJOR ${PROJECT_VERSION_MAJOR})
set(MICRO_FORGE_VERSION_MINOR ${PROJECT_VERSION_MINOR})
set(MICRO_FORGE_VERSION_PATCH ${PROJECT_VERSION_PATCH})
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/include/micro_forge/version.hpp.in
    ${CMAKE_CURRENT_BINARY_DIR}/generated/micro_forge/version.hpp
    @ONLY
)

# Engine library: the shared machine kernel, grown batch by batch.
option(MICRO_FORGE_EMBEDDED "Freestanding-ish build: no exceptions, no RTTI" OFF)

add_library(micro_forge
    src/loader/cinux_loader.cpp
    src/machine.cpp
    src/engine.cpp
    src/base/jit.cpp
    src/base/jit_translate.cpp
    src/base/jit_translate_alu.cpp
    src/base/jit_translate_mov.cpp
    src/base/wasm_emit.cpp
    src/base/wasm_block.cpp
    src/presets/cinux/cinux_machine.cpp
    src/kernel/bus.cpp
    src/kernel/acpi_tables.cpp
    src/kernel/ahci.cpp
    src/kernel/io_apic.cpp
    src/kernel/lapic.cpp
    src/kernel/pic_8259.cpp
    src/kernel/pci_config.cpp
    src/kernel/pit_8254.cpp
    src/kernel/port_space.cpp
    src/kernel/register_region.cpp
    src/kernel/rtc.cpp
    src/kernel/ram_region.cpp
    src/kernel/serial_16550.cpp
    src/kernel/scheduler.cpp
    src/arch/x86_64/decoder.cpp
    src/arch/x86_64/tables.cpp
    src/arch/x86_64/render.cpp
    src/arch/x86_64/cpu.cpp
    src/arch/x86_64/memory.cpp
    src/arch/x86_64/fast_dispatch.cpp
    src/arch/x86_64/jit_alu.cpp
    src/arch/x86_64/jit_control.cpp
    src/arch/x86_64/jit_cpu.cpp
    src/arch/x86_64/execute.cpp
    src/arch/x86_64/dispatch.cpp
    src/arch/x86_64/interrupt.cpp
    src/arch/x86_64/io.cpp
    src/arch/x86_64/grp3.cpp
    src/arch/x86_64/sysregs.cpp
    src/arch/x86_64/sse.cpp
    src/arch/x86_64/sse_ext.cpp
    src/arch/x86_64/sse_dispatch.cpp
    src/arch/x86_64/syscall_impl.cpp
    src/arch/x86_64/mmu.cpp
)
target_include_directories(micro_forge PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/generated>
)
# Internal headers (src/**) are PRIVATE: consumers must not include them.
target_include_directories(micro_forge PRIVATE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
)

# Embedded profile: the engine already avoids exceptions and RTTI internally
# (std::expected everywhere), so the flags mainly guard against regressions
# and let the same sources build for freestanding-ish targets.
if(MICRO_FORGE_EMBEDDED)
    target_compile_options(micro_forge PUBLIC -fno-exceptions -fno-rtti)
    target_compile_definitions(micro_forge PUBLIC MICRO_FORGE_EMBEDDED=1)
endif()

enable_testing()
add_subdirectory(tests)

# ips bench (batch 3 onward): advisory perf baselines, see bench/.
add_subdirectory(bench)

# Decode-dump tool for the objdump differential harness (batch 2).
add_executable(mf-decode-dump tools/dec_dump.cpp)
target_link_libraries(mf-decode-dump PRIVATE micro_forge)

# Interactive Cinux shell: COM1 wired to the terminal.
add_executable(mf-cinux-play tools/cinux_play.cpp)
target_link_libraries(mf-cinux-play PRIVATE micro_forge)

# --- Install / export: micro_forge as an embeddable library -------------
# Consumers get the public headers, the static library, and a package
# config: find_package(micro_forge CONFIG REQUIRED), then
# target_link_libraries(<app> PRIVATE micro_forge::micro_forge).
include(GNUInstallDirs)
install(TARGETS micro_forge
    EXPORT micro_forgeTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
install(DIRECTORY include/micro_forge
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)
install(EXPORT micro_forgeTargets
    FILE micro_forgeTargets.cmake
    NAMESPACE micro_forge::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/micro_forge
)
