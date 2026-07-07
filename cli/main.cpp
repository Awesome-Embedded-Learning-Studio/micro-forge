// micro-forge CLI — unified firmware runner.
// B1 scope: `run` subcommand, argument parsing, human-readable output.
//   stdout  ← firmware output (USART)              (pipeable / assertable)
//   stderr  ← run status + fault summary           (diagnostics)
#include "arch/arm/cortex_m3/cortex_m3.hpp"
#include "chips/stm32f1/soc/stm32f103_soc.hpp"
#include "cli/snapshot.hpp"
#include "cpu/cpu.hpp"
#include "sim/coordinator.hpp"
#include "tools/memory_dump.hpp"
#include "tools/mmio_trace.hpp"

#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <span>
#include <string>
#include <vector>

using namespace micro_forge;
using namespace micro_forge::chips::stm32f1;

namespace {

// Ctrl+C flips this; the run loop checks it between chunks so a default
// unbounded run (no --max-steps) stops gracefully with a status report.
volatile std::sig_atomic_t g_interrupted = 0;
void on_sigint(int) {
    g_interrupted = 1;
}

std::vector<uint8_t> read_file(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(f), {}};
}

bool is_elf(const std::vector<uint8_t>& d) {
    return d.size() >= 4 && d[0] == 0x7f && d[1] == 'E' && d[2] == 'L' &&
           d[3] == 'F';
}

const char* state_name(cpu::CPU::State s) {
    switch (s) {
        case cpu::CPU::State::Running:
            return "Running";
        case cpu::CPU::State::Halted:
            return "Halted";
        case cpu::CPU::State::Faulted:
            return "Faulted";
    }
    return "Unknown";
}

const char* fault_kind_name(cpu::CPU::CPUError k) {
    switch (k) {
        case cpu::CPU::CPUError::IllegalInstruction:
            return "IllegalInstruction";
        case cpu::CPU::CPUError::DataAccessFault:
            return "DataAccessFault";
        case cpu::CPU::CPUError::InstructionFetchFault:
            return "InstructionFetchFault";
        case cpu::CPU::CPUError::InvalidPc:
            return "InvalidPc";
        case cpu::CPU::CPUError::ExceptionEntryFault:
            return "ExceptionEntryFault";
        case cpu::CPU::CPUError::ExceptionReturnFault:
            return "ExceptionReturnFault";
        case cpu::CPU::CPUError::NotRunning:
            return "NotRunning";
        case cpu::CPU::CPUError::RegisterIndexOverflow:
            return "RegisterIndexOverflow";
        case cpu::CPU::CPUError::FailedPollIntr:
            return "FailedPollIntr";
    }
    return "Unknown";
}

const char* bus_error_name(BusError b) {
    switch (b) {
        case BusError::Unmapped: return "Unmapped";
        case BusError::Unaligned: return "Unaligned";
        case BusError::ReadOnly: return "ReadOnly";
        case BusError::InvalidDevice: return "InvalidDevice";
        case BusError::RegionOverlap: return "RegionOverlap";
        case BusError::OutOfRange: return "OutOfRange";
        case BusError::PeripheralFault: return "PeripheralFault";
    }
    return "Unknown";
}

const char* width_name(Width w) {
    switch (w) {
        case Width::Byte: return "byte";
        case Width::HalfWord: return "halfword";
        case Width::Word: return "word";
    }
    return "?";
}

// Create the SoC and load firmware (ELF or raw binary). Shared by `run` and
// `dump-mem` so the loading path lives in exactly one place.
std::expected<std::unique_ptr<Stm32f103Soc>, std::string> load_firmware(
    const std::vector<uint8_t>& data, uint32_t base) {
    auto soc = Stm32f103Soc::create();
    if (!soc) return std::unexpected(soc.error());
    auto lr = is_elf(data) ? (*soc)->load_elf(data)
                           : (*soc)->load_bin(base, data);
    if (!lr) return std::unexpected(lr.error());
    return std::move(*soc);
}

const char* run_result_name(sim::RunResult r) {
    switch (r) {
        case sim::RunResult::Running:
            return "MaxSteps";
        case sim::RunResult::Halted:
            return "Halted";
        case sim::RunResult::Faulted:
            return "Faulted";
        case sim::RunResult::StepError:
            return "StepError";
    }
    return "Unknown";
}

void print_usage() {
    std::fprintf(
        stderr,
        "usage: micro-forge <subcommand> [options]\n"
        "  run <firmware.{elf,bin}> [--chip stm32f103] [--base 0x08000000]\n"
        "      [--max-steps N] [--trace-mmio] [--snapshot-json FILE]\n"
        "      (runs forever by default; Ctrl+C stops with a report)\n"
        "  dump-mem <firmware.{elf,bin}> --addr 0x20000000 [--len 256]\n"
        "      [--chip stm32f103] [--base 0x08000000]\n"
        "      (loads firmware, dumps a memory window to stdout)\n");
}

struct RunOptions {
    std::string chip = "stm32f103";
    std::string firmware;
    uint32_t base = 0x08000000;
    size_t max_steps = SIZE_MAX;
    bool trace_mmio = false;
    std::string snapshot_json; // B2/B3: empty = none
};

int cmd_run(int argc, char** argv) {
    RunOptions opt;
    for (int i = 0; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() -> const char* {
            return (i + 1 < argc) ? argv[++i] : nullptr;
        };
        if (a == "--chip") {
            const char* v = next();
            if (!v) {
                std::fprintf(stderr, "--chip needs a value\n");
                return 2;
            }
            opt.chip = v;
        } else if (a == "--base") {
            const char* v = next();
            if (!v) {
                std::fprintf(stderr, "--base needs a value\n");
                return 2;
            }
            opt.base = static_cast<uint32_t>(std::strtoul(v, nullptr, 0));
        } else if (a == "--max-steps") {
            const char* v = next();
            if (!v) {
                std::fprintf(stderr, "--max-steps needs a value\n");
                return 2;
            }
            opt.max_steps = static_cast<size_t>(std::strtoull(v, nullptr, 0));
        } else if (a == "--trace-mmio") {
            opt.trace_mmio = true;
        } else if (a == "--snapshot-json") {
            const char* v = next();
            if (!v) {
                std::fprintf(stderr, "--snapshot-json needs a value\n");
                return 2;
            }
            opt.snapshot_json = v;
        } else if (!a.empty() && a[0] != '-') {
            if (opt.firmware.empty()) {
                opt.firmware = a;
            } else {
                std::fprintf(stderr, "unexpected positional arg: %s\n",
                             a.c_str());
                return 2;
            }
        } else {
            std::fprintf(stderr, "unknown option: %s\n", a.c_str());
            print_usage();
            return 2;
        }
    }

    if (opt.chip != "stm32f103") {
        std::fprintf(stderr, "unsupported chip '%s' (only stm32f103)\n",
                     opt.chip.c_str());
        return 2;
    }
    if (opt.firmware.empty()) {
        std::fprintf(stderr, "missing firmware path\n");
        print_usage();
        return 2;
    }
    auto data = read_file(opt.firmware);
    if (data.empty()) {
        std::fprintf(stderr, "cannot read firmware: %s\n",
                     opt.firmware.c_str());
        return 1;
    }

    auto soc = load_firmware(data, opt.base);
    if (!soc) {
        std::fprintf(stderr, "firmware load failed: %s\n",
                     soc.error().c_str());
        return 1;
    }

    std::string usart_out;
    (*soc)->parts().event_bus.uart.connect(
        [&](const hooks::UartByte& e) {
            usart_out += static_cast<char>(e.byte);
        });

    // B3: collect recent MMIO accesses into a capped ring for diagnostics.
    std::vector<tools::MmioAccess> events;
    constexpr size_t kEventCap = 256;
    const bool want_events = opt.trace_mmio || !opt.snapshot_json.empty();
    if (want_events) {
        tools::enable_mmio_trace(*(*soc)->machine().bus,
                                 [&](const tools::MmioAccess& a) {
                                     if (events.size() >= kEventCap) {
                                         events.erase(events.begin());
                                     }
                                     events.push_back(a);
                                 });
    }

    // Run in chunks so SIGINT (Ctrl+C) can interrupt a default unbounded run.
    sim::RunResult run_res = sim::RunResult::Running;
    size_t remaining = opt.max_steps;
    constexpr size_t kChunk = 100000;
    while (remaining > 0 && !g_interrupted) {
        size_t chunk = remaining < kChunk ? remaining : kChunk;
        run_res = (*soc)->run(chunk);
        remaining -= chunk;
        if (run_res != sim::RunResult::Running) {
            break;
        }
    }
    bool interrupted = g_interrupted != 0;

    // Firmware output → stdout (pipeable / assertable).
    if (!usart_out.empty()) {
        std::fwrite(usart_out.data(), 1, usart_out.size(), stdout);
        std::fflush(stdout);
    }

    // Status → stderr.
    auto cm3 = (*soc)->cortex_m3_cpu();
    auto st_res = (*soc)->machine().cpu->state();
    cpu::CPU::State st = st_res ? *st_res : cpu::CPU::State::Halted;
    const char* stop = interrupted ? "Interrupted" : run_result_name(run_res);
    std::fprintf(stderr, "[micro-forge] state=%s stop=%s\n", state_name(st),
                 stop);

    if (cm3.IsValid()) {
        const auto& fr = cm3->last_fault();
        if (fr.has_value()) {
            std::fprintf(stderr,
                "[fault] kind=%s pc=0x%08X lr=0x%08X sp=0x%08X\n",
                fault_kind_name(fr->kind), static_cast<unsigned>(fr->pc),
                static_cast<unsigned>(fr->lr), static_cast<unsigned>(fr->sp));
            if (fr->is_32bit) {
                std::fprintf(stderr, "       insn=0x%04X 0x%04X (32-bit)\n",
                    fr->opcode16, fr->opcode16_2);
            } else {
                std::fprintf(stderr, "       insn=0x%04X (16-bit)\n",
                    fr->opcode16);
            }
            if (fr->access_addr) {
                std::fprintf(stderr, "       access=0x%08X",
                    static_cast<unsigned>(*fr->access_addr));
                if (fr->access_width) {
                    std::fprintf(stderr, " (%s)",
                        width_name(*fr->access_width));
                }
                std::fputc('\n', stderr);
            }
            if (fr->bus_error) {
                std::fprintf(stderr, "       bus_error=%s\n",
                    bus_error_name(*fr->bus_error));
            }
        }
    }

    if (opt.trace_mmio) {
        for (const auto& e : events) {
            char buf[160];
            auto sv = tools::format_mmio_access(e, buf, sizeof(buf));
            std::fprintf(stderr, "%.*s\n", static_cast<int>(sv.size()),
                         sv.data());
        }
    }

    if (!opt.snapshot_json.empty()) {
        std::ofstream sf(opt.snapshot_json);
        if (!sf) {
            std::fprintf(stderr, "cannot write snapshot: %s\n",
                         opt.snapshot_json.c_str());
        } else {
            cli::SnapshotExtras extras{
                std::span<const tools::MmioAccess>(events), usart_out};
            cli::write_snapshot_json(**soc, sf, extras);
            std::fprintf(stderr, "[micro-forge] snapshot → %s\n",
                         opt.snapshot_json.c_str());
        }
    }

    bool bad = (st == cpu::CPU::State::Faulted) ||
               (run_res == sim::RunResult::StepError);
    return bad ? 1 : 0;
}

int cmd_dump_mem(int argc, char** argv) {
    std::string chip = "stm32f103", firmware;
    uint32_t base = 0x08000000, addr = 0, len = 256;
    bool addr_set = false;
    for (int i = 0; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() -> const char* {
            return (i + 1 < argc) ? argv[++i] : nullptr;
        };
        if (a == "--chip") {
            const char* v = next();
            if (!v) { std::fprintf(stderr, "--chip needs a value\n"); return 2; }
            chip = v;
        } else if (a == "--base") {
            const char* v = next();
            if (!v) { std::fprintf(stderr, "--base needs a value\n"); return 2; }
            base = static_cast<uint32_t>(std::strtoul(v, nullptr, 0));
        } else if (a == "--addr") {
            const char* v = next();
            if (!v) { std::fprintf(stderr, "--addr needs a value\n"); return 2; }
            addr = static_cast<uint32_t>(std::strtoul(v, nullptr, 0));
            addr_set = true;
        } else if (a == "--len") {
            const char* v = next();
            if (!v) { std::fprintf(stderr, "--len needs a value\n"); return 2; }
            len = static_cast<uint32_t>(std::strtoul(v, nullptr, 0));
        } else if (!a.empty() && a[0] != '-') {
            if (firmware.empty()) {
                firmware = a;
            } else {
                std::fprintf(stderr, "unexpected positional arg: %s\n", a.c_str());
                return 2;
            }
        } else {
            std::fprintf(stderr, "unknown option: %s\n", a.c_str());
            print_usage();
            return 2;
        }
    }
    if (chip != "stm32f103") {
        std::fprintf(stderr, "unsupported chip '%s' (only stm32f103)\n", chip.c_str());
        return 2;
    }
    if (firmware.empty()) {
        std::fprintf(stderr, "missing firmware path\n");
        print_usage();
        return 2;
    }
    if (!addr_set) {
        std::fprintf(stderr, "missing --addr (e.g. --addr 0x20000000)\n");
        return 2;
    }

    auto data = read_file(firmware);
    if (data.empty()) {
        std::fprintf(stderr, "cannot read firmware: %s\n", firmware.c_str());
        return 1;
    }
    auto soc = load_firmware(data, base);
    if (!soc) {
        std::fprintf(stderr, "firmware load failed: %s\n", soc.error().c_str());
        return 1;
    }

    // Dump the requested memory window to stdout via the shared tool.
    tools::memory_dump(*(*soc)->machine().bus, addr, len,
                       [](std::string_view sv) {
                           std::fwrite(sv.data(), 1, sv.size(), stdout);
                       });
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    std::signal(SIGINT, on_sigint);
    if (argc < 2) {
        print_usage();
        return 2;
    }
    std::string sub = argv[1];
    if (sub == "run") {
        return cmd_run(argc - 2, argv + 2);
    }
    if (sub == "dump-mem") {
        return cmd_dump_mem(argc - 2, argv + 2);
    }
    if (sub == "-h" || sub == "--help" || sub == "help") {
        print_usage();
        return 0;
    }
    std::fprintf(stderr, "unknown subcommand: %s\n", sub.c_str());
    print_usage();
    return 2;
}
