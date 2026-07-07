// micro-forge GUI main window.
//
// Thin Qt view over a model::Session: a QTimer fires onTick(), which asks the
// session to run a small chunk of steps and then refreshes the panels from the
// session's IntrospectionSnapshot. Owns no simulator state directly — that
// lives in model::Session (Qt-free, unit-testable). The sim never runs on a
// QThread — that would break deterministic replay (DIRECTIVES §E).
#pragma once

#include "gui/model/session.hpp"

#include <QMainWindow>
#include <QString>

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
    void rebuildSession();
    void refreshFromSnapshot();

    model::Session session_;
    bool running_ = false;

    QTimer* timer_ = nullptr;
    QLabel* state_label_ = nullptr;
    QTableWidget* regs_table_ = nullptr;
    QLabel* gpio_label_ = nullptr;
    QPushButton* run_btn_ = nullptr;
};

} // namespace micro_forge::gui
