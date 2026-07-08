// Clock panel — see clock_panel.hpp.
#include "gui/view/panels/clock_panel.hpp"

#include <QLabel>
#include <QLatin1Char>
#include <QString>
#include <QVBoxLayout>

#include <cstdint>

namespace micro_forge::gui::panels {

ClockPanel::ClockPanel(QWidget* parent) : QWidget(parent) {
    auto* lay = new QVBoxLayout(this);
    label_ = new QLabel;
    label_->setStyleSheet("font-family: monospace;");
    lay->addWidget(label_);
}

void ClockPanel::refresh(const introspection::IntrospectionSnapshot& snap) {
    const auto& c = snap.peripherals.clock;
    // Pretty-print MHz with 2 decimals (e.g. 8.00 MHz, 72.00 MHz, 36.00 MHz).
    const auto mhz = [](std::uint32_t hz) {
        const std::uint32_t whole = hz / 1'000'000u;
        const std::uint32_t frac = (hz % 1'000'000u) / 10'000u; // 2 decimals
        return QString("%1.%2 MHz")
            .arg(whole)
            .arg(frac, 2, 10, QLatin1Char('0'));
    };
    // ASCII tree (monospace-safe, no encoding surprises).
    label_->setText(
        QString("SYSCLK   %1\n"
                " +-- HCLK (AHB/CPU)  %2\n"
                "      +-- APB1  %3\n"
                "      `-- APB2  %4")
            .arg(mhz(c.sysclk))
            .arg(mhz(c.hclk))
            .arg(mhz(c.apb1))
            .arg(mhz(c.apb2)));
}

} // namespace micro_forge::gui::panels
