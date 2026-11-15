# example/ — guest-specific glue that the core library must not know
# about. Each subdirectory is one guest: its loader shim, its machine
# preset, and any tool that boots exactly that guest.

# The Cinux example: boot shim (long-mode stage) plus the machine preset.
# Public include root is example/ so consumers write "cinux/cinux_*.hpp".
add_library(micro_forge_cinux STATIC
    cinux/cinux_loader.cpp
    cinux/cinux_machine.cpp
)
target_include_directories(micro_forge_cinux PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(micro_forge_cinux PUBLIC micro_forge)

add_executable(mf-cinux-play cinux/cinux_play.cpp)
target_link_libraries(mf-cinux-play PRIVATE micro_forge_cinux)
