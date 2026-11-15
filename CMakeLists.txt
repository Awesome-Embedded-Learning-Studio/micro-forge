cmake_minimum_required(VERSION 3.25)

project(micro-forge
    VERSION 0.1.0
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ---------------------------------------------------------------------------
# Compile profile: release by default, debug on purpose.
#
# An emulator is pure compute; an unoptimized build is roughly an order of
# magnitude slower and only worth building for source-level debugging.
# cmake/compile_config/<profile>.cmake owns the flags per profile. The
# mapping is two-way by build type: Debug takes the debug profile, every
# other type (Release, RelWithDebInfo, ...) takes the release profile, so
# profiling builds keep the bench methodology's -O2 from CMake itself.
# ---------------------------------------------------------------------------
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    include(cmake/compile_config/debug.cmake)
else()
    include(cmake/compile_config/release.cmake)
endif()

find_program(MICRO_FORGE_PYTHON3 python3 REQUIRED)

# Generated headers (JIT templates, machine description): one uniform
# mechanism in cmake/tools/generators.cmake.
include(cmake/tools/generators.cmake)

include(cmake/meta/version.cmake)

add_subdirectory(src)

if(EMSCRIPTEN)
    # Wasm configure: the engine artifact only. Host tools and the test
    # suite do not cross-compile meaningfully; native configure builds them.
    add_subdirectory(wasm)
else()
    add_subdirectory(tests)
    add_subdirectory(tools)

    # example/: guest-specific glue (the Cinux loader shim and its terminal
    # front end). The core library must not know about it.
    add_subdirectory(example)
endif()

# --- Install / export: micro_forge as an embeddable library -------------
# Consumers get the public headers, the static library, and a package
# config: find_package(micro_forge CONFIG REQUIRED), then
# target_link_libraries(<app> PRIVATE micro_forge::micro_forge).
if(NOT EMSCRIPTEN)
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
endif()
