// wfi_blink runner — loads the WFI blink firmware and counts PC13 toggles.
// P2.a fast-forward (MF_FAST_FORWARD env): with WFI sleep, the coordinator
// skips straight to each SysTick IRQ, so 8M steps cover many blink cycles.
// Without it, WFI busy-steps the 8000-cycle SysTick period → few toggles.
#include "arch/arm/cortex_m3/cortex_m3.hpp"
#include "chips/stm32f1/soc/stm32f103_soc.hpp"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

using namespace micro_forge;
using namespace micro_forge::cpu::arm::cortex_m3;
using namespace micro_forge::chips::stm32f1;

static std::vector<uint8_t> read_file(const char* path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        return {};
    }
    return {std::istreambuf_iterator<char>(f), {}};
}

int main(int argc, char** argv) {
    const char* elf_path = (argc > 1) ? argv[1] : "wfi_blink.elf";
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

    int toggle_count = 0;
    (*soc)->parts().gpioc.set_pin_change_callback([&](uint8_t pin, bool) {
        if (pin == 13) {
            toggle_count++;
        }
    });

    auto r = (*soc)->load_elf(data);
    if (!r) {
        fprintf(stderr, "Failed to load ELF: %s\n", r.error().c_str());
        return 1;
    }

    auto cm3 = (*soc)->cortex_m3_cpu();
    if (!cm3.IsValid()) {
        fprintf(stderr, "Cortex-M3 CPU not initialized\n");
        return 1;
    }

    if (const char* ff = std::getenv("MF_FAST_FORWARD")) {
        std::string s(ff);
        bool on = s == "1" || s == "on" || s == "true";
        (*soc)->set_fast_forward_enabled(on);
        fprintf(stderr, "[WFI_BLINK] fast_forward=%s\n", on ? "ON" : "OFF");
    }

    (*soc)->run(8000000);

    const auto& missing = cm3->missing_opcodes();
    if (!missing.empty()) {
        fprintf(stderr, "=== Missing instructions: %zu ===\n", missing.size());
        for (auto& [addr, hw1, hw2] : missing) {
            if (hw2) {
                fprintf(stderr, "  PC=0x%08X  hw1=0x%04X hw2=0x%04X\n", addr,
                        hw1, hw2);
            } else {
                fprintf(stderr, "  PC=0x%08X  hw1=0x%04X\n", addr, hw1);
            }
        }
    }

    auto state_res = cm3->state();
    bool faulted = state_res && *state_res == cpu::CPU::State::Faulted;
    if (faulted) {
        auto pc_val = cm3->pc();
        fprintf(stderr, "[WFI_BLINK] CPU faulted at PC=0x%08X\n",
                pc_val.has_value() ? *pc_val : 0);
    }

    printf("GPIO PC13 toggled %d times\n", toggle_count);
    return toggle_count > 0 ? 0 : 2;
}
