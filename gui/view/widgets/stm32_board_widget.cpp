// STM32F103 board widget — see stm32_board_widget.hpp.
#include "gui/view/widgets/stm32_board_widget.hpp"

#include "QuarkWidgets/LedPanel.hpp"
#include "QuarkWidgets/QuarkBulb.hpp"

#include <QColor>
#include <QFont>
#include <QPainter>
#include <QPaintEvent>
#include <QPen>
#include <QPoint>
#include <QRect>
#include <QResizeEvent>
#include <QSize>
#include <QSizePolicy>
#include <QString>

namespace micro_forge::gui::view {

namespace {
constexpr int kChipW = 200;
constexpr int kChipH = 250;
constexpr int kPinGap = 22;
constexpr int kPinStart = 30; // first pin offset from chip top (room for label)
constexpr int kWireLen = 70;  // pin wire length out to the LED
constexpr int kPc13Bulb = 24; // PC13 on-board bulb side, px
} // namespace

Stm32BoardWidget::Stm32BoardWidget(QWidget* parent) : QWidget(parent) {
    setMinimumSize(600, 440);   // widened from 480×420 to fit the LED panel
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    // Port-A LED row (PA0..PA7 + ODR hex) — a LedPanel child. Green tint keeps
    // the board's "lit = pin drives high" semantics; small bulbs fit a dense
    // pin column beside the chip.
    ledPanel_ = new quark::LedPanel(8, Qt::Vertical, this);
    ledPanel_->setLabelPrefix(QStringLiteral("PA"));
    ledPanel_->setColor(QColor(90, 220, 100));
    ledPanel_->setBulbSize(QSize(24, 24));

    // On-board PC13 LED — same green "lit" semantics as the port-A row.
    pc13Bulb_ = new quark::QuarkBulb(this);
    pc13Bulb_->setColor(QColor(90, 220, 100));
    pc13Bulb_->setFixedSize(kPc13Bulb, kPc13Bulb);
}

void Stm32BoardWidget::refresh(
    const introspection::IntrospectionSnapshot& snap) {
    odr_[0] = snap.peripherals.gpio[0].odr; // A
    odr_[1] = snap.peripherals.gpio[1].odr; // B (rendered pins use A/C)
    odr_[2] = snap.peripherals.gpio[2].odr; // C
    if (ledPanel_ != nullptr) ledPanel_->setLevels(odr_[0]); // PA0..PA7
    if (pc13Bulb_ != nullptr) {                            // PC13 = port C bit 13
        // Blue Pill PC13 LED is active-low: lit when ODR bit13 == 0 (firmware
        // led.on() writes BSRR reset → ODR=0 → LED conducts). Invert so the
        // bulb reflects the physical LED, not the raw register bit.
        pc13Bulb_->setState(!static_cast<bool>((odr_[2] >> 13) & 1u));
    }
    update(); // async — never block the tick loop on a repaint.
}

void Stm32BoardWidget::resizeEvent(QResizeEvent*) {
    const int chip_x = (width() - kChipW) / 2;
    const int chip_y = (height() - kChipH) / 2;

    if (ledPanel_ != nullptr) {
        // Place the LED panel just past the UART wire stubs, aligned to the chip.
        const int x = chip_x + kChipW + kWireLen + 20;
        const QSize hint = ledPanel_->sizeHint();
        ledPanel_->setGeometry(x, chip_y, qMax(96, hint.width()),
                               qMax(kChipH, hint.height()));
    }
    if (pc13Bulb_ != nullptr) {
        // Below the chip, on centre: the pin pad + wire are painted in
        // paintEvent; the bulb self-draws here.
        const int cx = chip_x + kChipW / 2;
        const int y1 = chip_y + kChipH + 38;   // wire length, matches paintEvent
        pc13Bulb_->setGeometry(cx - kPc13Bulb / 2, y1, kPc13Bulb, kPc13Bulb);
    }
}

void Stm32BoardWidget::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.fillRect(rect(), QColor(245, 245, 240)); // off-white PCB

    const int chip_x = (width() - kChipW) / 2;
    const int chip_y = (height() - kChipH) / 2;
    const QRect chip(chip_x, chip_y, kChipW, kChipH);

    // ── chip body ──
    p.setPen(QPen(QColor(20, 20, 20), 1));
    p.setBrush(QColor(35, 35, 40));
    p.drawRoundedRect(chip, 10, 10);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(130, 130, 130));
    p.drawEllipse(chip.x() + 14, chip.y() + 14, 6, 6); // pin-1 dimple

    QFont bold = font();
    bold.setBold(true);
    bold.setPointSize(13);
    QFont small = font();
    small.setPointSize(8);
    p.setFont(bold);
    p.setPen(Qt::white);
    p.drawText(chip, Qt::AlignHCenter | Qt::AlignTop, "STM32F103");
    p.setFont(small);
    p.drawText(QPoint(chip.x() + kChipW / 2 - 24, chip.y() + 26), "Cortex-M3");

    // ── PA0..PA7 LEDs: hosted by the ledPanel_ child (see refresh / resizeEvent).
    //    Only the UART markers (PA9/PA10) + SWD + the PC13 pad/wire/label are
    //    painted below — PC13's LED itself is the pc13Bulb_ child.

    // PA9/PA10 UART markers (wire + label, no LED).
    const auto rightTag = [&](int row, const QString& name,
                              const QColor& wire, const QString& tag) {
        const int y = chip_y + kPinStart + (8 + row) * kPinGap;
        const int x0 = chip_x + kChipW;
        const int x1 = x0 + kWireLen;
        p.setPen(QPen(QColor(200, 200, 200), 1));
        p.setBrush(QColor(210, 210, 210));
        p.drawRect(x0 - 4, y - 3, 8, 6);
        p.setPen(QPen(wire, 2));
        p.drawLine(x0, y, x1, y);
        p.setPen(QColor(40, 40, 40));
        p.setFont(small);
        p.drawText(x0 + 6, y - 6, name);
        p.drawText(x1 + 4, y + 4, tag);
    };
    rightTag(0, "PA9", QColor(40, 120, 200), "TX \xe2\x86\x92"); // →
    rightTag(1, "PA10", QColor(40, 120, 200), "\xe2\x86\x90 RX"); // ←

    // ── left side: SWD debug port ──
    const auto leftPin = [&](int row, const QString& name) {
        const int y = chip_y + kPinStart + row * kPinGap;
        const int x0 = chip_x;
        const int x1 = x0 - kWireLen;
        p.setPen(QPen(QColor(200, 200, 200), 1));
        p.setBrush(QColor(210, 210, 210));
        p.drawRect(x0 - 4, y - 3, 8, 6);
        p.setPen(QPen(QColor(150, 150, 150), 2));
        p.drawLine(x0, y, x1, y);
        p.setPen(QColor(40, 40, 40));
        p.setFont(small);
        p.drawText(x1 - 30, y - 6, name);
    };
    leftPin(0, "PA13");
    leftPin(1, "PA14");
    p.setPen(QColor(90, 90, 90));
    p.setFont(small);
    p.drawText(QPoint(chip_x - kWireLen, chip_y + kPinStart + 2 * kPinGap + 14),
               "SWD");

    // ── bottom: PC13 — pin pad + wire + label painted here; the LED itself
    //    is the pc13Bulb_ child (positioned in resizeEvent, lit in refresh) ──
    {
        const int x = chip_x + kChipW / 2;
        const int y0 = chip_y + kChipH;
        const int y1 = y0 + 38;
        p.setPen(QPen(QColor(200, 200, 200), 1));
        p.setBrush(QColor(210, 210, 210));
        p.drawRect(x - 3, y0 - 4, 6, 8);          // pin pad on the chip edge
        p.setPen(QPen(QColor(40, 160, 200), 2));
        p.drawLine(x, y0, x, y1);                 // wire down to the bulb
        p.setPen(QColor(40, 40, 40));
        p.setFont(small);
        p.drawText(x - 30, y1 + kPc13Bulb + 14, "PC13 / LED");
    }

    // ── legend ──
    p.setPen(QColor(130, 130, 130));
    p.setFont(small);
    p.drawText(12, height() - 12, "LED lit = pin drives high (ODR bit set)");
}

} // namespace micro_forge::gui::view
