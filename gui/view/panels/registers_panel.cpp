// CPU register panel — see registers_panel.hpp.
#include "gui/view/panels/registers_panel.hpp"

#include <QHeaderView>
#include <QLabel>
#include <QLatin1Char>
#include <QString>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

#include <cstdint>

namespace micro_forge::gui::panels {

RegistersPanel::RegistersPanel(QWidget* parent) : QWidget(parent) {
    auto* lay = new QVBoxLayout(this);

    table_ = new QTableWidget(16, 2);
    table_->setHorizontalHeaderLabels({"reg", "value"});
    table_->verticalHeader()->setVisible(false);
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    lay->addWidget(table_);
}

void RegistersPanel::refresh(const introspection::IntrospectionSnapshot& snap) {
    static constexpr const char* kNames[16] = {
        "r0", "r1", "r2", "r3", "r4", "r5", "r6", "r7",
        "r8", "r9", "r10", "r11", "r12", "sp", "lr", "pc"};
    auto hex = [](std::uint32_t v) {
        return QString("0x%1").arg(v, 8, 16, QLatin1Char('0'));
    };
    for (int i = 0; i < 13; ++i) {
        table_->setItem(i, 0, new QTableWidgetItem(kNames[i]));
        table_->setItem(i, 1, new QTableWidgetItem(hex(snap.cpu.regs[i])));
    }
    table_->setItem(13, 0, new QTableWidgetItem(kNames[13]));
    table_->setItem(13, 1, new QTableWidgetItem(hex(snap.cpu.sp)));
    table_->setItem(14, 0, new QTableWidgetItem(kNames[14]));
    table_->setItem(14, 1, new QTableWidgetItem(hex(snap.cpu.lr)));
    table_->setItem(15, 0, new QTableWidgetItem(kNames[15]));
    table_->setItem(15, 1, new QTableWidgetItem(hex(snap.cpu.pc)));
}

} // namespace micro_forge::gui::panels
