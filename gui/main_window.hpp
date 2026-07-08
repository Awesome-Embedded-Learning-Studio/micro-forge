// micro-forge GUI main window.
//
// QMainWindow shell: a toolbar (run/step/reset + state + speed) plus dockable
// panels. Serial output is central; registers dock left; status / fault /
// peripherals dock right; GPIO dock bottom. Users can drag/fold/float any
// dock. All panels refresh from the session's snapshot each tick — MainWindow
// owns no simulator state (that's in model::Session).
#pragma once

#include "gui/model/session.hpp"

#include <QMainWindow>
#include <QString>

class QCloseEvent;
class QComboBox;
class QLabel;
class QPushButton;
class QTimer;

namespace micro_forge::gui::panels {
class RegistersPanel;
class GpioPanel;
class SerialPanel;
class StatusPanel;
class FaultPanel;
class MemoryPanel;
class PeripheralPanel;
class ClockPanel;
} // namespace micro_forge::gui::panels

namespace micro_forge::gui::view {
class BoardView;
} // namespace micro_forge::gui::view

namespace micro_forge::gui {

class MainWindow : public QMainWindow {
    Q_OBJECT

  public:
    explicit MainWindow(const QString& firmware_path,
                        QWidget* parent = nullptr);

  protected:
    void closeEvent(QCloseEvent* event) override;

  private slots:
    void onTick();
    void onRunClicked();
    void onStepClicked();
    void onResetClicked();

  private:
    void rebuildSession();
    void refreshFromSnapshot();
    void refreshMemory(); // C4-mem: re-dump the memory panel's tracked region.

    model::Session session_;
    bool running_ = false;

    QTimer* timer_ = nullptr;
    QLabel* state_label_ = nullptr;
    QPushButton* run_btn_ = nullptr;
    QComboBox* speed_combo_ = nullptr;
    panels::RegistersPanel* regs_panel_ = nullptr;
    panels::SerialPanel* serial_panel_ = nullptr;
    panels::GpioPanel* gpio_panel_ = nullptr;
    panels::StatusPanel* status_panel_ = nullptr;
    panels::FaultPanel* fault_panel_ = nullptr;
    panels::PeripheralPanel* periph_panel_ = nullptr;
    panels::ClockPanel* clock_panel_ = nullptr;
    panels::MemoryPanel* memory_panel_ = nullptr;
    view::BoardView* board_view_ = nullptr;
};

} // namespace micro_forge::gui
