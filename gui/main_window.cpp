// micro-forge GUI main window implementation.
//
// QMainWindow shell: a toolbar (run/step/reset + state) plus dockable panels.
// The serial output panel is the central widget (main interaction surface);
// registers dock left, GPIO dock bottom. Users can drag/fold/float any dock.
// All panels refresh from the session's snapshot each tick — MainWindow owns
// no simulator state (that's in model::Session).
#include "gui/main_window.hpp"

#include "gui/view/panels/gpio_panel.hpp"
#include "gui/view/panels/registers_panel.hpp"
#include "gui/view/panels/serial_panel.hpp"

#include "cpu/cpu.hpp"

#include <QDockWidget>
#include <QLabel>
#include <QPushButton>
#include <QString>
#include <QToolBar>
#include <QTimer>

#include <cstdint>

namespace micro_forge::gui {

MainWindow::MainWindow(const QString& firmware_path, QWidget* parent)
    : QMainWindow(parent) {
    setWindowTitle("micro-forge");

    session_.set_firmware(firmware_path.toStdString());

    // ── toolbar: run / step / reset + state label ──
    auto* toolbar = addToolBar("main");
    run_btn_ = new QPushButton("Run");
    auto* step_btn = new QPushButton("Step");
    auto* reset_btn = new QPushButton("Reset");
    state_label_ = new QLabel("idle");
    state_label_->setStyleSheet("font-family: monospace;");
    toolbar->addWidget(run_btn_);
    toolbar->addWidget(step_btn);
    toolbar->addWidget(reset_btn);
    toolbar->addSeparator();
    toolbar->addWidget(state_label_);

    // ── panels ──
    regs_panel_ = new panels::RegistersPanel;
    serial_panel_ = new panels::SerialPanel;
    gpio_panel_ = new panels::GpioPanel;

    // Central: serial output (the surface the user watches while firmware runs).
    setCentralWidget(serial_panel_);

    // Left dock: CPU registers.
    auto* regs_dock = new QDockWidget("CPU registers", this);
    regs_dock->setObjectName("regs_dock"); // for saveState/restoreState later
    regs_dock->setWidget(regs_panel_);
    addDockWidget(Qt::LeftDockWidgetArea, regs_dock);

    // Bottom dock: GPIO.
    auto* gpio_dock = new QDockWidget("GPIO", this);
    gpio_dock->setObjectName("gpio_dock");
    gpio_dock->setWidget(gpio_panel_);
    addDockWidget(Qt::BottomDockWidgetArea, gpio_dock);

    connect(run_btn_, &QPushButton::clicked, this, &MainWindow::onRunClicked);
    connect(step_btn, &QPushButton::clicked, this, &MainWindow::onStepClicked);
    connect(reset_btn, &QPushButton::clicked,
            this, &MainWindow::onResetClicked);

    timer_ = new QTimer(this);
    timer_->setInterval(50); // ~20 UI ticks/sec
    connect(timer_, &QTimer::timeout, this, &MainWindow::onTick);

    rebuildSession();
    refreshFromSnapshot();

    // Test/CI hook: auto-run under offscreen so a headless launch exercises
    // the QTimer→run→refresh loop without a human clicking Run. No effect in
    // the normal interactive launch (env var unset).
    if (qEnvironmentVariableIsSet("MICRO_FORGE_GUI_AUTORUN")) {
        running_ = true;
        run_btn_->setText("Pause");
        timer_->start();
    }

    resize(960, 720);
}

void MainWindow::rebuildSession() {
    auto r = session_.rebuild();
    if (!r) {
        state_label_->setText(QString::fromStdString(r.error()));
    }
}

void MainWindow::onRunClicked() {
    running_ = !running_;
    if (running_) {
        run_btn_->setText("Pause");
        timer_->start();
    } else {
        run_btn_->setText("Run");
        timer_->stop();
    }
    refreshFromSnapshot();
}

void MainWindow::onStepClicked() {
    session_.step();
    refreshFromSnapshot();
}

void MainWindow::onResetClicked() {
    running_ = false;
    run_btn_->setText("Run");
    timer_->stop();
    rebuildSession();
    refreshFromSnapshot();
}

void MainWindow::onTick() {
    if (running_ && session_.valid()) {
        session_.run(20000);
        refreshFromSnapshot();
    }
}

void MainWindow::refreshFromSnapshot() {
    if (!session_.valid()) {
        return;
    }
    const auto snap = session_.snapshot();

    const char* st = "?";
    switch (snap.cpu.state) {
        case cpu::CPU::State::Running:
            st = "Running";
            break;
        case cpu::CPU::State::Halted:
            st = "Halted";
            break;
        case cpu::CPU::State::Faulted:
            st = "Faulted";
            break;
    }
    QString label = QString("%1 | %2 | cycles=%3")
                        .arg(QString::fromLatin1(st))
                        .arg(snap.cpu.handler_mode ? "handler" : "thread")
                        .arg(snap.cycles);
    if (snap.fault.present) {
        label += " | FAULT";
    }
    state_label_->setText(label);

    regs_panel_->refresh(snap);
    serial_panel_->refresh(snap);
    gpio_panel_->refresh(snap);
}

} // namespace micro_forge::gui
