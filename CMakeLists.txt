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
add_library(micro_forge
    src/kernel/bus.cpp
    src/kernel/ram_region.cpp
    src/kernel/scheduler.cpp
    src/arch/x86_64/decoder.cpp
    src/arch/x86_64/tables.cpp
    src/arch/x86_64/render.cpp
)
target_include_directories(micro_forge PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/generated>
)
# Internal headers (src/**) are PRIVATE: consumers must not include them.
target_include_directories(micro_forge PRIVATE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
)

enable_testing()
add_subdirectory(tests)

# Decode-dump tool for the objdump differential harness (batch 2).
add_executable(mf-decode-dump tools/dec_dump.cpp)
target_link_libraries(mf-decode-dump PRIVATE micro_forge)
