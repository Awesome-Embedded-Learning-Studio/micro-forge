// Status / masks panel — see status_panel.hpp.
#include "gui/view/panels/status_panel.hpp"

#include <QHeaderView>
#include <QLabel>
#include <QLatin1Char>
#include <QString>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

#include <cstdint>

namespace micro_forge::gui::panels {

StatusPanel::StatusPanel(QWidget* parent) : QWidget(parent) {
    auto* lay = new QVBoxLayout(this);

    table_ = new QTableWidget(7, 2);
    table_->setHorizontalHeaderLabels({"field", "value"});
    table_->verticalHeader()->setVisible(false);
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    lay->addWidget(table_);
}

void StatusPanel::refresh(const introspection::IntrospectionSnapshot& snap) {
    auto hex = [](std::uint32_t v) {
        return QString("0x%1").arg(v, 8, 16, QLatin1Char('0'));
    };
    const auto& c = snap.cpu;
    struct Row {
        const char* name;
        std::uint32_t v;
    };
    const Row rows[7] = {
        {"xpsr", c.xpsr},         {"PRIMASK", c.primask},
        {"BASEPRI", c.basepri},   {"FAULTMASK", c.faultmask},
        {"CONTROL", c.control},   {"MSP", c.msp},
        {"PSP", c.psp},
    };
    for (int i = 0; i < 7; ++i) {
        table_->setItem(i, 0,
                        new QTableWidgetItem(QString::fromLatin1(rows[i].name)));
        table_->setItem(i, 1, new QTableWidgetItem(hex(rows[i].v)));
    }
}

} // namespace micro_forge::gui::panels
