// micro-forge GUI main window implementation.
//
// Thin Qt view over a model::Session. Owns only UI widgets; simulator state
// (SoC, firmware, USART buffer) lives in session_. A QTimer drives run() +
// refreshFromSnapshot() each tick — single-threaded on the Qt main thread.
#include "gui/main_window.hpp"

#include "cpu/cpu.hpp"

#include <QChar>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLatin1Char>
#include <QPushButton>
#include <QString>
#include <QTableWidget>
#include <QTableWidgetItem>
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

    // ── CPU registers (r0-r12, sp, lr, pc) ──
    regs_table_ = new QTableWidget(16, 2);
    regs_table_->setHorizontalHeaderLabels({"reg", "value"});
    regs_table_->verticalHeader()->setVisible(false);
    regs_table_->horizontalHeader()->setStretchLastSection(true);
    regs_table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    root->addWidget(regs_table_);

    // ── GPIO panel: A/B/C port ODR with per-pin LED glyphs ──
    auto* gpio_title = new QLabel("GPIO output (A/B/C, pin0..pin15)");
    gpio_title->setStyleSheet("font-weight: bold;");
    root->addWidget(gpio_title);
    gpio_label_ = new QLabel;
    gpio_label_->setStyleSheet("font-family: monospace;");
    root->addWidget(gpio_label_);

    setCentralWidget(central);
    resize(480, 720);

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

    static constexpr const char* kNames[16] = {
        "r0", "r1", "r2", "r3", "r4", "r5", "r6", "r7",
        "r8", "r9", "r10", "r11", "r12", "sp", "lr", "pc"};
    auto hex = [](std::uint32_t v) {
        return QString("0x%1").arg(v, 8, 16, QLatin1Char('0'));
    };
    for (int i = 0; i < 13; ++i) {
        regs_table_->setItem(i, 0, new QTableWidgetItem(kNames[i]));
        regs_table_->setItem(i, 1, new QTableWidgetItem(hex(snap.cpu.regs[i])));
    }
    regs_table_->setItem(13, 0, new QTableWidgetItem(kNames[13]));
    regs_table_->setItem(13, 1, new QTableWidgetItem(hex(snap.cpu.sp)));
    regs_table_->setItem(14, 0, new QTableWidgetItem(kNames[14]));
    regs_table_->setItem(14, 1, new QTableWidgetItem(hex(snap.cpu.lr)));
    regs_table_->setItem(15, 0, new QTableWidgetItem(kNames[15]));
    regs_table_->setItem(15, 1, new QTableWidgetItem(hex(snap.cpu.pc)));

    // GPIO: 3 ports (A/B/C), each ODR hex + 16 LED glyphs (pin0..pin15).
    auto led = [](std::uint16_t odr) {
        QString s;
        for (int i = 0; i < 16; ++i) {
            s += (odr >> i) & 1 ? QChar(0x25CF) : QChar(0x00B7); // ● or ·
        }
        return s;
    };
    QString g;
    for (int i = 0; i < 3; ++i) {
        const auto& port = snap.peripherals.gpio[i];
        g += QString("%1 0x%2  %3\n")
                 .arg(QChar(port.port))
                 .arg(port.odr, 4, 16, QLatin1Char('0'))
                 .arg(led(port.odr));
    }
    gpio_label_->setText(g);
}

} // namespace micro_forge::gui
