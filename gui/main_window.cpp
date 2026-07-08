// micro-forge GUI main window implementation.
//
// Thin Qt view: assembles the control bar + panels, drives the session via
// QTimer, and refreshes every panel from the snapshot each tick. Owns no
// simulator state — that's in model::Session.
#include "gui/main_window.hpp"

#include "gui/view/panels/gpio_panel.hpp"
#include "gui/view/panels/registers_panel.hpp"
#include "gui/view/panels/serial_panel.hpp"

#include "cpu/cpu.hpp"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QString>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include <cstdint>

namespace micro_forge::gui {

MainWindow::MainWindow(const QString& firmware_path, QWidget* parent)
    : QMainWindow(parent) {
    setWindowTitle("micro-forge");

    session_.set_firmware(firmware_path.toStdString());

    auto* central = new QWidget;
    auto* root = new QVBoxLayout(central);

    // ── control bar ──
    auto* bar = new QHBoxLayout;
    run_btn_ = new QPushButton("Run");
    auto* step_btn = new QPushButton("Step");
    auto* reset_btn = new QPushButton("Reset");
    bar->addWidget(run_btn_);
    bar->addWidget(step_btn);
    bar->addWidget(reset_btn);
    bar->addStretch();
    state_label_ = new QLabel("idle");
    state_label_->setStyleSheet("font-family: monospace;");
    bar->addWidget(state_label_);
    root->addLayout(bar);

    // ── panels (fixed vertical layout for now; dock organization comes next) ──
    regs_panel_ = new panels::RegistersPanel;
    root->addWidget(regs_panel_);
    serial_panel_ = new panels::SerialPanel;
    root->addWidget(serial_panel_);
    gpio_panel_ = new panels::GpioPanel;
    root->addWidget(gpio_panel_);

    setCentralWidget(central);
    resize(480, 780);

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
