# The wasm engine artifact, configured with the Emscripten toolchain
# (emcmake). It links the SAME micro_forge library target the native
# build uses — one source list, two hosts.
#
# Native configure: cmake -S . -B build          (wasm target not built)
# Wasm configure:   emcmake cmake -S . -B build-wasm
# Build:            cmake --build build-wasm
#
# Usage (no wrapper script):
#   emcmake cmake -S . -B build-wasm
#   cmake --build build-wasm
#
# Produce: build-wasm/engine.wasm — STANDALONE_WASM, no Emscripten JS
# glue, single file. The exported linear memory is guest physical memory
# from offset 0 (spec 5.4); engine statics and heap sit above the guest
# window via --global-base, and the maximum is pinned at 4 GiB so guest
# accesses inside the envelope never need explicit bounds checks.

# Guest RAM comes from the machine description's [wasm] section, so the
# CMake recipe and the C++ side cannot disagree. CMake variables are NOT
# interpolated into the python source: the machine name travels via env,
# keeping quotes and braces out of each other's hair.
set(MICRO_FORGE_MACHINE "cinux" CACHE STRING "Machine description in machines/")
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env
        "MICRO_FORGE_MACHINE=${MICRO_FORGE_MACHINE}"
        "${MICRO_FORGE_PYTHON3}" "${CMAKE_SOURCE_DIR}/scripts/gen/read_wasm_ram.py"
    OUTPUT_VARIABLE GUEST_RAM_MB
    RESULT_VARIABLE ram_result
)
if(NOT ram_result EQUAL 0)
    message(FATAL_ERROR "cannot read wasm guest RAM from machines/${MICRO_FORGE_MACHINE}.toml")
endif()
string(STRIP "${GUEST_RAM_MB}" GUEST_RAM_MB)

math(EXPR GUEST_END "${GUEST_RAM_MB} * 1024 * 1024 + 0x400000") # + LFB window
math(EXPR GLOBAL_BASE "${GUEST_END} + 0x1000000")               # + 16 MiB margin
math(EXPR INITIAL_MEMORY "${GLOBAL_BASE} + 0x1000000")          # + heap headroom

# LTO: LLVM must see the hot loop and helpers across translation units.
# Keep an opt-out for controlled comparisons.
set(WASM_LTO "1" CACHE STRING "1 = -flto, 0 = plain")
set(WASM_PROFILE "0" CACHE STRING "1 = --profiling-funcs for V8 profiles")

# Export list derives from the sources (scripts/gen/gen_wasm_exports.py),
# so the ABI surface and the link line cannot drift apart. _malloc/_free
# are the Emscripten allocator entries the standalone module needs.
set(EXPORTS_OUT "${CMAKE_BINARY_DIR}/exports.list")
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env
        "WASM_EXPORTS_OUT=${EXPORTS_OUT}"
        "${MICRO_FORGE_PYTHON3}" "${CMAKE_SOURCE_DIR}/scripts/gen/gen_wasm_exports.py"
    RESULT_VARIABLE exports_result
)
if(NOT exports_result EQUAL 0)
    message(FATAL_ERROR "wasm export list generation failed")
endif()
file(STRINGS "${EXPORTS_OUT}" EXPORT_NAMES)
list(TRANSFORM EXPORT_NAMES PREPEND "_")
list(JOIN EXPORT_NAMES "," EXPORTS_CSV)

add_executable(mf-engine
    ${CMAKE_SOURCE_DIR}/src/wasm/exports.cpp
    ${CMAKE_SOURCE_DIR}/src/wasm/instance_table.cpp
    ${CMAKE_SOURCE_DIR}/src/wasm/bridge.cpp
    ${CMAKE_SOURCE_DIR}/src/wasm/block_runner.cpp
    ${CMAKE_SOURCE_DIR}/example/cinux/cinux_loader.cpp
)
target_link_libraries(mf-engine PRIVATE micro_forge)
# The engine links the Cinux loader shim from example/ directly; the
# example's own targets (play) are native-only.
target_include_directories(mf-engine PRIVATE ${CMAKE_SOURCE_DIR}/example)
set_target_properties(mf-engine PROPERTIES
    OUTPUT_NAME "engine"
    SUFFIX ".wasm"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)

# The engine already avoids exceptions and RTTI (std::expected everywhere);
# the flags mainly guard against regressions in the freestanding target.
target_compile_options(mf-engine PRIVATE -fno-exceptions -fno-rtti)
if(WASM_LTO)
    target_compile_options(mf-engine PRIVATE -flto)
    target_link_options(mf-engine PRIVATE -flto)
endif()

target_link_options(mf-engine PRIVATE
    -sSTANDALONE_WASM
    --no-entry
    "-sINITIAL_MEMORY=${INITIAL_MEMORY}"
    "-sMAXIMUM_MEMORY=4GB"
    -sALLOW_MEMORY_GROWTH=1
    "-sTOTAL_STACK=262144"
    "-sEXPORTED_FUNCTIONS=${EXPORTS_CSV},_malloc,_free"
    "-Wl,--global-base=${GLOBAL_BASE}"
    -Wl,--export-memory
    -Wl,--growable-table
)
if(WASM_PROFILE)
    target_link_options(mf-engine PRIVATE --profiling-funcs)
endif()
