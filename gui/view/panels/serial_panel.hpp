// Serial panel — read-only view of USART bytes the session has accumulated,
// plus a text input to inject bytes into USART RX (A2). The input only emits
// a signal; MainWindow forwards it to model::Session so the panel itself stays
// free of simulator coupling.
#pragma once

#include "introspection/introspection.hpp"

#include <QString>
#include <QWidget>

class QLineEdit;
class QTextEdit;

namespace micro_forge::gui::panels {

class SerialPanel : public QWidget {
    Q_OBJECT
  public:
    explicit SerialPanel(QWidget* parent = nullptr);

    void refresh(const introspection::IntrospectionSnapshot& snap);

  Q_SIGNALS:
    // Emitted on Enter in the input field — the raw text the user typed.
    void inputSubmitted(const QString& text);

  private:
    QTextEdit* view_;
    QLineEdit* input_;
};

} // namespace micro_forge::gui::panels
