// A2 spike: 加载 TAMCPP 3_uart_logger,看 USART1 TX 输出 + 注入 RX 命令。
// main: send_string("UART Logger Ready!\r\n") + RXNE 中断收命令 → handle_command
// ("LED ON"→回 "OK: LED ON\r\n" + led.on())。TX 链路与 hal_uart E2E 同;RX 中断是新链路。
#include "arch/arm/cortex_m3/cortex_m3.hpp"
#include "chips/stm32f1/periph/stm32f1_usart.hpp"
#include "chips/stm32f1/soc/stm32f103_soc.hpp"
#include "tools/mmio_trace.hpp"

#include <cstdio>
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
    const char* elf_path = (argc > 1) ? argv[1] : "tamcpp_uart.elf";
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

    std::string output;
    (*soc)->parts().serial().set_output(
        [&](uint8_t ch) { output += static_cast<char>(ch); });

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

    // 跑到 main 发 "UART Logger Ready!" + uart_start_receive + 进 while
    (*soc)->run(2000000);

    // 诊断:inject 前 huart_ Instance + flash 向量表 + 0x1674 ldr r0 的 literal
    {
        auto inst_pre = (*soc)->parts().sram.read(0x14, Width::Word);
        auto vec_usart1 = (*soc)->parts().flash.read(0xD4, Width::Word);
        auto ldr_r0_lit = (*soc)->parts().flash.read(0x167c, Width::Word);
        fprintf(stderr,
                "[diag] pre-inject [0x20000014]=0x%X vec[USART1@0xD4]=0x%X "
                "lit@0x0800167c=0x%X\n",
                inst_pre.has_value() ? *inst_pre : 0xDEAD,
                vec_usart1.has_value() ? *vec_usart1 : 0xDEAD,
                ldr_r0_lit.has_value() ? *ldr_r0_lit : 0xDEAD);
    }

    // mmio_trace:看 CPU 进 USART1 handler 时读哪些地址(0x0800167c=ldr r0 lit,
    // 0x20000014=huart, 0x40010800=GPIOA, 0x40013800=USART1 SR)
    tools::enable_mmio_trace(*(*soc)->machine().bus, [](const tools::MmioAccess& a) {
        if (a.addr == 0x0800167cU || a.addr == 0x20000014U ||
            a.addr == 0x40010800U || a.addr == 0x40013800U ||
            a.addr == 0x44444444U || a.addr == 0x080000D4U ||
            a.addr == 0x40013804U) {
            fprintf(stderr, "[mmio] %c 0x%08X = 0x%X\n",
                    a.is_write ? 'W' : 'R', a.addr, a.value);
        }
    });

    // 注入 RX 命令 "LED ON\r\n",逐字节 + run 让 RXNE 中断 → main pop → handle_command
    auto& usart1 = static_cast<Stm32f1Usart&>((*soc)->parts().serial());
    const char cmd[] = "LED ON\r\n";
    for (char c : cmd) {
        usart1.inject_rx(static_cast<uint8_t>(c));
        (*soc)->run(2000000);
    }
    (*soc)->run(5000000);

    auto pc_val = cm3->pc();
    auto state_res = cm3->state();
    fprintf(stderr, "[TAMCPP-uart] PC=0x%08X state=%d\n",
            pc_val.has_value() ? *pc_val : 0,
            state_res ? static_cast<int>(*state_res) : -1);

    // 诊断:huart_.Instance@0x20000014 + USART1 CR1 + fault 详情
    auto inst = (*soc)->parts().sram.read(0x14, Width::Word);
    auto cr1 = usart1.read(0x0C, Width::Word);
    fprintf(stderr, "[diag] huart_.Instance=0x%X USART1.CR1=0x%X\n",
            inst.has_value() ? *inst : 0xDEAD, cr1.has_value() ? *cr1 : 0xDEAD);
    if (cm3->last_fault()) {
        auto& f = *cm3->last_fault();
        fprintf(stderr,
                "[fault] access_addr=0x%X bus_error=%d width=%d pc=0x%X "
                "sp=0x%X lr=0x%X\n",
                f.access_addr.has_value() ? *f.access_addr : 0xDEAD,
                f.bus_error.has_value() ? static_cast<int>(*f.bus_error) : -1,
                f.access_width.has_value() ? static_cast<int>(*f.access_width) : -1,
                f.pc, f.sp, f.lr);
    }

    const auto& missing = cm3->missing_opcodes();
    if (!missing.empty()) {
        fprintf(stderr, "=== Missing instructions: %zu ===\n", missing.size());
    }

    fprintf(stderr, "UART output(%zu bytes, hex): ", output.size());
    for (unsigned char c : output) {
        fprintf(stderr, "%02X ", c);
    }
    fprintf(stderr, "\n");
    printf("GPIO PC13 toggled %d times\n", toggle_count);
    bool tx_ok = output.find("UART Logger Ready!") != std::string::npos;
    bool rx_ok = output.find("OK: LED ON") != std::string::npos;
    fprintf(stderr, "[TAMCPP-uart] TX Ready=%d  RX cmd=%d\n", tx_ok ? 1 : 0,
            rx_ok ? 1 : 0);
    return tx_ok ? 0 : 2;
}
