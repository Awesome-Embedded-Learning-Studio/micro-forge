/**
 * @file stm32_run.cpp
 * @brief Minimal STM32F103 runner: load an ELF/bin, run, report visible effects.
 * @version 0.1.0
 */
#include <cctype>
#include <cstdint>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "micro_forge/basic_engine.hpp"
#include "micro_forge/machines/stm32f103.hpp"
#include "micro_forge/arch/arm/cortex_m3/cortex_m3.hpp"
#include "micro_forge/device/base/hooks/event_bus.hpp"

namespace {

bool done_ok(uint64_t left, const micro_forge::RunResult& result) {
    return left != 0 && result.reason == micro_forge::StopReason::kBudgetExhausted;

}

using micro_forge::StopReason;
using Machine = micro_forge::machines::Stm32F103Machine;

const char* reason_name(StopReason reason) {
    switch (reason) {
    case StopReason::kBudgetExhausted: return "budget-exhausted";
    case StopReason::kHalted: return "halted";
    case StopReason::kExited: return "exited";
    case StopReason::kDebugExit: return "debug-exit";
    case StopReason::kDecodeError: return "decode-error";
    case StopReason::kUnsupported: return "unsupported-instruction";
    case StopReason::kFault: return "fault";
    }
    return "unknown";
}

bool is_elf(const std::vector<uint8_t>& image) {
    return image.size() >= 4 && image[0] == 0x7F && image[1] == 'E' && image[2] == 'L' && image[3] == 'F';
}

} // namespace

int main(int argc, char** argv) {
    const char* path = nullptr;
    uint64_t cycles = UINT64_MAX; // unlimited unless --cycles
    bool semihost = false;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--cycles" && i + 1 < argc) {
            cycles = std::strtoull(argv[++i], nullptr, 0);
        } else if (arg == "--semihost") {
            semihost = true;
        } else {
            path = argv[i];
        }
    }
    if (path == nullptr) {
        std::fprintf(stderr, "usage: mf-stm32-run <firmware.bin|.axf> [--cycles N] [--semihost]\n");
        return 2;
    }

    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        std::fprintf(stderr, "cannot open %s\n", path);
        return 2;
    }
    const std::vector<uint8_t> image{std::istreambuf_iterator<char>(stream), {}};

    micro_forge::BasicEngine<Machine> engine(std::in_place);
    Machine& machine = engine.machine();
    machine.set_semihosting_enabled(semihost);

    unsigned led_edges = 0;
    bool led_high = false;
    machine.events().gpio.connect([&](const auto& edge) {
        if (edge.port == 'A' && edge.pin == 5) { // Nucleo-F103RB user LED
            ++led_edges;
            led_high = edge.rising;
            std::printf("[cpu=%llu] PA5 %s\n",
                        static_cast<unsigned long long>(machine.cpu().cycles().value_or(0)),
                        edge.rising ? "HIGH" : "LOW");
            std::fflush(stdout);
        }
    });

    bool loaded = false;
    if (is_elf(image)) {
        const auto result = machine.load_elf(image);
        loaded = result.has_value();
        if (!loaded) {
            std::fprintf(stderr, "elf load failed: kind=%u address=0x%llX\n",
                         static_cast<unsigned>(result.error().kind),
                         static_cast<unsigned long long>(result.error().address));
        }
    } else {
        loaded = machine.load_image(Machine::kFlashBase, image);
        if (!loaded) {
            std::fprintf(stderr, "raw image does not fit Flash at 0x%08X\n", Machine::kFlashBase);
        }
    }
    if (!loaded) {
        return 2;
    }
    if (cycles == UINT64_MAX) {
        std::printf("running indefinitely (Ctrl+C stops); pass --cycles N to bound it\n");
    }
    const auto booted = machine.reset();
    if (!booted) {
        std::fprintf(stderr, "reset failed: kind=%u address=0x%llX\n",
                     static_cast<unsigned>(booted.error().kind),
                     static_cast<unsigned long long>(booted.error().address));
        return 2;
    }

    std::size_t printed = 0;
    micro_forge::RunResult result{};
    while (done_ok(cycles, result)) {
        const uint64_t step = std::min<uint64_t>(500000ULL, cycles);
        result = engine.run(step);
        cycles -= step;
        const auto live = engine.console();
        if (live.size() > printed) {
            std::fwrite(live.data() + printed, 1, live.size() - printed, stdout);
            std::fflush(stdout);
            printed = live.size();
        }
    }
    const auto console = engine.console();
    std::printf("firmware: %s\n", path);
    std::printf("stop: %s pc=0x%08X code=%llu detail=0x%llX\n", reason_name(result.reason),
                static_cast<unsigned>(result.pc), static_cast<unsigned long long>(result.code),
                static_cast<unsigned long long>(result.detail));
    std::printf("cpu cycles: %llu\n", static_cast<unsigned long long>(machine.cpu().cycles().value_or(0)));
    std::printf("PA5 (user LED): %u edge(s), now %s\n", led_edges, led_high ? "HIGH" : "LOW");
    if (console.size() > printed) {
        std::printf("console:\n");
        std::fwrite(console.data() + printed, 1, console.size() - printed, stdout);
        std::fputc('\n', stdout);
    }
    return result.reason == StopReason::kBudgetExhausted || result.reason == StopReason::kExited ? 0 : 1;
}
