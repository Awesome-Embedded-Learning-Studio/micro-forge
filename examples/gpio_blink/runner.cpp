// gpio_blink example runner — loads blink firmware and runs it.
//
// Default: runs forever (no max-steps) since the firmware blinks in an
// infinite loop. Each PA5 edge is printed as it happens — that's the visible
// "blink" on a headless run. Stop with Ctrl+C.
#include "chips/stm32f1/soc/stm32f103_soc.hpp"
#include "cpu/cpu.hpp"
#include "sim/coordinator.hpp"

#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using namespace micro_forge;
using namespace micro_forge::chips::stm32f1;

static std::vector<uint8_t> read_file(const char* path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        return {};
    }
    return {std::istreambuf_iterator<char>(f), {}};
}

int main(int argc, char** argv) {
    const char* elf_path = (argc > 1) ? argv[1] : "blink.elf";
    auto data = read_file(elf_path);
    if (data.empty()) {
        fprintf(stderr, "Failed to read %s\n", elf_path);
        return 1;
    }

    auto soc = Stm32f103Soc::create();
    if (!soc) {
        fprintf(stderr, "Failed to create SoC: %s\n", soc.error().c_str());
        return 1;
    }

    // Each PA5 edge prints live — the visible blink on a headless run.
    int toggle_count = 0;
    (*soc)->parts().gpioa.set_pin_change_callback(
        [&](uint8_t pin, bool high) {
            if (pin != 5) {
                return;
            }
            ++toggle_count;
            printf("PA5 %s  (toggle #%d)\n",
                   high ? "HIGH \xE2\x97\x8F" : "LOW  \xC2\xB7",
                   toggle_count);
            fflush(stdout);
        });

    auto r = (*soc)->load_elf(data);
    if (!r) {
        fprintf(stderr, "Failed to load ELF: %s\n", r.error().c_str());
        return 1;
    }

    // Run forever; the firmware loops, the user stops with Ctrl+C. A fault
    // breaks out and reports the state + how far it got.
    fprintf(stderr, "blink running — Ctrl+C to stop\n");
    while (true) {
        const auto res = (*soc)->run(100000);
        if (res != sim::RunResult::Running) {
            const auto state = (*soc)->machine().cpu->state().value_or(
                cpu::CPU::State::Halted);
            fprintf(stderr, "stopped: state=%d (after %d toggles)\n",
                    static_cast<int>(state), toggle_count);
            break;
        }
    }
    return 0;
}
