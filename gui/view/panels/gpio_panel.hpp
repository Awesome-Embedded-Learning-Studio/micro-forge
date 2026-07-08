// GPIO panel — A/B/C port ODR (hex) + 16 per-pin LED glyphs (pin0..pin15).
#pragma once

#include "introspection/introspection.hpp"

#include <QWidget>

class QLabel;

namespace micro_forge::gui::panels {

class GpioPanel : public QWidget {
    Q_OBJECT

  public:
    explicit GpioPanel(QWidget* parent = nullptr);

    void refresh(const introspection::IntrospectionSnapshot& snap);

  private:
    QLabel* label_;
};

} // namespace micro_forge::gui::panels
