// micro-forge GUI dashboard — Qt6 Widgets skeleton (G5a).
//
// Milestone 04: the GUI is a pure consumer of cli::read_introspection(); the
// simulator core stays single-threaded and runs in this same Qt main thread
// (never on a QThread — that would break deterministic replay). See
// DIRECTIVES §E and notes 031–033.
//
// This skeleton only proves the toolchain: Qt6 + micro_forge link, a window
// opens under WSLg. The CPU/GPIO panels and the run loop land in G5b/G5c.
#include <QApplication>
#include <QLabel>
#include <QMainWindow>

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QMainWindow win;
    win.setWindowTitle("micro-forge");
    auto* label = new QLabel(
        "micro-forge GUI dashboard\n"
        "G5a skeleton — Qt6 Widgets + micro_forge core linked.\n"
        "(CPU/GPIO panels land in G5b/G5c)");
    label->setAlignment(Qt::AlignCenter);
    win.setCentralWidget(label);
    win.resize(480, 240);
    win.show();
    return app.exec();
}
