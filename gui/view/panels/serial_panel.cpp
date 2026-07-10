// Serial panel — see serial_panel.hpp.
#include "gui/view/panels/serial_panel.hpp"

#include "QuarkWidgets/UartTerminalView.hpp"

#include <QLineEdit>
#include <QString>
#include <QVBoxLayout>

namespace micro_forge::gui::panels {

SerialPanel::SerialPanel(QWidget* parent) : QWidget(parent) {
    auto* lay = new QVBoxLayout(this);

    view_ = new quark::UartTerminalView;
    lay->addWidget(view_);

    input_ = new QLineEdit;
    input_->setPlaceholderText("Send to USART RX — type then Enter");
    lay->addWidget(input_);

    connect(input_, &QLineEdit::returnPressed, this, [this] {
        emit inputSubmitted(input_->text());
        input_->clear();
    });
}

void SerialPanel::refresh(const introspection::IntrospectionSnapshot& snap) {
    const auto sv = snap.peripherals.usart_output;
    // Buffer shrank → session rebuilt: old output is stale, resync from zero.
    if (sv.size() < shownBytes_) {
        view_->clear();
        shownBytes_ = 0;
    }
    if (sv.size() > shownBytes_) {
        const auto len = static_cast<std::size_t>(sv.size()) - shownBytes_;
        view_->appendText(QString::fromUtf8(sv.data() + shownBytes_,
                                            static_cast<int>(len)));
        shownBytes_ = sv.size();
    }
}

} // namespace micro_forge::gui::panels
