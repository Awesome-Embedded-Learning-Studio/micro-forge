// Serial panel — read-only view of USART bytes the session has accumulated,
// plus a text input to inject bytes into USART RX (A2). The input only emits
// a signal; MainWindow forwards it to model::Session so the panel itself stays
// free of simulator coupling.
//
// The terminal body is a quark::UartTerminalView (org-level control). The
// snapshot carries the host's whole accumulated USART buffer each tick, so this
// adapter tracks how many bytes it has already appended and feeds only the new
// tail — keeping the terminal append-only (its RX counter + autoscroll stay
// meaningful) instead of replacing the whole text every tick.
#pragma once

#include "introspection/introspection.hpp"

#include <cstddef>

#include <QString>
#include <QWidget>

class QLineEdit;

namespace quark { class UartTerminalView; }

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
    quark::UartTerminalView* view_;
    QLineEdit* input_;
    // Bytes of the snapshot's USART buffer already shown in the terminal. Used
    // to append only the delta each tick. Reset when the buffer shrinks (the
    // session rebuilt → old output is stale → terminal cleared).
    std::size_t shownBytes_ = 0;
};

} // namespace micro_forge::gui::panels
