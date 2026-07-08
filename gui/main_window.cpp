// micro-forge GUI main window implementation.
//
// QMainWindow shell assembling dockable panels around a central serial output.
// Drives the session via QTimer and refreshes every panel from the snapshot
// each tick. A speed selector throttles how many steps per tick — firmware
// that uses big software delays (e.g. blink) needs a higher gear to look live.
// Owns no simulator state — that's in model::Session.
#include "gui/main_window.hpp"

#include "gui/view/panels/fault_panel.hpp"
#include "gui/view/panels/gpio_panel.hpp"
#include "gui/view/panels/peripheral_panel.hpp"
#include "gui/view/panels/registers_panel.hpp"
#include "gui/view/panels/serial_panel.hpp"
#include "gui/view/panels/status_panel.hpp"
#include "gui/view/board_view/board_view.hpp"

#include "cpu/cpu.hpp"

#include <QCloseEvent>
#include <QComboBox>
#include <QDockWidget>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QString>
#include <QToolBar>
#include <QTimer>

#include <cstddef>
#include <cstdint>

namespace micro_forge::gui {

MainWindow::MainWindow(const QString& firmware_path, QWidget* parent)
    : QMainWindow(parent) {
    setWindowTitle("micro-forge");

    session_.set_firmware(firmware_path.toStdString());

    // ── toolbar: run / step / reset + state + speed ──
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
    toolbar->addSeparator();
    toolbar->addWidget(new QLabel(tr("Speed:")));
    speed_combo_ = new QComboBox;
    // Steps per tick (~20 ticks/sec). Higher gears make software-delay
    // firmware (blink loops) visibly animate instead of crawling.
    speed_combo_->addItem(tr("1×"), 20000);
    speed_combo_->addItem(tr("5×"), 100000);
    speed_combo_->addItem(tr("25×"), 500000);
    speed_combo_->addItem(tr("100×"), 2000000);
    speed_combo_->setCurrentIndex(0);
    toolbar->addWidget(speed_combo_);

    // ── panels ──
    regs_panel_ = new panels::RegistersPanel;
    status_panel_ = new panels::StatusPanel;
    serial_panel_ = new panels::SerialPanel;
    fault_panel_ = new panels::FaultPanel;
    gpio_panel_ = new panels::GpioPanel;
    periph_panel_ = new panels::PeripheralPanel;
    board_view_ = new view::BoardView;

    // Central: the board (chip + LEDs) — micro-forge's visual main stage.
    // Serial output moves to the bottom dock so firmware output stays visible
    // alongside the GPIO dock while the board owns the centre.
    setCentralWidget(board_view_);

    // Left dock: CPU registers.
    auto* regs_dock = new QDockWidget("CPU registers", this);
    regs_dock->setObjectName("regs_dock");
    regs_dock->setWidget(regs_panel_);
    addDockWidget(Qt::LeftDockWidgetArea, regs_dock);

    // Right dock: status / fault / peripherals (stacked vertically).
    auto* status_dock = new QDockWidget("Status / masks", this);
    status_dock->setObjectName("status_dock");
    status_dock->setWidget(status_panel_);
    addDockWidget(Qt::RightDockWidgetArea, status_dock);

    auto* fault_dock = new QDockWidget("Fault", this);
    fault_dock->setObjectName("fault_dock");
    fault_dock->setWidget(fault_panel_);
    addDockWidget(Qt::RightDockWidgetArea, fault_dock);

    auto* periph_dock = new QDockWidget("Peripherals", this);
    periph_dock->setObjectName("periph_dock");
    periph_dock->setWidget(periph_panel_);
    addDockWidget(Qt::RightDockWidgetArea, periph_dock);

    // Bottom dock: GPIO + serial output (side by side).
    auto* gpio_dock = new QDockWidget("GPIO", this);
    gpio_dock->setObjectName("gpio_dock");
    gpio_dock->setWidget(gpio_panel_);
    addDockWidget(Qt::BottomDockWidgetArea, gpio_dock);

    auto* serial_dock = new QDockWidget("Serial", this);
    serial_dock->setObjectName("serial_dock");
    serial_dock->setWidget(serial_panel_);
    addDockWidget(Qt::BottomDockWidgetArea, serial_dock);

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

    resize(1100, 760);

    // Restore the user's dock layout + window geometry from last session (A3).
    // First launch: saved state is absent → restoreState returns false and the
    // defaults above stay in effect. Docks must already exist + be named
    // (setObjectName in the ctor above) for restoreState to relocate them.
    QSettings settings("micro-forge", "gui");
    restoreGeometry(settings.value("geometry").toByteArray());
    restoreState(settings.value("windowState").toByteArray());
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
        const auto steps =
            static_cast<std::size_t>(speed_combo_->currentData().toInt());
        session_.run(steps);
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
    status_panel_->refresh(snap);
    serial_panel_->refresh(snap);
    fault_panel_->refresh(snap);
    gpio_panel_->refresh(snap);
    periph_panel_->refresh(snap);
    board_view_->refresh(snap);
}

void MainWindow::closeEvent(QCloseEvent* event) {
    // Persist dock layout + window geometry so the next launch opens the way
    // the user left it (A3). Docks were all given objectNames in the ctor.
    QSettings settings("micro-forge", "gui");
    settings.setValue("geometry", saveGeometry());
    settings.setValue("windowState", saveState());
    QMainWindow::closeEvent(event);
}

} // namespace micro_forge::gui
