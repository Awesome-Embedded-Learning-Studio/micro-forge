# Version header from the project VERSION (single source of truth):
# bump the project() version, rebuild, and every TU sees the new triple
# through micro_forge/version.hpp.
set(MICRO_FORGE_VERSION       ${PROJECT_VERSION})
set(MICRO_FORGE_VERSION_MAJOR ${PROJECT_VERSION_MAJOR})
set(MICRO_FORGE_VERSION_MINOR ${PROJECT_VERSION_MINOR})
set(MICRO_FORGE_VERSION_PATCH ${PROJECT_VERSION_PATCH})
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/include/micro_forge/version.hpp.in
    ${CMAKE_CURRENT_BINARY_DIR}/generated/micro_forge/version.hpp
    @ONLY
)
