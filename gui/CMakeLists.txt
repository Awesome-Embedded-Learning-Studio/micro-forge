# GUI dashboard — Qt6 Widgets consumer of micro_forge (milestone 04; DIRECTIVES §E).
#
# Entered only when MICRO_FORGE_GUI=ON (guarded in the root CMakeLists). The
# core library stays zero-dep; only this target links Qt6::Widgets. The sim
# runs synchronously in the Qt main thread — never on a QThread (deterministic
# replay). AUTOMOC is needed because MainWindow uses the Q_OBJECT macro.
find_package(Qt6 REQUIRED COMPONENTS Widgets)
set(CMAKE_AUTOMOC ON)

# QuarkWidgets — org-level Qt control library. The repo now holds two
# independent sub-libraries: the MD3 widget library at the root (SHARED + aex,
# for CFDesktop) and the embedded-debug controls under embedded/ (STATIC +
# QTest, zero deps — these). micro-forge consumes ONLY the embedded/ controls
# (QuarkBulb / LedPanel / UartTerminalView); pointing at the subdir avoids
# pulling in the root's aex dependency. Dev: MCUUTILS_DIR → ~/MCUUtils;
# release/CI: third_party/QuarkWidgets submodule. Wiring lives in gui/ (not the
# root CMakeLists) so the parallel-worktree collision hotspot is untouched.
set(MCUUTILS_DIR "$ENV{MCUUTILS_DIR}" CACHE PATH
    "MCUUtils monorepo root (holds QuarkWidgets/); needed for MICRO_FORGE_GUI=ON")
set(quarkwidgets_dir "${MCUUTILS_DIR}/QuarkWidgets")
if(NOT EXISTS "${quarkwidgets_dir}/embedded/CMakeLists.txt")
    # Release/CI fallback: in-repo submodule (init via git submodule update).
    set(quarkwidgets_dir "${CMAKE_SOURCE_DIR}/third_party/QuarkWidgets")
endif()
if(NOT EXISTS "${quarkwidgets_dir}/embedded/CMakeLists.txt")
    message(FATAL_ERROR
        "QuarkWidgets embedded controls not found under "
        "'${MCUUTILS_DIR}/QuarkWidgets/embedded' nor "
        "'${CMAKE_SOURCE_DIR}/third_party/QuarkWidgets/embedded'. "
        "Set MCUUTILS_DIR (e.g. export MCUUTILS_DIR=$HOME/MCUUtils) or init "
        "the third_party/QuarkWidgets submodule. Required for MICRO_FORGE_GUI=ON.")
endif()
add_subdirectory("${quarkwidgets_dir}/embedded"
                 "${CMAKE_BINARY_DIR}/quarkwidgets-embedded-build")

add_executable(micro-forge-gui
    main.cpp
    main_window.cpp
    model/session.cpp
    view/widgets/stm32_board_widget.cpp
    view/panels/registers_panel.cpp
    view/panels/gpio_panel.cpp
    view/panels/serial_panel.cpp
    view/panels/status_panel.cpp
    view/panels/fault_panel.cpp
    view/panels/peripheral_panel.cpp
    view/panels/clock_panel.cpp
    view/panels/memory_panel.cpp
)
# main.cpp / main_window.cpp include "gui/main_window.hpp" (header sits next to
# the .cpp here), so the include root is the repo top, not src/.
target_include_directories(micro-forge-gui PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(micro-forge-gui PRIVATE micro_forge Qt6::Widgets QuarkWidgets::Embedded)
