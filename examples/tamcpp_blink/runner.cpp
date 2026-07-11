// A1 spike: 加载 TAMCPP 1_led_control 的 C++ elf,看 GPIO PC13 toggle count>0。
// 验证 C++ HAL 样例(__libc_init_array + HAL_Delay/SysTick 中断链路)能在模拟器跑。
// 模板抄自 examples/hal_blink/runner.cpp,改:监听 gpioc pin13(TAMCPP 板载 LED 在 PC13,
// ActiveLevel::Low)。elf 由 /tmp/tamcpp-build/build.sh 编出(借 STM32CubeF1 HAL)。
#include "arch/arm/cortex_m3/cortex_m3.hpp"
#include "chips/stm32f1/soc/stm32f103_soc.hpp"

#include <cstdio>
#include <fstream>
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
    const char* elf_path = (argc > 1) ? argv[1] : "tamcpp_led.elf";
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

    // TAMCPP 1_led_control: LED<PortC, PIN_13, ActiveLevel::Low>。
    // HAL_Delay(500) 交替 on/off,on() 写 RESET(低有效亮),off() 写 SET。
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

    // 80M step ≈ 80M cycle → SysTick(load=63999)约 1250 次中断 → uwTick≈1250,
    // 够第一个 HAL_Delay(500) 返回 + led.on() + 第二个 HAL_Delay(500) + led.off(),
    // 即至少 1 次 PC13 翻转。
    (*soc)->run(80000000);

    auto pc_val = cm3->pc();
    auto state_res = cm3->state();
    fprintf(stderr, "[TAMCPP] PC=0x%08X state=%d\n",
            pc_val.has_value() ? *pc_val : 0,
            state_res ? static_cast<int>(*state_res) : -1);

    const auto& missing = cm3->missing_opcodes();
    if (!missing.empty()) {
        fprintf(stderr, "=== Missing instructions: %zu ===\n",
                missing.size());
        for (auto& [addr, hw1, hw2] : missing) {
            if (hw2) {
                fprintf(stderr, "  PC=0x%08X  hw1=0x%04X hw2=0x%04X\n", addr,
                        hw1, hw2);
            } else {
                fprintf(stderr, "  PC=0x%08X  hw1=0x%04X\n", addr, hw1);
            }
        }
    }

    printf("GPIO PC13 toggled %d times\n", toggle_count);
    return toggle_count > 0 ? 0 : 2;
}
