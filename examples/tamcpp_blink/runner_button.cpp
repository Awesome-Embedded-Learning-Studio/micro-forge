// A2 spike: 加载 TAMCPP 2_button_control,注入 PA0 按钮输入,看 PC13 toggle。
// Button<PA0,PullUp,Low>:轮询去抖(20ms 状态机),Pressed→led.on(),Released→led.off()。
// 模拟器 Stm32f1Gpio::configure_pin 忽略 pull 参数,idr_ 不反映 PullUp(初值 0)→
// Button 误判"上电即按下"→ boot lock。runner 注入 simulate_input(0,true) 模拟上拉
// 空闲态让 Button 进 Idle,再注入按下/释放。
#include "arch/arm/cortex_m3/cortex_m3.hpp"
#include "chips/stm32f1/soc/stm32f103_soc.hpp"
#include "tools/mmio_trace.hpp"

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
    const char* elf_path = (argc > 1) ? argv[1] : "tamcpp_button.elf";
    auto data = read_file(elf_path);
    if (data.empty()) {
        fprintf(stderr, "Failed to read %s\n", elf_path);
        return 1;
    }

    auto soc = Stm32f103Soc::create();
    if (!soc) {
        fprintf(stderr, "SoC create failed: %s\n", soc.error().c_str());
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
        fprintf(stderr, "ELF load failed: %s\n", r.error().c_str());
        return 1;
    }
    auto cm3 = (*soc)->cortex_m3_cpu();
    if (!cm3.IsValid()) {
        fprintf(stderr, "CPU not init\n");
        return 1;
    }

    auto dump = [&](const char* tag) {
        auto idr = (*soc)->parts().gpioa.read(0x08, Width::Word);
        auto odr = (*soc)->parts().gpioc.read(0x0C, Width::Word);
        auto tck = (*soc)->parts().sram.read(0x10, Width::Word);
        auto scc = (*soc)->parts().sram.read(0x08, Width::Word);
        auto pc = cm3->pc();
        fprintf(stderr, "[%s] PC=0x%X SCC=0x%X uwTick=0x%X PA0=0x%X PC13=0x%X",
                tag, pc.has_value() ? *pc : 0xDEAD,
                scc.has_value() ? *scc : 0xDEAD, tck.has_value() ? *tck : 0xDEAD,
                idr.has_value() ? *idr : 0xDEAD, odr.has_value() ? *odr : 0xDEAD);
        if (cm3->last_fault()) {
            auto& f = *cm3->last_fault();
            fprintf(stderr, " FAULT pc=0x%X addr=0x%X kind=%d", f.pc,
                    f.access_addr.has_value() ? *f.access_addr : 0xDEAD,
                    static_cast<int>(f.kind));
        }
        fprintf(stderr, "\n");
    };
    tools::enable_mmio_trace(*(*soc)->machine().bus, [](const tools::MmioAccess& a) {
        if (a.addr == 0x40010808U) {
            fprintf(stderr, "[mmio] %c IDR=0x%X\n",
                    a.is_write ? 'W' : 'R', a.value);
        }
    });

    // 1) 上拉空闲
    (*soc)->parts().gpioa.simulate_input(0, true);
    (*soc)->run(10000000);
    dump("after idle");
    // 2) 按下
    (*soc)->parts().gpioa.simulate_input(0, false);
    (*soc)->run(10000000);
    dump("after press");
    // 3) 释放
    (*soc)->parts().gpioa.simulate_input(0, true);
    (*soc)->run(30000000);
    dump("after release");

    auto pc_val = cm3->pc();
    auto state_res = cm3->state();
    fprintf(stderr, "[TAMCPP-button] PC=0x%08X state=%d\n",
            pc_val.has_value() ? *pc_val : 0,
            state_res ? static_cast<int>(*state_res) : -1);

    // 诊断:RCC CR(ready 位)+ SysTick + uwTick,看为什么卡 OscConfig 轮询
    auto rcc_cr = (*soc)->parts().rcc.read(0x00, Width::Word);
    auto& systick = (*soc)->parts().systick;
    auto uwTick = (*soc)->parts().sram.read(0x10, Width::Word);
    auto pa0_idr = (*soc)->parts().gpioa.read(0x08, Width::Word);
    auto pc13_odr = (*soc)->parts().gpioc.read(0x0C, Width::Word);
    fprintf(stderr,
            "[diag] RCC_CR=0x%X SysTick.ctrl=0x%X load=%u uwTick=0x%X "
            "PA0_IDR=0x%X PC13_ODR=0x%X\n",
            rcc_cr.has_value() ? *rcc_cr : 0xDEAD, systick.ctrl(),
            systick.load(), uwTick.has_value() ? *uwTick : 0xDEAD,
            pa0_idr.has_value() ? *pa0_idr : 0xDEAD,
            pc13_odr.has_value() ? *pc13_odr : 0xDEAD);

    const auto& missing = cm3->missing_opcodes();
    if (!missing.empty()) {
        fprintf(stderr, "=== Missing instructions: %zu ===\n", missing.size());
        for (auto& [addr, hw1, hw2] : missing) {
            if (hw2) {
                fprintf(stderr, "  PC=0x%08X hw1=0x%04X hw2=0x%04X\n", addr, hw1, hw2);
            } else {
                fprintf(stderr, "  PC=0x%08X hw1=0x%04X\n", addr, hw1);
            }
        }
    }

    printf("GPIO PC13 toggled %d times\n", toggle_count);
    return toggle_count > 0 ? 0 : 2;
}
