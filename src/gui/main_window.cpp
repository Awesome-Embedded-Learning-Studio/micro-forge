// micro-forge GUI main window implementation (G5b).
#include "gui/main_window.hpp"

#include "cli/introspection.hpp"
#include "cpu/cpu.hpp"

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

#include <fstream>
#include <iterator>

namespace micro_forge::gui {

namespace {

std::vector<uint8_t> read_file(const QString& path) {
    std::ifstream f(path.toUtf8().constData(), std::ios::binary);
    return {std::istreambuf_iterator<char>(f), {}};
}

bool is_elf(const std::vector<uint8_t>& d) {
    return d.size() >= 4 && d[0] == 0x7f && d[1] == 'E' && d[2] == 'L' &&
           d[3] == 'F';
}

} // namespace

MainWindow::MainWindow(const QString& firmware_path, QWidget* parent)
    : QMainWindow(parent), firmware_path_(firmware_path) {
    setWindowTitle("micro-forge");

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

    setCentralWidget(central);
    resize(440, 620);

    connect(run_btn_, &QPushButton::clicked, this, &MainWindow::onRunClicked);
    connect(step_btn, &QPushButton::clicked, this, &MainWindow::onStepClicked);
    connect(reset_btn, &QPushButton::clicked,
            this, &MainWindow::onResetClicked);

    timer_ = new QTimer(this);
    timer_->setInterval(50); // ~20 UI ticks/sec
    connect(timer_, &QTimer::timeout, this, &MainWindow::onTick);

    rebuildSoc();
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

void MainWindow::rebuildSoc() {
    usart_output_.clear();
    auto created = chips::stm32f1::Stm32f103Soc::create();
    if (!created) {
        soc_.reset();
        state_label_->setText("SoC create failed");
        return;
    }
    soc_ = std::move(*created);
    soc_->parts().serial().set_output(
        [this](uint8_t ch) { usart_output_ += static_cast<char>(ch); });

    if (!firmware_path_.isEmpty()) {
        firmware_data_ = read_file(firmware_path_);
        if (firmware_data_.empty()) {
            state_label_->setText(QString("cannot read: ") + firmware_path_);
            return;
        }
        auto lr = is_elf(firmware_data_)
                      ? soc_->load_elf(firmware_data_)
                      : soc_->load_bin(0x08000000u, firmware_data_);
        if (!lr) {
            state_label_->setText(
                QString("load failed: ") + QString::fromStdString(lr.error()));
        }
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
    if (soc_) {
        soc_->run(1);
        refreshFromSnapshot();
    }
}

void MainWindow::onResetClicked() {
    running_ = false;
    run_btn_->setText("Run");
    timer_->stop();
    rebuildSoc();
    refreshFromSnapshot();
}

void MainWindow::onTick() {
    if (soc_ && running_) {
        soc_->run(20000);
        refreshFromSnapshot();
    }
}

void MainWindow::refreshFromSnapshot() {
    if (!soc_) {
        return;
    }
    const auto snap = cli::read_introspection(*soc_, usart_output_);

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
    auto hex = [](uint32_t v) {
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
}

} // namespace micro_forge::gui
