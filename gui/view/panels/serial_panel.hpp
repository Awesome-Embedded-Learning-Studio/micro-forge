// Serial output panel — read-only view of the USART bytes accumulated by the
// session. This is the "wow" surface: firmware output appears here as it runs.
#pragma once

#include "introspection/introspection.hpp"

#include <QWidget>

class QTextEdit;

namespace micro_forge::gui::panels {

class SerialPanel : public QWidget {
    Q_OBJECT

  public:
    explicit SerialPanel(QWidget* parent = nullptr);

    void refresh(const introspection::IntrospectionSnapshot& snap);

  private:
    QTextEdit* view_;
};

} // namespace micro_forge::gui::panels
