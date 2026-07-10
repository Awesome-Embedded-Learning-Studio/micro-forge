// Load TAMCPP 3_uart_logger: verify USART1 TX prints the banner and an injected
// RX line ("LED ON\r\n") is parsed by the firmware into "OK: LED ON\r\n".
//
// TX chain == hal_uart E2E (send_string → HAL_UART_Transmit → DR → output).
// RX chain is the new path: inject_rx → RXNE IRQ → HAL_UART_RxCpltCallback →
// ring buffer → main pop → handle_command → send_string("OK: LED ON").
//
// Each byte is injected then run until PC returns to main's while loop before
// the next byte — this keeps the host-driven injection synchronous with one
// complete IRQ entry/handler/return round trip per byte.
#include "arch/arm/cortex_m3/cortex_m3.hpp"
#include "chips/stm32f1/periph/stm32f1_usart.hpp"
#include "chips/stm32f1/soc/stm32f103_soc.hpp"

#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

using namespace micro_forge;
using namespace micro_forge::cpu::arm::cortex_m3;
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
    const char* elf_path = (argc > 1) ? argv[1] : "tamcpp_uart.elf";
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

    std::string output;
    (*soc)->parts().serial().set_output(
        [&](uint8_t ch) { output += static_cast<char>(ch); });

    if (auto loaded = (*soc)->load_elf(data); !loaded) {
        std::fprintf(stderr, "ELF load failed: %s\n", loaded.error().c_str());
        return 1;
    }
    auto cm3 = (*soc)->cortex_m3_cpu();
    if (!cm3.IsValid()) {
        std::fprintf(stderr, "CPU not init\n");
        return 1;
    }

    // Run to the point main has printed the banner, armed RXNE reception, and
    // entered its while loop.
    (*soc)->run(2'000'000);

    // Inject "LED ON\r\n" one byte at a time. After each byte, run until PC is
    // back in main's while loop (0x0800'02B2–0x0800'0334) so the RXNE IRQ for
    // that byte is fully entered, handled, and returned before the next byte.
    auto& usart1 = static_cast<Stm32f1Usart&>((*soc)->parts().serial());
    const char cmd[] = "LED ON\r\n";
    for (char c : cmd) {
        usart1.inject_rx(static_cast<uint8_t>(c));
        for (int i = 0; i < 200'000; ++i) {
            (*soc)->run(1'000);
            const uint32_t pc = cm3->pc().value_or(0);
            if (pc >= 0x0800'02B2u && pc <= 0x0800'0334u) {
                break;
            }
        }
    }
    (*soc)->run(5'000'000);

    const bool tx_ok = output.find("UART Logger Ready!") != std::string::npos;
    const bool rx_ok = output.find("OK: LED ON") != std::string::npos;

    std::fprintf(stderr, "UART output(%zu bytes): ", output.size());
    for (unsigned char c : output) {
        std::fprintf(stderr, "%02X ", c);
    }
    std::fprintf(stderr, "\n[TAMCPP-uart] TX banner=%d  RX cmd=%d\n",
                 tx_ok ? 1 : 0, rx_ok ? 1 : 0);
    return (tx_ok && rx_ok) ? 0 : 2;
}
