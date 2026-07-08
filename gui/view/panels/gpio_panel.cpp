// GPIO panel — see gpio_panel.hpp.
#include "gui/view/panels/gpio_panel.hpp"

#include <QChar>
#include <QHBoxLayout>
#include <QLabel>
#include <QLatin1Char>
#include <QPushButton>
#include <QString>
#include <QVBoxLayout>

#include <cstdint>

namespace micro_forge::gui::panels {

GpioPanel::GpioPanel(QWidget* parent) : QWidget(parent) {
    auto* lay = new QVBoxLayout(this);

    label_ = new QLabel;
    label_->setStyleSheet("font-family: monospace;");
    lay->addWidget(label_);

    // Input-injection buttons (A2): each click toggles the cached pin level.
    auto* row = new QHBoxLayout;
    auto* pa0_btn = new QPushButton("PA0 toggle");
    auto* pc13_btn = new QPushButton("PC13 toggle");
    row->addWidget(pa0_btn);
    row->addWidget(pc13_btn);
    row->addStretch();
    lay->addLayout(row);

    connect(pa0_btn, &QPushButton::clicked, this, [this] {
        pa0_high_ = !pa0_high_;
        emit gpioInputRequested('A', 0, pa0_high_);
    });
    connect(pc13_btn, &QPushButton::clicked, this, [this] {
        pc13_high_ = !pc13_high_;
        emit gpioInputRequested('C', 13, pc13_high_);
    });
}

void GpioPanel::refresh(const introspection::IntrospectionSnapshot& snap) {
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
    label_->setText(g);
}

} // namespace micro_forge::gui::panels
