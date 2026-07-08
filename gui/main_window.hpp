// micro-forge GUI main window.
//
// Thin Qt view over a model::Session. Owns only UI: a control bar plus a set
// of panels (registers / serial / gpio / ...) refreshed from the session's
// IntrospectionSnapshot each tick. Each panel is an independent widget under
// view/panels/. The sim never runs on a QThread (DIRECTIVES §E).
#pragma once

#include "gui/model/session.hpp"

#include <QMainWindow>
#include <QString>

class QLabel;
class QPushButton;
class QTimer;

namespace micro_forge::gui::panels {
class RegistersPanel;
class GpioPanel;
class SerialPanel;
} // namespace micro_forge::gui::panels

namespace micro_forge::gui {

class MainWindow : public QMainWindow {
    Q_OBJECT

  public:
    explicit MainWindow(const QString& firmware_path,
                        QWidget* parent = nullptr);

  private slots:
    void onTick();
    void onRunClicked();
    void onStepClicked();
    void onResetClicked();

  private:
    void rebuildSession();
    void refreshFromSnapshot();

    model::Session session_;
    bool running_ = false;

    QTimer* timer_ = nullptr;
    QLabel* state_label_ = nullptr;
    QPushButton* run_btn_ = nullptr;
    panels::RegistersPanel* regs_panel_ = nullptr;
    panels::SerialPanel* serial_panel_ = nullptr;
    panels::GpioPanel* gpio_panel_ = nullptr;
};

} // namespace micro_forge::gui
