/**
 * @file stm32f103.cpp
 * @brief STM32F103 assembly, reset protocol and deterministic execution loop.
 * @version 0.1.0
 */
#include "micro_forge/machines/stm32f103.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <expected>
#include <memory>
#include <optional>
#include <span>
#include <tuple>
#include <utility>

#include "micro_forge/arch/arm/cortex_m3/cortex_m3_reset.hpp"
#include "micro_forge/arch/base/cpu.hpp"
#include "micro_forge/base/run_result.hpp"
#include "micro_forge/device/base/device.hpp"
#include "micro_forge/device/base/hooks/events.hpp"
#include "micro_forge/device/base/types.hpp"
#include "micro_forge/kernel/bus/bus_error.hpp"
#include "stm32f103_internal.hpp"

namespace micro_forge::machines {
namespace {

void require_mapping(std::expected<void, kernel::BusError> result) {
    // Every mapping below is a fixed, checked machine invariant, not user input.
    if (!result) {
        std::abort();
    }
}

uint64_t divided_ticks(uint64_t elapsed, uint64_t divisor, uint64_t& remainder,
                       uint64_t& old_divisor) {
    if (old_divisor != divisor) {
        remainder = 0;
        old_divisor = divisor;
    }
    const uint64_t total = remainder + elapsed;
    remainder = total % divisor;
    return total / divisor;
}

} // namespace

Stm32F103Machine::Impl::Impl() {
    std::ranges::fill(std::span(flash.data(), kFlashSize), uint8_t{0xFF});
    require_mapping(bus.map(0, kFlashSize, flash));
    require_mapping(bus.map(kFlashBase, kFlashSize, flash));
    require_mapping(bus.map_ram(kSramBase, kSramSize, sram));
    require_mapping(bus.map(0x2200'0000, 0x0200'0000, sram_bitband));
    require_mapping(bus.map(0x4200'0000, 0x0200'0000, peripheral_bitband));
    struct Window {
        uint64_t base;
        uint64_t size;
    };
    constexpr std::array windows = {
        Window{.base = 0xE000'E010, .size = 0x10},  Window{.base = 0xE000'E100, .size = 0xC00},
        Window{.base = 0xE000'ED00, .size = 0x40},  Window{.base = 0x4002'1000, .size = 0x400},
        Window{.base = 0x4001'0000, .size = 0x400}, Window{.base = 0x4001'0400, .size = 0x400},
        Window{.base = 0x4002'2000, .size = 0x400}, Window{.base = 0x4001'0800, .size = 0x400},
        Window{.base = 0x4001'0C00, .size = 0x400}, Window{.base = 0x4001'1000, .size = 0x400},
        Window{.base = 0x4001'3800, .size = 0x400}, Window{.base = 0x4000'4400, .size = 0x400},
        Window{.base = 0x4000'0000, .size = 0x400}, Window{.base = 0x4000'0400, .size = 0x400},
    };
    static_assert(windows.size() == 14);
    for (std::size_t i = 0; i < windows.size(); ++i) {
        require_mapping(bus.map(windows[i].base, windows[i].size, regions[i]));
    }
    cpu.set_nvic(nvic);
    cpu.set_scb(scb);
    cpu.set_breakpoint_handler([this](uint8_t immediate) { return handle_breakpoint(immediate); });
    systick.set_irq_callback([this] { cpu.sys_tick_irq(); });
    tim2.set_irq_callback([this] { std::ignore = cpu.raise_irq(28); });
    tim3.set_irq_callback([this] { std::ignore = cpu.raise_irq(29); });
    usart1.set_irq_callback([this] { std::ignore = cpu.raise_irq(37); });
    usart2.set_irq_callback([this] { std::ignore = cpu.raise_irq(38); });
    exti.set_afio(afio);
    exti.set_irq_callback([this](uint32_t irq) { std::ignore = cpu.raise_irq(irq); });
    const auto edge = [this](const device::hooks::GpioEdge& event) {
        exti.on_gpio_edge(event);
        events.gpio.emit(event);
    };
    gpioa.edge_signal().connect(edge);
    gpiob.edge_signal().connect(edge);
    gpioc.edge_signal().connect(edge);
    const auto clock = [this] { return cpu.cycles().value_or(0); };
    gpioa.set_cycle_source(clock);
    gpiob.set_cycle_source(clock);
    gpioc.set_cycle_source(clock);
    events.set_cycle_source(clock);
    const auto transmit = [this](uint8_t unit, uint8_t byte) {
        console.push_back(static_cast<char>(byte));
        device::hooks::UartByte event;
        event.cycle = events.now();
        event.unit = unit;
        event.byte = byte;
        events.uart.emit(event);
    };
    usart1.set_output([transmit](uint8_t byte) { transmit(1, byte); });
    usart2.set_output([transmit](uint8_t byte) { transmit(2, byte); });
    scb.set_vtor_callback([this](uint32_t base) { cpu.set_vector_table_base(base); });
    scb.set_prigroup_callback([this](uint8_t group) { cpu.set_prigroup(group); });
}

std::optional<std::span<uint8_t>> Stm32F103Machine::Impl::image_window(uint64_t address,
                                                                       uint64_t size) {
    const auto window = [address, size](uint64_t base, uint64_t extent,
                                        uint8_t* data) -> std::optional<std::span<uint8_t>> {
        if (address < base || address - base > extent || size > extent - (address - base)) {
            return std::nullopt;
        }
        return std::span(data + (address - base), static_cast<std::size_t>(size));
    };
    if (auto bytes = window(0, kFlashSize, flash.data())) {
        return bytes;
    }
    if (auto bytes = window(kFlashBase, kFlashSize, flash.data())) {
        return bytes;
    }
    return window(kSramBase, kSramSize, sram.data());
}

void Stm32F103Machine::Impl::reset_devices() {
    const std::array<device::Device*, 14> devices = {
        &systick, &nvic,  &scb,   &rcc,    &afio,   &exti, &flash_interface,
        &gpioa,   &gpiob, &gpioc, &usart1, &usart2, &tim2, &tim3,
    };
    for (auto* device : devices) {
        device->reset();
    }
    scheduler = kernel::Scheduler{};
    systick_remainder = 0;
    tim2_remainder = 0;
    tim3_remainder = 0;
    systick_divisor = 1;
    tim2_divisor = 1;
    tim3_divisor = 1;
    console.clear();
}

void Stm32F103Machine::Impl::tick_devices(uint64_t elapsed) {
    // CPU clocks are HCLK ticks, not a claimed cycle-accurate wall clock.
    // SysTick's external reference is HCLK/8; APB1 timers run at 2*PCLK1
    // when the APB prescaler is not one. Clock changes reset fractional phase.
    const uint64_t st_divisor = (systick.ctrl() & 4U) != 0 ? 1 : 8;
    const auto st_ticks = divided_ticks(elapsed, st_divisor, systick_remainder, systick_divisor);
    if (st_ticks != 0) {
        systick.tick(st_ticks);
    }
    const auto cfgr = rcc.read(4, device::Width::kWord);
    constexpr std::array<uint64_t, 8> timer_divisors = {1, 1, 1, 1, 1, 2, 4, 8};
    const auto timer_divisor = timer_divisors[(cfgr.value_or(0) >> 8U) & 7U];
    if (rcc.is_clock_enabled(0x4000'0000)) {
        const auto ticks = divided_ticks(elapsed, timer_divisor, tim2_remainder, tim2_divisor);
        if (ticks != 0) {
            tim2.tick(ticks);
        }
    }
    if (rcc.is_clock_enabled(0x4000'0400)) {
        const auto ticks = divided_ticks(elapsed, timer_divisor, tim3_remainder, tim3_divisor);
        if (ticks != 0) {
            tim3.tick(ticks);
        }
    }
    scheduler.dispatch(cpu.cycles().value_or(0));
}

RunResult Stm32F103Machine::Impl::fault(arch::cpu::CPU::CPUError error) {
    if (status.reason == StopReason::kFault) {
        return status; // Preserve the address of a rejected debugger memory access.
    }
    const auto& record = cpu.last_fault();
    status = {
        .reason = error == arch::cpu::CPU::CPUError::kIllegalInstruction
                      ? StopReason::kUnsupported
                      : StopReason::kFault,
        .pc = record ? record->pc : cpu.pc().value_or(0),
        .code = static_cast<uint64_t>(error),
        .detail = record && record->access_addr ? *record->access_addr : 0,
    };
    return status;
}

Stm32F103Machine::Stm32F103Machine() : impl_(std::make_unique<Impl>()) {}
Stm32F103Machine::~Stm32F103Machine() = default;

bool Stm32F103Machine::load_image(uint64_t address, std::span<const uint8_t> image) {
    auto destination = impl_->image_window(address, image.size());
    if (!destination) {
        return false;
    }
    if (!image.empty()) {
        std::memmove(destination->data(), image.data(), image.size());
    }
    impl_->cpu.invalidate_instruction_cache();
    return true;
}

std::expected<void, Stm32Error> Stm32F103Machine::reset() {
    auto& state = *impl_;
    state.reset_devices();
    std::ignore = state.cpu.reset();
    const auto fail = [&state](Stm32Error::Kind kind,
                               uint64_t address) -> std::expected<void, Stm32Error> {
        state.status = {
            .reason = StopReason::kFault,
            .pc = 0,
            .code = static_cast<uint64_t>(kind),
            .detail = address,
        };
        return std::unexpected(Stm32Error{.kind = kind, .address = address});
    };
    const auto sp = state.bus.read_u32(0);
    const auto pc = state.bus.read_u32(4);
    if (!sp || !pc) {
        return fail(Stm32Error::Kind::kVectorReadFault, !sp ? 0 : 4);
    }
    if ((*sp & 3U) != 0 || *sp < kSramBase || *sp > kSramBase + kSramSize) {
        return fail(Stm32Error::Kind::kInvalidStackPointer, *sp);
    }
    const uint32_t entry = *pc & ~uint32_t{1};
    const auto destination = state.image_window(entry, 2);
    if ((*pc & 1U) == 0 || !destination) {
        return fail(Stm32Error::Kind::kInvalidResetHandler, *pc);
    }
    const auto result = arch::arm::cortex_m3::cortex_m3_reset(state.cpu, state.memory, 0);
    if (!result) {
        return fail(Stm32Error::Kind::kCpuResetFailed, 0);
    }
    state.cpu.launch();
    state.status = {.reason = StopReason::kBudgetExhausted, .pc = entry};
    return {};
}

RunResult Stm32F103Machine::run_for(uint64_t budget) {
    auto& state = *impl_;
    if (state.status.reason != StopReason::kBudgetExhausted) {
        return state.status;
    }
    for (uint64_t i = 0; i < budget; ++i) {
        const auto before = state.cpu.cycles().value_or(0);
        const auto result = state.cpu.step();
        const auto elapsed = state.cpu.cycles().value_or(before) - before;
        if (elapsed != 0) {
            state.tick_devices(elapsed);
        }
        if (!result) {
            return state.fault(result.error());
        }
        if (state.status.reason != StopReason::kBudgetExhausted) {
            return state.status;
        }
        const auto cpu_state = state.cpu.state();
        if (!cpu_state || *cpu_state == arch::cpu::CPU::State::kFaulted) {
            const auto& record = state.cpu.last_fault();
            return state.fault(record ? record->kind
                                      : arch::cpu::CPU::CPUError::kDataAccessFault);
        }
        if (*cpu_state == arch::cpu::CPU::State::kHalted) {
            state.status = {.reason = StopReason::kHalted, .pc = state.cpu.pc().value_or(0)};
            return state.status;
        }
    }
    state.status.pc = state.cpu.pc().value_or(0);
    return state.status;
}

std::span<const char> Stm32F103Machine::console() const {
    return impl_->console;
}
void Stm32F103Machine::inject_serial(uint8_t byte) {
    impl_->usart1.inject_rx(byte);
}
void Stm32F103Machine::inject_serial2(uint8_t byte) {
    impl_->usart2.inject_rx(byte);
}
void Stm32F103Machine::set_semihosting_enabled(bool enabled) {
    impl_->semihosting_enabled = enabled;
}
arch::arm::cortex_m3::CortexM3CPU& Stm32F103Machine::cpu() {
    return impl_->cpu;
}
const arch::arm::cortex_m3::CortexM3CPU& Stm32F103Machine::cpu() const {
    return impl_->cpu;
}
kernel::Bus& Stm32F103Machine::bus() {
    return impl_->bus;
}
kernel::Scheduler& Stm32F103Machine::scheduler() {
    return impl_->scheduler;
}
device::hooks::EventBus& Stm32F103Machine::events() {
    return impl_->events;
}
device::stm32f1::Stm32f1Gpio* Stm32F103Machine::gpio(char port) {
    switch (port) {
    case 'A':
        return &impl_->gpioa;
    case 'B':
        return &impl_->gpiob;
    case 'C':
        return &impl_->gpioc;
    default:
        return nullptr;
    }
}

} // namespace micro_forge::machines
