#include "chips/stm32f1/stm32f103_soc.hpp"
#include "arch/arm/cortex_m3/cortex_m3.hpp"
#include "arch/arm/cortex_m3/cortex_m3_reset.hpp"
#include "hooks/events.hpp"
#include "chips/stm32f1/interrupt_config.hpp"
#include "chips/stm32f1/memory_bus.hpp"
#include "chips/stm32f1/peripheral_config.hpp"

namespace micro_forge::chips::stm32f1 {

periph::Gpio& Stm32f103Parts::gpio(char id) {
    switch (id) {
        case 'A':
            return gpioa;
        case 'B':
            return gpiob;
        case 'C':
            return gpioc;
        default:
            return gpioa;
    }
}

std::expected<std::unique_ptr<Stm32f103Soc>, std::string>
Stm32f103Soc::create() {
    auto soc = std::unique_ptr<Stm32f103Soc>(new Stm32f103Soc());
    auto& m = soc->machine_;
    auto& p = soc->parts_;

    // Bus
    m.bus = std::make_unique<memory::Bus>();

    // Memory: Flash + SRAM
    auto r = configure_memory(*m.bus, p.flash, p.sram);
    if (!r) {
        return std::unexpected("failed to configure memory");
    }

    // Interrupt devices: NVIC + SysTick
    r = configure_interrupt_devices(*m.bus, p.nvic, p.systick, p.scb);
    if (!r) {
        return std::unexpected("failed to configure interrupt devices");
    }

    // Peripherals: RCC + GPIO + USART + Timer
    r = configure_peripherals(*m.bus, p);
    if (!r) {
        return std::unexpected("failed to configure peripherals");
    }

    // CPU
    auto cm3 = std::make_unique<cpu::arm::cortex_m3::CortexM3CPU>(
        m.bus->GetWeak());
    auto cm3_weak = cm3->GetWeak();
    auto* cm3_ptr = cm3.get();
    m.cpu = std::move(cm3);
    soc->cortex_m3_ = cm3_weak;
    cm3_ptr->set_nvic(p.nvic);
    cm3_ptr->set_scb(p.scb);

    // Wire SysTick → CPU system exception (bypasses NVIC)
    p.systick.set_irq_callback([cm3_weak]() {
        if (cm3_weak.IsValid()) {
            cm3_weak->sys_tick_irq();
        }
    });

    // Wire Timer UIF (edge, DIER.UIE) → NVIC TIM2 line (IRQ 28).
    p.tim2.set_irq_callback([cm3_weak]() {
        if (cm3_weak.IsValid()) {
            (void)cm3_weak->raise_irq(kTim2Irqn);
        }
    });

    // Wire EXTI: GPIO edges → EXTI (EXTICR routing) → NVIC.
    p.exti.set_afio(p.afio);
    p.exti.set_irq_callback([cm3_weak](intr::intr_n_t irq) {
        if (cm3_weak.IsValid()) {
            (void)cm3_weak->raise_irq(irq);
        }
    });
    auto exti_edge = [exti_weak = p.exti.GetWeak()](const hooks::GpioEdge& e) {
        if (exti_weak.IsValid()) {
            exti_weak->on_gpio_edge(e);
        }
    };
    p.gpioa.edge_signal().connect(exti_edge);
    p.gpiob.edge_signal().connect(exti_edge);
    p.gpioc.edge_signal().connect(exti_edge);

    // Wire SCB VTOR write → CPU vector_table_base_ update
    p.scb.set_vtor_callback([cm3_weak](uint32_t vtor) {
        if (cm3_weak.IsValid()) {
            cm3_weak->set_vector_table_base(vtor);
        }
    });

    // Wire SCB AIRCR.PRIGROUP write → CPU priority-grouping update
    p.scb.set_prigroup_callback([cm3_weak](uint8_t group) {
        if (cm3_weak.IsValid()) {
            cm3_weak->set_prigroup(group);
        }
    });

    // Stamp GPIO edge events with the CPU cycle counter.
    auto cycle_src = [cm3_weak]() -> uint64_t {
        if (!cm3_weak.IsValid()) {
            return 0;
        }
        return cm3_weak->cycles().value_or(0);
    };
    p.gpioa.set_cycle_source(cycle_src);
    p.gpiob.set_cycle_source(cycle_src);
    p.gpioc.set_cycle_source(cycle_src);

    // SimulationCoordinator
    auto clock = sim::VirtualClock(std::span<const sim::DomainConfig>(
        stm32f103_default_clocks, kClockDomainCount));
    m.coord = std::make_unique<sim::SimulationCoordinator>(std::move(clock));
    m.coord->set_cpu(cm3_weak);
    m.coord->add_tickable(p.systick.GetWeak(),
                          domain_index(ClockDomain::Sysclk));
    m.coord->add_tickable(p.tim2.GetWeak(), domain_index(ClockDomain::Apb1));

    return soc;
}

std::expected<void, std::string>
Stm32f103Soc::load_elf(std::span<const uint8_t> data) {
    auto r = machine_.load_elf(data);
    if (!r) {
        return r;
    }

    if (!cortex_m3_.IsValid()) {
        return std::unexpected("Cortex-M3 CPU not initialized");
    }

    auto reset_r =
        cpu::arm::cortex_m3::cortex_m3_reset(*cortex_m3_, *machine_.bus);
    if (!reset_r) {
        return reset_r;
    }

    cortex_m3_->launch();
    return {};
}

std::expected<void, std::string>
Stm32f103Soc::load_bin(uint32_t base, std::span<const uint8_t> data) {
    auto r = machine_.load_bin(base, data);
    if (!r) {
        return r;
    }

    if (!cortex_m3_.IsValid()) {
        return std::unexpected("Cortex-M3 CPU not initialized");
    }

    auto reset_r =
        cpu::arm::cortex_m3::cortex_m3_reset(*cortex_m3_, *machine_.bus);
    if (!reset_r) {
        return reset_r;
    }

    cortex_m3_->launch();
    return {};
}

micro_forge::sim::RunResult Stm32f103Soc::run(size_t max_steps) {
    return machine_.run(max_steps);
}

} // namespace micro_forge::chips::stm32f1
