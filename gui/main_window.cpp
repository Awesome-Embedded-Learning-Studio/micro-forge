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
#include "gui/view/widgets/stm32_board_widget.hpp"
#include "gui/view/panels/clock_panel.hpp"
#include "gui/view/panels/memory_panel.hpp"

#include "cpu/cpu.hpp"

#include <QByteArray>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDockWidget>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QKeySequence>
#include <QLabel>
#include <QMenuBar>
#include <QMimeData>
#include <QPushButton>
#include <QSettings>
#include <QString>
#include <QToolBar>
#include <QTimer>
#include <QUrl>

#include <cstddef>
#include <cstdint>

namespace micro_forge::gui {

MainWindow::MainWindow(const QString& firmware_path, QWidget* parent)
    : QMainWindow(parent) {
    setWindowTitle("micro-forge");
    setAcceptDrops(true); // drag-and-drop an .elf/.bin onto the window to load it

    // File menu: Open firmware via dialog — alternative to drag-and-drop /
    // command-line. Reuses loadFirmware (stop, set path, rebuild, refresh).
    auto* file_menu = menuBar()->addMenu(tr("&File"));
    auto* open_action =
        file_menu->addAction(tr("&Open firmware..."), this, [this]() {
            const QString path = QFileDialog::getOpenFileName(
                this, tr("Open firmware"), QString(),
                tr("Firmware (*.elf *.bin);;All files (*)"));
            if (!path.isEmpty()) {
                loadFirmware(path);
            }
        });
    open_action->setShortcut(QKeySequence::Open); // Ctrl+O

    session_.set_firmware(firmware_path.toStdString());

    // ── toolbar: run / step / reset + state + speed ──
    auto* toolbar = addToolBar("main");
    toolbar->setObjectName("main_toolbar"); // saveState/restoreState need it
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
    speed_combo_->setCurrentIndex(3); // default 100× — 1× is too slow for busy-wait
    toolbar->addWidget(speed_combo_);
    auto* ff_check = new QCheckBox(tr("Fast-forward WFI"));
    ff_check->setToolTip(tr("Skip WFI sleep straight to the next IRQ (P2.a).\n"
                            "Only affects firmware that uses WFI; busy-wait "
                            "(HAL_Delay) is unaffected."));
    toolbar->addWidget(ff_check);
    connect(ff_check, &QCheckBox::toggled, this, [this](bool on) {
        session_.set_fast_forward_enabled(on);
    });
    auto* jit_check = new QCheckBox(tr("JIT cache"));
    jit_check->setToolTip(tr("Cache decoded instructions per PC (skip\n"
                             "fetch+decode on repeat). +30-35% faster.\n"
                             "Works for ALL firmware (16+32-bit)."));
    toolbar->addWidget(jit_check);
    connect(jit_check, &QCheckBox::toggled, this, [this](bool on) {
        session_.set_jit_enabled(on);
    });

    // ── panels ──
    regs_panel_ = new panels::RegistersPanel;
    status_panel_ = new panels::StatusPanel;
    serial_panel_ = new panels::SerialPanel;
    fault_panel_ = new panels::FaultPanel;
    gpio_panel_ = new panels::GpioPanel;
    periph_panel_ = new panels::PeripheralPanel;
    clock_panel_ = new panels::ClockPanel;
    memory_panel_ = new panels::MemoryPanel;
    board_widget_ = new view::Stm32BoardWidget;

    // Central: the board (chip + LEDs) — micro-forge's visual main stage.
    // Serial output moves to the bottom dock so firmware output stays visible
    // alongside the GPIO dock while the board owns the centre.
    setCentralWidget(board_widget_);

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

    auto* clock_dock = new QDockWidget("Clock tree", this);
    clock_dock->setObjectName("clock_dock");
    clock_dock->setWidget(clock_panel_);
    addDockWidget(Qt::RightDockWidgetArea, clock_dock);

    auto* memory_dock = new QDockWidget("Memory", this);
    memory_dock->setObjectName("memory_dock");
    memory_dock->setWidget(memory_panel_);
    addDockWidget(Qt::RightDockWidgetArea, memory_dock);

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

    // USART RX: forward the serial panel's input box to the session. Inject one
    // byte at a time and run enough steps for the RXNE IRQ to consume it (the
    // USART model has a single-slot DR — back-to-back injects with no run in
    // between overwrite DR and lose bytes). Append CRLF so the firmware's line
    // parser (main.cpp: waits for '\r'/'\n') fires handle_command.
    connect(serial_panel_, &panels::SerialPanel::inputSubmitted,
            this, [this](const QString& text) {
                const auto bytes = text.toUtf8();
                for (const char b : bytes) {
                    session_.inject_rx(static_cast<std::uint8_t>(b));
                    session_.run(50'000);
                }
                session_.inject_rx('\r');
                session_.run(50'000);
                session_.inject_rx('\n');
                session_.run(50'000);
            });
    // GPIO input injection (PA0 button in gpio_panel): firmware that polls IDR
    // — e.g. TAMCPP 2_button_control's HAL_GPIO_ReadPin — sees the level.
    connect(gpio_panel_, &panels::GpioPanel::injectGpio,
            this, [this](char port, std::uint8_t pin, bool high) {
                session_.simulate_gpio_input(port, pin, high);
            });
    // C4-mem: a new address → dump it immediately (don't wait for the tick).
    connect(memory_panel_, &panels::MemoryPanel::addr_changed,
            this, [this](std::uint32_t) { refreshMemory(); });

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
    clock_panel_->refresh(snap);
    board_widget_->refresh(snap);
    refreshMemory();
}

void MainWindow::refreshMemory() {
    if (!session_.valid() || !memory_panel_->has_addr()) {
        return;
    }
    // Dump 64 bytes from the tracked address; cheap enough to run per tick.
    const auto dump =
        session_.read_memory(memory_panel_->current_addr(), 64);
    memory_panel_->show_dump(QString::fromStdString(dump));
}

void MainWindow::closeEvent(QCloseEvent* event) {
    // Persist dock layout + window geometry so the next launch opens the way
    // the user left it (A3). Docks were all given objectNames in the ctor.
    QSettings settings("micro-forge", "gui");
    settings.setValue("geometry", saveGeometry());
    settings.setValue("windowState", saveState());
    QMainWindow::closeEvent(event);
}

void MainWindow::dragEnterEvent(QDragEnterEvent* event) {
    if (event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
    }
}

void MainWindow::dropEvent(QDropEvent* event) {
    const auto urls = event->mimeData()->urls();
    if (urls.isEmpty()) {
        return;
    }
    const QString path = urls.first().toLocalFile();
    if (path.isEmpty()) {
        return;
    }
    loadFirmware(path);
    event->acceptProposedAction();
}

void MainWindow::loadFirmware(const QString& path) {
    running_ = false;
    run_btn_->setText("Run");
    timer_->stop();
    session_.set_firmware(path.toStdString());
    rebuildSession();
    refreshFromSnapshot();
}

} // namespace micro_forge::gui
