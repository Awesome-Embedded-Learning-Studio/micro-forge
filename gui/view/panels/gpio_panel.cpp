// GPIO panel — see gpio_panel.hpp.
#include "gui/view/panels/gpio_panel.hpp"

#include <QChar>
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

    // PA0 input button (active-low): hold = press (drive IDR low), release =
    // idle (drive IDR high). Forwarded to session.simulate_gpio_input. Useful
    // for firmware that polls IDR (TAMCPP 2_button_control's ReadPin).
    pa0_btn_ = new QPushButton("PA0 button — hold = press");
    pa0_btn_->setCheckable(true);
    lay->addWidget(pa0_btn_);
    connect(pa0_btn_, &QPushButton::toggled, this,
            [this](bool checked) { emit injectGpio('A', 0, !checked); });
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
