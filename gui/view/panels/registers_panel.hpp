// CPU register panel — r0..r12 / sp / lr / pc, refreshed from the snapshot.
#pragma once

#include "introspection/introspection.hpp"

#include <QWidget>

class QTableWidget;

namespace micro_forge::gui::panels {

class RegistersPanel : public QWidget {
    Q_OBJECT

  public:
    explicit RegistersPanel(QWidget* parent = nullptr);

    void refresh(const introspection::IntrospectionSnapshot& snap);

  private:
    QTableWidget* table_;
};

} // namespace micro_forge::gui::panels
