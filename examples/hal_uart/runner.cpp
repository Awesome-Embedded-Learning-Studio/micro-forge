#include "chips/stm32f1/soc/stm32f103_soc.hpp"

#include <fstream>
#include <iostream>
#include <string>
#include <vector>

using namespace micro_forge;
using namespace micro_forge::chips::stm32f1;

static std::vector<uint8_t> read_file(const char* path) {
    std::ifstream f(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(f), {}};
}

int main() {
    const char* elf = "firmware/hal_uart.elf";
    auto data = read_file(elf);
    if (data.empty()) {
        std::cerr << "Firmware not found: " << elf << "\n";
        return 1;
    }

    auto soc = Stm32f103Soc::create();
    if (!soc) {
        std::cerr << "Failed to create SoC: " << soc.error() << "\n";
        return 1;
    }

    std::string output;
    (*soc)->parts().serial().set_output(
        [&](uint8_t ch) { output += static_cast<char>(ch); });

    auto r = (*soc)->load_elf(data);
    if (!r) {
        std::cerr << "load_elf failed: " << r.error() << "\n";
        return 1;
    }

    (*soc)->run(200000);

    auto state = (*soc)->machine().cpu->state();
    if (state && *state == cpu::CPU::State::Faulted) {
        std::cerr << "CPU faulted!\n";
        return 1;
    }

    std::cout << output << std::flush;
    return 0;
}
