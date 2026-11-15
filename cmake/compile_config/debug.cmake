# Debug profile: source-level debugging only. Build this profile on
# purpose; an emulator here is roughly an order of magnitude slower.
add_compile_options(-O0 -g)
add_compile_definitions(MICRO_FORGE_DEBUG=1)
