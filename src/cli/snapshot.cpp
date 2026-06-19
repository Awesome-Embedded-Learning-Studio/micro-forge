// JSON snapshot serialization. Hand-written, zero external deps.
// Addresses/values use lowercase hex strings; numbers stay decimal.
#include "cli/snapshot.hpp"

#include "arch/arm/cortex_m3/cortex_m3.hpp"
#include "chips/stm32f1/stm32f103_soc.hpp"
#include "cpu/cpu.hpp"

#include <cstdio>
#include <iomanip>
#include <ostream>

using namespace micro_forge;
using micro_forge::chips::stm32f1::Stm32f103Soc;
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
        case CPU::State::Running: return "Running";
        case CPU::State::Halted:  return "Halted";
        case CPU::State::Faulted: return "Faulted";
    }
    return "Unknown";
}

const char* fault_kind_json(CPU::CPUError k) {
    switch (k) {
        case CPU::CPUError::IllegalInstruction:    return "IllegalInstruction";
        case CPU::CPUError::DataAccessFault:       return "DataAccessFault";
        case CPU::CPUError::InstructionFetchFault: return "InstructionFetchFault";
        case CPU::CPUError::InvalidPc:             return "InvalidPc";
        case CPU::CPUError::ExceptionEntryFault:   return "ExceptionEntryFault";
        case CPU::CPUError::ExceptionReturnFault:  return "ExceptionReturnFault";
        case CPU::CPUError::NotRunning:            return "NotRunning";
        case CPU::CPUError::RegisterIndexOverflow: return "RegisterIndexOverflow";
        case CPU::CPUError::FailedPollIntr:        return "FailedPollIntr";
    }
    return "Unknown";
}

// Minimal JSON string escaping (control chars + quotes + backslash).
void json_string(std::ostream& o, std::string_view s) {
    o << '\"';
    for (char c : s) {
        switch (c) {
            case '"':  o << "\\\""; break;
            case '\\': o << "\\\\"; break;
            case '\n': o << "\\n";  break;
            case '\r': o << "\\r";  break;
            case '\t': o << "\\t";  break;
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

void write_snapshot_json(Stm32f103Soc& soc, std::ostream& out,
                         const SnapshotExtras& extras) {
    out << "{";
    auto cm3 = soc.cortex_m3_cpu();
    if (cm3.IsValid()) {
        CPU::State st = cm3->state().value_or(CPU::State::Halted);

        out << "\"cpu\": {";
        out << "\"state\": \"" << state_json(st) << "\", ";
        out << "\"mode\": \"" << (cm3->in_handler_mode() ? "handler" : "thread")
            << "\", ";
        hex_kv(out, "pc", static_cast<unsigned>(cm3->pc().value_or(0)));
        out << ", ";
        hex_kv(out, "lr", static_cast<unsigned>(cm3->register_value(14).value_or(0)));
        out << ", ";
        hex_kv(out, "sp", static_cast<unsigned>(cm3->register_value(13).value_or(0)));
        out << ", \"regs\": {";
        for (int r = 0; r <= 12; ++r) {
            if (r) {
                out << ", ";
            }
            out << "\"r" << std::dec << r << "\": \"0x" << std::hex
                << std::setfill('0') << std::setw(8)
                << static_cast<unsigned>(cm3->register_value(r).value_or(0))
                << '\"';
        }
        out << "}}, ";

        const auto& fr = cm3->last_fault();
        if (fr.has_value()) {
            out << "\"fault\": {";
            out << "\"kind\": \"" << fault_kind_json(fr->kind) << "\", ";
            hex_kv(out, "pc", static_cast<unsigned>(fr->pc));
            out << ", ";
            hex_kv(out, "lr", static_cast<unsigned>(fr->lr));
            out << ", ";
            hex_kv(out, "sp", static_cast<unsigned>(fr->sp));
            out << ", \"is_32bit\": " << (fr->is_32bit ? "true" : "false")
                << "}, ";
        } else {
            out << "\"fault\": null, ";
        }
    }

    out << "\"run\": {\"cycles\": " << std::dec
        << soc.machine().cpu->cycles().value_or(0) << "}, ";

    out << "\"peripherals\": {\"usart_output\": ";
    json_string(out, extras.usart_output);
    out << "}, ";

    out << "\"events\": [";
    for (size_t i = 0; i < extras.events.size(); ++i) {
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
