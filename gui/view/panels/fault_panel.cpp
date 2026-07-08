// Fault detail panel — see fault_panel.hpp.
#include "gui/view/panels/fault_panel.hpp"

#include "cpu/cpu.hpp"

#include <QLabel>
#include <QLatin1Char>
#include <QString>
#include <QVBoxLayout>

#include <cstdint>

namespace micro_forge::gui::panels {

namespace {

const char* fault_kind_name(cpu::CPU::CPUError k) {
    switch (k) {
        case cpu::CPU::CPUError::IllegalInstruction: return "IllegalInstruction";
        case cpu::CPU::CPUError::DataAccessFault: return "DataAccessFault";
        case cpu::CPU::CPUError::InstructionFetchFault: return "InstructionFetchFault";
        case cpu::CPU::CPUError::InvalidPc: return "InvalidPc";
        case cpu::CPU::CPUError::ExceptionEntryFault: return "ExceptionEntryFault";
        case cpu::CPU::CPUError::ExceptionReturnFault: return "ExceptionReturnFault";
        case cpu::CPU::CPUError::NotRunning: return "NotRunning";
        case cpu::CPU::CPUError::RegisterIndexOverflow: return "RegisterIndexOverflow";
        case cpu::CPU::CPUError::FailedPollIntr: return "FailedPollIntr";
    }
    return "Unknown";
}

// bus_error_raw is the BusError enum (core/types.hpp) stored as a raw int so
// this panel need not depend on memory/bus.hpp. Order: Unmapped=0, Unaligned,
// ReadOnly, InvalidDevice, RegionOverlap, OutOfRange, PeripheralFault=6.
const char* bus_error_name_raw(std::uint32_t raw) {
    switch (raw) {
        case 0: return "Unmapped";
        case 1: return "Unaligned";
        case 2: return "ReadOnly";
        case 3: return "InvalidDevice";
        case 4: return "RegionOverlap";
        case 5: return "OutOfRange";
        case 6: return "PeripheralFault";
        default: return "Unknown";
    }
}

} // namespace

FaultPanel::FaultPanel(QWidget* parent) : QWidget(parent) {
    auto* lay = new QVBoxLayout(this);
    auto* title = new QLabel("Fault detail");
    title->setStyleSheet("font-weight: bold;");
    lay->addWidget(title);

    label_ = new QLabel("— none —");
    label_->setStyleSheet("font-family: monospace;");
    lay->addWidget(label_);
}

void FaultPanel::refresh(const introspection::IntrospectionSnapshot& snap) {
    if (!snap.fault.present) {
        label_->setText("— none —");
        return;
    }
    auto hex = [](std::uint32_t v) {
        return QString("0x%1").arg(v, 8, 16, QLatin1Char('0'));
    };
    auto hex16 = [](std::uint16_t v) {
        return QString("0x%1").arg(v, 4, 16, QLatin1Char('0'));
    };
    const auto& f = snap.fault;
    QString s;
    s += QString("kind:   %1\n")
             .arg(QString::fromLatin1(fault_kind_name(f.kind)));
    s += QString("pc:     %1\n").arg(hex(f.pc));
    s += QString("lr:     %1\n").arg(hex(f.lr));
    s += QString("sp:     %1\n").arg(hex(f.sp));
    s += QString("xpsr:   %1\n").arg(hex(f.xpsr));
    if (f.is_32bit) {
        s += QString("insn:   %1 %2 (32-bit)\n")
                 .arg(hex16(f.opcode16), hex16(f.opcode16_2));
    } else {
        s += QString("insn:   %1 (16-bit)\n").arg(hex16(f.opcode16));
    }
    if (f.has_access_addr) {
        s += QString("access: %1\n").arg(hex(f.access_addr));
    }
    if (f.has_bus_error) {
        s += QString("bus:    %1\n")
                 .arg(QString::fromLatin1(bus_error_name_raw(f.bus_error_raw)));
    }
    label_->setText(s);
}

} // namespace micro_forge::gui::panels
