// Memory panel — interactive hex dump of a memory region (C4-mem). Unlike the
// snapshot panels, this one drives a query: the user types an address, and
// MainWindow reads that region from the SoC bus via tools::memory_dump (routed
// through Session) and pushes the dump back here each tick so it tracks memory
// as firmware runs. The disassembler is intentionally out of scope (later
// batch) — this panel only shows raw bytes.
#pragma once

#include <QWidget>

#include <cstdint>

class QLineEdit;
class QString;
class QTextEdit;

namespace micro_forge::gui::panels {

class MemoryPanel : public QWidget {
    Q_OBJECT
  public:
    explicit MemoryPanel(QWidget* parent = nullptr);

    bool has_addr() const noexcept { return has_addr_; }
    std::uint32_t current_addr() const noexcept { return current_addr_; }

    // Display a freshly-read hex dump (called by MainWindow each tick).
    void show_dump(const QString& text);

  Q_SIGNALS:
    // Emitted when the user enters a new address (Enter). MainWindow reacts by
    // reading the region immediately instead of waiting for the next tick.
    void addr_changed(std::uint32_t addr);

  private:
    QLineEdit* addr_input_;
    QTextEdit* dump_view_;
    std::uint32_t current_addr_ = 0;
    bool has_addr_ = false;
};

} // namespace micro_forge::gui::panels
