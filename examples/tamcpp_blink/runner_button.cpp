// Load TAMCPP 2_button_control, drive its active-low PA0 button, and verify
// that the active-low PC13 LED receives both the Pressed and Released writes.
#include "arch/arm/cortex_m3/cortex_m3.hpp"
#include "chips/stm32f1/soc/stm32f103_soc.hpp"
#include "tools/mmio_trace.hpp"

#include <cstdio>
#include <fstream>
#include <vector>

using namespace micro_forge;
using namespace micro_forge::chips::stm32f1;

namespace {

std::vector<uint8_t> read_file(const char* path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        return {};
    }
    return {std::istreambuf_iterator<char>(f), {}};
}

} // namespace

int main(int argc, char** argv) {
    const char* elf_path = (argc > 1) ? argv[1] : "tamcpp_button.elf";
    auto data = read_file(elf_path);
    if (data.empty()) {
        std::fprintf(stderr, "Failed to read %s\n", elf_path);
        return 1;
    }

    auto soc = Stm32f103Soc::create();
    if (!soc) {
        std::fprintf(stderr, "SoC create failed: %s\n", soc.error().c_str());
        return 1;
    }
    if (auto loaded = (*soc)->load_elf(data); !loaded) {
        std::fprintf(stderr, "ELF load failed: %s\n", loaded.error().c_str());
        return 1;
    }

    int led_on_writes = 0;
    int led_off_writes = 0;
    tools::enable_mmio_trace(
        *(*soc)->machine().bus, [&](const tools::MmioAccess& access) {
            if (!access.is_write || !access.ok || access.addr != 0x4001'1010u ||
                access.width != Width::Word) {
                return;
            }
            if (access.value & (1u << (13 + 16))) {
                ++led_on_writes; // active-low PC13: BSRR reset half
            }
            if (access.value & (1u << 13)) {
                ++led_off_writes; // active-low PC13: BSRR set half
            }
        });

    // The GPIO model has no floating-pad/pull resistor model yet, so inject
    // the pull-up level before firmware reaches Button's BootSync state.
    (*soc)->parts().gpioa.simulate_input(0, true);
    (*soc)->run(10'000'000);

    (*soc)->parts().gpioa.simulate_input(0, false); // press
    (*soc)->run(4'000'000);                         // >20 ms debounce

    (*soc)->parts().gpioa.simulate_input(0, true); // release
    (*soc)->run(4'000'000);                        // >20 ms debounce

    auto pa0 = (*soc)->parts().gpioa.read(0x08, Width::Word);
    auto pc13 = (*soc)->parts().gpioc.read(0x0C, Width::Word);
    auto cpu = (*soc)->cortex_m3_cpu();
    auto state = cpu.IsValid()
                     ? cpu->state()
                     : cpu::CPU::CPUExpected<cpu::CPU::State>{
                           std::unexpected{cpu::CPU::CPUError::NotRunning}};

    const bool passed = pa0 && ((*pa0 & 1u) != 0) && pc13 &&
                        ((*pc13 & (1u << 13)) != 0) && led_on_writes > 0 &&
                        led_off_writes > 0 && state &&
                        *state == cpu::CPU::State::Running;

    std::printf("TAMCPP button: on=%d off=%d PA0=%u PC13=%u state=%d [%s]\n",
                led_on_writes, led_off_writes, pa0 ? (*pa0 & 1u) : 0u,
                pc13 ? ((*pc13 >> 13) & 1u) : 0u,
                state ? static_cast<int>(*state) : -1,
                passed ? "PASS" : "FAIL");
    return passed ? 0 : 2;
}
