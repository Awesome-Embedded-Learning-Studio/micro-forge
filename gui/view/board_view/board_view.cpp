// Board view — see board_view.hpp.
#include "gui/view/board_view/board_view.hpp"

#include <QColor>
#include <QFont>
#include <QPainter>
#include <QPaintEvent>
#include <QPen>
#include <QPoint>
#include <QRect>
#include <QSizePolicy>
#include <QString>

#include <cstdint>

namespace micro_forge::gui::view {

namespace {
constexpr int kChipW = 190;
constexpr int kChipH = 240;
constexpr int kPinGap = 26;   // vertical gap between pins on one side
constexpr int kPinStart = 38; // first pin offset from chip top (room for label)
constexpr int kWireLen = 70;  // pin wire length out to the component
constexpr int kLedR = 12;     // LED radius
} // namespace

BoardView::BoardView(QWidget* parent) : QWidget(parent) {
    setMinimumSize(460, 400);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void BoardView::refresh(const introspection::IntrospectionSnapshot& snap) {
    odr_[0] = snap.peripherals.gpio[0].odr; // A
    odr_[1] = snap.peripherals.gpio[1].odr; // B (rendered pins currently use A/C)
    odr_[2] = snap.peripherals.gpio[2].odr; // C
    update(); // async — never block the tick loop on a repaint.
}

void BoardView::paintEvent(QPaintEvent*) {
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
    // pin-1 orientation dimple (top-left inside the chip)
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(130, 130, 130));
    p.drawEllipse(chip.x() + 14, chip.y() + 14, 6, 6);

    QFont bold = font();
    bold.setBold(true);
    bold.setPointSize(13);
    QFont small = font();
    small.setPointSize(8);

    p.setFont(bold);
    p.setPen(Qt::white);
    p.drawText(chip, Qt::AlignHCenter | Qt::AlignTop, "STM32F103");
    p.setFont(small);
    p.drawText(QPoint(chip.x() + kChipW / 2 - 24, chip.y() + 28), "Cortex-M3");

    const auto bit = [](std::uint16_t odr, int pin) -> bool {
        return (odr >> pin) & 1u;
    };

    // ── right-side pins: drive external components ──
    // Draws a pin stub on the chip's right edge + a coloured wire + name label,
    // returns the anchor point where the external component sits.
    const auto rightPin = [&](int row, const QString& name,
                              const QColor& wire) -> QPoint {
        const int y = chip_y + kPinStart + row * kPinGap;
        const int x0 = chip_x + kChipW;
        const int x1 = x0 + kWireLen;
        p.setPen(QPen(QColor(200, 200, 200), 1));
        p.setBrush(QColor(210, 210, 210));
        p.drawRect(x0 - 4, y - 3, 8, 6); // pin stub
        p.setPen(QPen(wire, 2));
        p.drawLine(x0, y, x1, y); // wire
        p.setPen(QColor(40, 40, 40));
        p.setFont(small);
        p.drawText(x0 + 6, y - 6, name); // pin name
        return QPoint(x1, y);
    };

    const auto drawLed = [&](const QPoint& anchor, bool on,
                             const QString& label) {
        const QRect led_rect(anchor.x() - kLedR, anchor.y() - kLedR,
                             kLedR * 2, kLedR * 2);
        p.setPen(QPen(QColor(60, 60, 60), 1));
        p.setBrush(on ? QColor(90, 220, 100) : QColor(70, 70, 70));
        p.drawEllipse(led_rect);
        p.setPen(QColor(40, 40, 40));
        p.setFont(small);
        p.drawText(led_rect.x() - 6, led_rect.bottom() + 12, label);
    };

    // PA5 → LED1 (the gpio_blink demo toggles this pin)
    drawLed(rightPin(0, "PA5", QColor(200, 40, 40)), bit(odr_[0], 5), "LED1");
    // PA0 → LED2
    drawLed(rightPin(1, "PA0", QColor(200, 130, 40)), bit(odr_[0], 0), "LED2");

    // PA9 → TX, PA10 → RX (UART markers, no LED)
    p.setPen(QColor(40, 40, 40));
    p.setFont(small);
    {
        const QPoint a = rightPin(2, "PA9", QColor(40, 120, 200));
        p.drawText(a.x() + 4, a.y() + 4, "TX \xe2\x86\x92"); // →
    }
    {
        const QPoint a = rightPin(3, "PA10", QColor(40, 120, 200));
        p.drawText(a.x() + 4, a.y() + 4, "\xe2\x86\x90 RX"); // ←
    }

    // ── left-side pins: SWD debug port ──
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

    // ── bottom pin: PC13 board LED ──
    {
        const int x = chip_x + kChipW / 2;
        const int y0 = chip_y + kChipH;
        const int y1 = y0 + 38;
        p.setPen(QPen(QColor(200, 200, 200), 1));
        p.setBrush(QColor(210, 210, 210));
        p.drawRect(x - 3, y0 - 4, 6, 8);
        p.setPen(QPen(QColor(40, 160, 200), 2));
        p.drawLine(x, y0, x, y1);
        const QRect led_rect(x - kLedR, y1, kLedR * 2, kLedR * 2);
        const bool on = bit(odr_[2], 13);
        p.setPen(QPen(QColor(60, 60, 60), 1));
        p.setBrush(on ? QColor(90, 220, 100) : QColor(70, 70, 70));
        p.drawEllipse(led_rect);
        p.setPen(QColor(40, 40, 40));
        p.setFont(small);
        p.drawText(led_rect.x() - 10, led_rect.bottom() + 12, "PC13 / LED3");
    }

    // ── legend ──
    p.setPen(QColor(130, 130, 130));
    p.setFont(small);
    p.drawText(12, height() - 12, "LED lit = pin drives high (ODR bit set)");
}

} // namespace micro_forge::gui::view
