#pragma once

#include "chips/machine.hpp"
#include "chips/stm32f1/clock_domains.hpp"
#include "chips/stm32f1/stm32f1_afio.hpp"
#include "chips/stm32f1/stm32f1_exti.hpp"
#include "chips/stm32f1/stm32f1_flash.hpp"
#include "chips/stm32f1/stm32f1_gpio.hpp"
#include "chips/stm32f1/stm32f1_rcc.hpp"
#include "chips/stm32f1/stm32f1_timer.hpp"
#include "chips/stm32f1/stm32f1_usart.hpp"
#include "memory/flat_memory.hpp"
#include "periph/clock_controller.hpp"
#include "periph/gpio.hpp"
#include "periph/nvic.hpp"
#include "periph/scb.hpp"
#include "periph/serial_port.hpp"
#include "periph/systick.hpp"
#include "periph/timer.hpp"
#include "util/weak_ptr/weak_ptr.h"

#include <expected>
#include <memory>
#include <span>
#include <string>

namespace micro_forge::cpu::arm::cortex_m3 {
class CortexM3CPU;
}

namespace micro_forge::chips::stm32f1 {

struct Stm32f103Parts {
    memory::FlatMemory flash{128 * 1024};
    memory::FlatMemory sram{20 * 1024};

    periph::NvicPeripheral nvic;
    periph::SysTickPeripheral systick;
    periph::ScbPeripheral scb;

    Stm32f1Rcc rcc;
    Stm32f1Afio afio;
    Stm32f1Flash flash_if;
    Stm32f1Gpio gpioa{'A'};
    Stm32f1Gpio gpiob{'B'};
    Stm32f1Gpio gpioc{'C'};
    Stm32f1Usart usart1;
    Stm32f1Timer tim2;
    Stm32f1Exti exti;

    Stm32f103Parts() = default;

    periph::Gpio& gpio(char id);
    periph::SerialPort& serial() { return usart1; }
    periph::Timer& timer() { return tim2; }
    periph::ClockController& clocks() { return rcc; }
};

class Stm32f103Soc {
  public:
    static std::expected<std::unique_ptr<Stm32f103Soc>, std::string> create();

    chips::Machine& machine() { return machine_; }
    Stm32f103Parts& parts() { return parts_; }
    WeakPtr<micro_forge::cpu::arm::cortex_m3::CortexM3CPU> cortex_m3_cpu() {
        return cortex_m3_;
    }

    std::expected<void, std::string> load_elf(std::span<const uint8_t> data);
    std::expected<void, std::string> load_bin(uint32_t base,
                                              std::span<const uint8_t> data);
    micro_forge::sim::RunResult run(size_t max_steps = SIZE_MAX);

    Stm32f103Soc(const Stm32f103Soc&) = delete;
    Stm32f103Soc& operator=(const Stm32f103Soc&) = delete;
    Stm32f103Soc(Stm32f103Soc&&) = delete;
    Stm32f103Soc& operator=(Stm32f103Soc&&) = delete;

  private:
    Stm32f103Soc() = default;

    chips::Machine machine_;
    Stm32f103Parts parts_;
    WeakPtr<micro_forge::cpu::arm::cortex_m3::CortexM3CPU> cortex_m3_;
};

} // namespace micro_forge::chips::stm32f1
