// Memory panel — see memory_panel.hpp.
#include "gui/view/panels/memory_panel.hpp"

#include <QLineEdit>
#include <QString>
#include <QTextEdit>
#include <QVBoxLayout>

#include <cstdint>

namespace micro_forge::gui::panels {

MemoryPanel::MemoryPanel(QWidget* parent) : QWidget(parent) {
    auto* lay = new QVBoxLayout(this);

    addr_input_ = new QLineEdit;
    addr_input_->setPlaceholderText(
        "Address (hex, e.g. 0x20000000) — Enter to track");
    lay->addWidget(addr_input_);

    dump_view_ = new QTextEdit;
    dump_view_->setReadOnly(true);
    dump_view_->setStyleSheet("font-family: monospace;");
    lay->addWidget(dump_view_);

    connect(addr_input_, &QLineEdit::returnPressed, this, [this] {
        bool ok = false;
        const unsigned parsed =
            addr_input_->text().trimmed().toUInt(&ok, 16); // accepts 0x prefix
        if (!ok) {
            return;
        }
        current_addr_ = static_cast<std::uint32_t>(parsed);
        has_addr_ = true;
        emit addr_changed(current_addr_);
    });
}

void MemoryPanel::show_dump(const QString& text) {
    dump_view_->setPlainText(text);
}

} // namespace micro_forge::gui::panels
