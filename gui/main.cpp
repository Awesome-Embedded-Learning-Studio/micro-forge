// micro-forge GUI dashboard — Qt6 Widgets entry point.
//
// Milestone 04: pure consumer of introspection::read_introspection(); the simulator
// core stays single-threaded and runs in this same Qt main thread (never on
// a QThread). See DIRECTIVES §E and notes 031–034.
//
// Usage: micro-forge-gui [firmware.{elf,bin}]
#include "gui/main_window.hpp"

#include <QApplication>

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    const QString firmware = (argc >= 2) ? QString::fromLocal8Bit(argv[1]) : "";
    micro_forge::gui::MainWindow win(firmware);
    win.show();
    return app.exec();
}
