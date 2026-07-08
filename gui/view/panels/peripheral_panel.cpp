// Peripheral status panel — see peripheral_panel.hpp.
#include "gui/view/panels/peripheral_panel.hpp"

#include <QLabel>
#include <QLatin1Char>
#include <QString>
#include <QVBoxLayout>

#include <cstdint>

namespace micro_forge::gui::panels {

PeripheralPanel::PeripheralPanel(QWidget* parent) : QWidget(parent) {
    auto* lay = new QVBoxLayout(this);

    label_ = new QLabel;
    label_->setStyleSheet("font-family: monospace;");
    lay->addWidget(label_);
}

void PeripheralPanel::refresh(const introspection::IntrospectionSnapshot& snap) {
    auto hex = [](std::uint32_t v) {
        return QString("0x%1").arg(v, 8, 16, QLatin1Char('0'));
    };
    const auto& p = snap.peripherals;
    QString s;
    s += QString("SysTick  ctrl=%1 load=%2 val=%3\n")
             .arg(hex(p.systick.ctrl), hex(p.systick.load), hex(p.systick.val));
    s += QString("NVIC     pending=%1 irq=%2 enabled=%3\n")
             .arg(p.nvic.has_pending ? QStringLiteral("yes") : QStringLiteral("no"))
             .arg(static_cast<int>(p.nvic.highest_pending_irq))
             .arg(static_cast<int>(p.nvic.enabled_count));
    s += QString("SCB      icsr=%1 vtor=%2 aircr=%3 prigroup=%4\n")
             .arg(hex(p.scb.icsr), hex(p.scb.vtor), hex(p.scb.aircr))
             .arg(static_cast<int>(p.scb.prigroup));
    label_->setText(s);
}

} // namespace micro_forge::gui::panels
