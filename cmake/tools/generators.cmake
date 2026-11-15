# Generated headers, one uniform mechanism.
#
# Every generated header follows the same recipe: a python script, an
# output path, and optional environment. Each runs once at configure
# time (the file must exist so include paths and install(DIRECTORY)
# see it) and once per build (so editing the source data refreshes it
# without a reconfigure). Regenerating unconditionally is deliberate:
# the outputs are cheap, and a stale JIT template header means every
# hole offset in the block compiler is silently wrong.

function(micro_forge_generated_header)
    set(options)
    set(one NAME SCRIPT OUT)
    # (OUT lands under the build tree's generated/ include root)
    set(multi ENV)
    cmake_parse_arguments(GEN "${options}" "${one}" "${multi}" ${ARGN})

    get_filename_component(gen_dir "${GEN_OUT}" DIRECTORY)
    file(MAKE_DIRECTORY "${gen_dir}")
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env ${GEN_ENV}
            "${MICRO_FORGE_PYTHON3}" "${CMAKE_CURRENT_SOURCE_DIR}/${GEN_SCRIPT}"
        RESULT_VARIABLE result
    )
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "generator '${GEN_NAME}' failed at configure time")
    endif()

    add_custom_target(
        ${GEN_NAME} ALL
        COMMAND "${CMAKE_COMMAND}" -E env ${GEN_ENV}
            "${MICRO_FORGE_PYTHON3}" "${CMAKE_CURRENT_SOURCE_DIR}/${GEN_SCRIPT}"
        COMMENT "Regenerating ${GEN_NAME} -> ${GEN_OUT}"
        VERBATIM
    )
endfunction()

# JIT templates: the assembler owns every hole offset (see the script).
micro_forge_generated_header(
    NAME jit_templates_gen
    SCRIPT scripts/gen/gen_jit_templates.py
    OUT "${CMAKE_BINARY_DIR}/generated/micro_forge/jit/jit_templates_generated.hpp"
    ENV "JIT_TEMPLATES_OUT=${CMAKE_BINARY_DIR}/generated/micro_forge/jit/jit_templates_generated.hpp"
)

# Machine description: MICRO_FORGE_MACHINE selects machines/<name>.toml.
set(MICRO_FORGE_MACHINE "cinux" CACHE STRING "Machine description in machines/")
micro_forge_generated_header(
    NAME machine_config_gen
    SCRIPT scripts/gen/gen_machine_config.py
    OUT "${CMAKE_CURRENT_SOURCE_DIR}/include/micro_forge/generated/machine_config_generated.hpp"
    ENV "MICRO_FORGE_MACHINE=${MICRO_FORGE_MACHINE}"
        "MACHINE_CONFIG_OUT=${CMAKE_CURRENT_SOURCE_DIR}/include/micro_forge/generated/machine_config_generated.hpp"
)
