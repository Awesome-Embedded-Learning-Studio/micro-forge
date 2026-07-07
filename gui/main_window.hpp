// micro-forge GUI main window (G5b).
//
// Owns the simulator (a Stm32f103Soc) and drives it from the Qt main thread:
// a QTimer fires onTick(), which runs a small chunk of steps and refreshes
// the CPU panel from cli::read_introspection(). The sim never runs on a
// QThread — that would break deterministic replay (DIRECTIVES §E).
#pragma once

#include "chips/stm32f1/stm32f103_soc.hpp"

#include <QMainWindow>
#include <QString>
#include <memory>
#include <string>
#include <vector>

class QLabel;
class QPushButton;
class QTableWidget;
class QTimer;

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
    void rebuildSoc();
    void refreshFromSnapshot();

    std::unique_ptr<chips::stm32f1::Stm32f103Soc> soc_;
    std::string usart_output_;
    QString firmware_path_;
    std::vector<uint8_t> firmware_data_;
    bool running_ = false;

    QTimer* timer_ = nullptr;
    QLabel* state_label_ = nullptr;
    QTableWidget* regs_table_ = nullptr;
    QLabel* gpio_label_ = nullptr;
    QPushButton* run_btn_ = nullptr;
};

} // namespace micro_forge::gui
