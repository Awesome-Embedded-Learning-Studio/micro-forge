// Serial output panel — see serial_panel.hpp.
#include "gui/view/panels/serial_panel.hpp"

#include <QLabel>
#include <QString>
#include <QTextEdit>
#include <QVBoxLayout>

namespace micro_forge::gui::panels {

SerialPanel::SerialPanel(QWidget* parent) : QWidget(parent) {
    auto* lay = new QVBoxLayout(this);

    view_ = new QTextEdit;
    view_->setReadOnly(true);
    view_->setStyleSheet("font-family: monospace;");
    lay->addWidget(view_);
}

void SerialPanel::refresh(const introspection::IntrospectionSnapshot& snap) {
    const auto sv = snap.peripherals.usart_output;
    view_->setPlainText(
        QString::fromUtf8(sv.data(), static_cast<int>(sv.size())));
}

} // namespace micro_forge::gui::panels
