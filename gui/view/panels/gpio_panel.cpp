// GPIO panel — see gpio_panel.hpp.
#include "gui/view/panels/gpio_panel.hpp"

#include <QChar>
#include <QLabel>
#include <QLatin1Char>
#include <QString>
#include <QVBoxLayout>

#include <cstdint>

namespace micro_forge::gui::panels {

GpioPanel::GpioPanel(QWidget* parent) : QWidget(parent) {
    auto* lay = new QVBoxLayout(this);
    auto* title = new QLabel("GPIO output (A/B/C, pin0..pin15)");
    title->setStyleSheet("font-weight: bold;");
    lay->addWidget(title);

    label_ = new QLabel;
    label_->setStyleSheet("font-family: monospace;");
    lay->addWidget(label_);
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
