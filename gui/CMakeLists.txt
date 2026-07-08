# GUI dashboard — Qt6 Widgets consumer of micro_forge (milestone 04; DIRECTIVES §E).
#
# Entered only when MICRO_FORGE_GUI=ON (guarded in the root CMakeLists). The
# core library stays zero-dep; only this target links Qt6::Widgets. The sim
# runs synchronously in the Qt main thread — never on a QThread (deterministic
# replay). AUTOMOC is needed because MainWindow uses the Q_OBJECT macro.
find_package(Qt6 REQUIRED COMPONENTS Widgets)
set(CMAKE_AUTOMOC ON)

add_executable(micro-forge-gui
    main.cpp
    main_window.cpp
    model/session.cpp
    view/board_view/board_view.cpp
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
target_link_libraries(micro-forge-gui PRIVATE micro_forge Qt6::Widgets)
