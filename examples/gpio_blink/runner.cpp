#include "chips/stm32f1/soc/stm32f103_soc.hpp"

#include <cstdio>
#include <fstream>
#include <iostream>
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

    int toggle_count = 0;
    (*soc)->parts().gpioa.set_pin_change_callback(
        [&](uint8_t pin, bool /*high*/) {
            if (pin == 5) {
                toggle_count++;
            }
        });

    auto r = (*soc)->load_elf(data);
    if (!r) {
        fprintf(stderr, "Failed to load ELF: %s\n", r.error().c_str());
        return 1;
    }

    (*soc)->run(200000);

    printf("GPIO PA5 toggled %d times\n", toggle_count);
    return toggle_count >= 6 ? 0 : 1; // 3 on + 3 off = 6
}
