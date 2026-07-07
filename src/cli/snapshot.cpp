// JSON snapshot serialization. Hand-written, zero external deps.
// Addresses/values use lowercase hex strings; numbers stay decimal.
//
// Field VALUES come from cli::read_introspection() — the single source of
// truth shared with the GUI dashboard (milestone 04). This file owns only the
// JSON text shape; it does no simulator state reading of its own, so CLI and
// GUI can never disagree on what a register/fault field holds.
#include "cli/snapshot.hpp"
#include "cli/introspection.hpp"

#include "cpu/cpu.hpp"

#include <cstddef>
#include <cstdio>
#include <iomanip>
#include <ostream>

using namespace micro_forge;
using micro_forge::cpu::CPU;
using micro_forge::tools::MmioAccess;

namespace micro_forge::cli {
namespace {

void hex_kv(std::ostream& o, const char* key, unsigned v) {
    o << '"' << key << "\": \"0x" << std::hex << std::setfill('0')
      << std::setw(8) << v << '\"';
}

const char* state_json(CPU::State s) {
    switch (s) {
        case CPU::State::Running:
            return "Running";
        case CPU::State::Halted:
            return "Halted";
        case CPU::State::Faulted:
            return "Faulted";
    }
    return "Unknown";
}

const char* fault_kind_json(CPU::CPUError k) {
    switch (k) {
        case CPU::CPUError::IllegalInstruction:
            return "IllegalInstruction";
        case CPU::CPUError::DataAccessFault:
            return "DataAccessFault";
        case CPU::CPUError::InstructionFetchFault:
            return "InstructionFetchFault";
        case CPU::CPUError::InvalidPc:
            return "InvalidPc";
        case CPU::CPUError::ExceptionEntryFault:
            return "ExceptionEntryFault";
        case CPU::CPUError::ExceptionReturnFault:
            return "ExceptionReturnFault";
        case CPU::CPUError::NotRunning:
            return "NotRunning";
        case CPU::CPUError::RegisterIndexOverflow:
            return "RegisterIndexOverflow";
        case CPU::CPUError::FailedPollIntr:
            return "FailedPollIntr";
    }
    return "Unknown";
}

// Minimal JSON string escaping (control chars + quotes + backslash).
void json_string(std::ostream& o, std::string_view s) {
    o << '\"';
    for (char c : s) {
        switch (c) {
            case '"':
                o << "\\\"";
                break;
            case '\\':
                o << "\\\\";
                break;
            case '\n':
                o << "\\n";
                break;
            case '\r':
                o << "\\r";
                break;
            case '\t':
                o << "\\t";
                break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char b[8];
                    std::snprintf(b, sizeof(b), "\\u%04x",
                                  static_cast<unsigned char>(c));
                    o << b;
                } else {
                    o << c;
                }
        }
    }
    o << '\"';
}

} // namespace

void write_snapshot_json(chips::stm32f1::Stm32f103Soc& soc, std::ostream& out,
                         const SnapshotExtras& extras) {
    const IntrospectionSnapshot snap = read_introspection(soc, extras.usart_output);
    out << "{";

    // ── cpu ──
    out << "\"cpu\": {";
    out << "\"state\": \"" << state_json(snap.cpu.state) << "\", ";
    out << "\"mode\": \"" << (snap.cpu.handler_mode ? "handler" : "thread")
        << "\", ";
    hex_kv(out, "pc", static_cast<unsigned>(snap.cpu.pc));
    out << ", ";
    hex_kv(out, "lr", static_cast<unsigned>(snap.cpu.lr));
    out << ", ";
    hex_kv(out, "sp", static_cast<unsigned>(snap.cpu.sp));
    out << ", ";
    hex_kv(out, "xpsr", static_cast<unsigned>(snap.cpu.xpsr));
    out << ", ";
    hex_kv(out, "primask", static_cast<unsigned>(snap.cpu.primask));
    out << ", ";
    hex_kv(out, "basepri", static_cast<unsigned>(snap.cpu.basepri));
    out << ", ";
    hex_kv(out, "faultmask", static_cast<unsigned>(snap.cpu.faultmask));
    out << ", ";
    hex_kv(out, "control", static_cast<unsigned>(snap.cpu.control));
    out << ", ";
    hex_kv(out, "msp", static_cast<unsigned>(snap.cpu.msp));
    out << ", ";
    hex_kv(out, "psp", static_cast<unsigned>(snap.cpu.psp));
    out << ", \"regs\": {";
    for (std::size_t r = 0; r < snap.cpu.regs.size(); ++r) {
        if (r) {
            out << ", ";
        }
        // std::dec before the register index: iostream hex is sticky and would
        // otherwise print r10 as "ra" (regression guard, see notes 006).
        out << "\"r" << std::dec << r << "\": \"0x" << std::hex
            << std::setfill('0') << std::setw(8)
            << static_cast<unsigned>(snap.cpu.regs[r]) << '\"';
    }
    out << "}}, ";

    // ── fault ──
    if (snap.fault.present) {
        out << "\"fault\": {";
        out << "\"kind\": \"" << fault_kind_json(snap.fault.kind) << "\", ";
        hex_kv(out, "pc", static_cast<unsigned>(snap.fault.pc));
        out << ", ";
        hex_kv(out, "lr", static_cast<unsigned>(snap.fault.lr));
        out << ", ";
        hex_kv(out, "sp", static_cast<unsigned>(snap.fault.sp));
        out << ", ";
        hex_kv(out, "xpsr", static_cast<unsigned>(snap.fault.xpsr));
        out << ", ";
        out << "\"is_32bit\": " << (snap.fault.is_32bit ? "true" : "false");
        out << ", ";
        hex_kv(out, "opcode16", static_cast<unsigned>(snap.fault.opcode16));
        out << ", ";
        hex_kv(out, "opcode16_2", static_cast<unsigned>(snap.fault.opcode16_2));
        if (snap.fault.has_access_addr) {
            out << ", ";
            hex_kv(out, "access_addr",
                   static_cast<unsigned>(snap.fault.access_addr));
        }
        if (snap.fault.has_bus_error) {
            out << ", ";
            hex_kv(out, "bus_error",
                   static_cast<unsigned>(snap.fault.bus_error_raw));
        }
        out << "}, ";
    } else {
        out << "\"fault\": null, ";
    }

    // ── run ──
    out << "\"run\": {\"cycles\": " << std::dec << snap.cycles << "}, ";

    // ── peripherals ──
    out << "\"peripherals\": {\"usart_output\": ";
    json_string(out, snap.peripherals.usart_output);
    out << ", \"gpio\": [";
    for (std::size_t i = 0; i < snap.peripherals.gpio.size(); ++i) {
        if (i) {
            out << ", ";
        }
        const auto& g = snap.peripherals.gpio[i];
        out << "{\"port\": \"" << g.port << "\", ";
        hex_kv(out, "odr", static_cast<unsigned>(g.odr));
        out << '}';
    }
    out << "], \"systick\": {";
    hex_kv(out, "ctrl", static_cast<unsigned>(snap.peripherals.systick.ctrl));
    out << ", ";
    hex_kv(out, "load", static_cast<unsigned>(snap.peripherals.systick.load));
    out << ", ";
    hex_kv(out, "val", static_cast<unsigned>(snap.peripherals.systick.val));
    out << "}, \"nvic\": {";
    out << "\"has_pending\": "
        << (snap.peripherals.nvic.has_pending ? "true" : "false") << ", ";
    // Decimal for the integer counters (hex_kv above leaves hex sticky).
    out << "\"highest_pending_irq\": " << std::dec
        << static_cast<unsigned>(snap.peripherals.nvic.highest_pending_irq)
        << ", ";
    out << "\"enabled_count\": " << snap.peripherals.nvic.enabled_count;
    out << "}, \"scb\": {";
    hex_kv(out, "icsr", static_cast<unsigned>(snap.peripherals.scb.icsr));
    out << ", ";
    hex_kv(out, "vtor", static_cast<unsigned>(snap.peripherals.scb.vtor));
    out << ", ";
    hex_kv(out, "aircr", static_cast<unsigned>(snap.peripherals.scb.aircr));
    out << ", ";
    // Decimal: hex_kv above leaves hex sticky; prigroup is 0..7.
    out << "\"prigroup\": " << std::dec
        << static_cast<unsigned>(snap.peripherals.scb.prigroup);
    out << "}}, ";

    // ── events (MMIO access ring, caller-collected) ──
    out << "\"events\": [";
    for (std::size_t i = 0; i < extras.events.size(); ++i) {
        if (i) {
            out << ", ";
        }
        const MmioAccess& e = extras.events[i];
        out << "{\"op\": \"" << (e.is_write ? "W" : "R") << "\", ";
        hex_kv(out, "addr", static_cast<unsigned>(e.addr));
        out << ", ";
        hex_kv(out, "value", static_cast<unsigned>(e.value));
        out << ", \"dev\": \"";
        if (!e.device.empty()) {
            out << e.device;
        } else {
            out << "?";
        }
        out << "\", \"ok\": " << (e.ok ? "true" : "false") << '}';
    }
    out << "]";
    out << "}\n";
}

} // namespace micro_forge::cli
