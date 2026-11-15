#include <gtest/gtest.h>

#include "micro_forge/arch/arm/cortex_m3/cortex_m3.hpp"
#include "micro_forge/basic_engine.hpp"
#include "micro_forge/kernel/bus/bus.hpp"
#include "micro_forge/kernel/scheduler.hpp"
#include "micro_forge/machines/stm32f103.hpp"
#include "micro_forge/device/base/hooks/event_bus.hpp"
#include "micro_forge/device/stm32f1/stm32f1_gpio.hpp"

#include <cstdint>
#include <utility>
#include <vector>

namespace {

using micro_forge::BasicEngine;
using micro_forge::RunResult;
using micro_forge::StopReason;
using micro_forge::arch::cpu::CPU;
using micro_forge::machines::Stm32F103Machine;
namespace cortex = micro_forge::arch::arm::cortex_m3;

class Stm32F103ExecutionTest : public ::testing::Test {
protected:
    static constexpr uint32_t kFlash = Stm32F103Machine::kFlashBase;
    static constexpr uint32_t kSram = Stm32F103Machine::kSramBase;
    static constexpr uint32_t kStack = kSram + Stm32F103Machine::kSramSize;
    static constexpr uint32_t kEntry = kFlash + 0x100;
    static constexpr uint32_t kHandler = kFlash + 0x200;
    static constexpr uint32_t kMarker = kSram + 0x100;
    static constexpr uint32_t kNvic = 0xE000E100;
    static constexpr uint32_t kSysTick = 0xE000E010;
    static constexpr uint32_t kRcc = 0x40021000;
    static constexpr uint32_t kTim2 = 0x40000000;
    static constexpr uint32_t kUsart1 = 0x40013800;
    static constexpr uint32_t kUsart2 = 0x40004400;
    static constexpr uint32_t kGpioA = 0x40010800;
    static constexpr uint32_t kAfio = 0x40010000;
    static constexpr uint32_t kExti = 0x40010400;

    BasicEngine<Stm32F103Machine> engine_{std::in_place};
    std::vector<uint8_t> image_ = std::vector<uint8_t>(0x400, 0);

    Stm32F103Machine& machine() { return engine_.machine(); }
    cortex::CortexM3CPU& cpu() { return machine().cpu(); }

    void SetUp() override {
        put32(0, kStack);
        put32(4, kEntry | 1u);
        put16(0x100, 0xE7FE); // B .
    }

    void put16(uint32_t offset, uint16_t value) {
        image_.at(offset) = static_cast<uint8_t>(value);
        image_.at(offset + 1) = static_cast<uint8_t>(value >> 8);
    }
    void put32(uint32_t offset, uint32_t value) {
        put16(offset, static_cast<uint16_t>(value));
        put16(offset + 2, static_cast<uint16_t>(value >> 16));
    }
    void write32(uint32_t address, uint32_t value) {
        ASSERT_TRUE(machine().bus().write_u32(address, value).has_value());
    }
    uint32_t read32(uint32_t address) {
        const auto value = machine().bus().read_u32(address);
        EXPECT_TRUE(value.has_value()) << std::hex << address;
        return value.value_or(0);
    }
    uint32_t reg(unsigned index) { return cpu().register_value(index).value_or(0xDEAD); }
    uint64_t cycles() { return cpu().cycles().value_or(UINT64_MAX); }

    void boot() {
        ASSERT_TRUE(engine_.load_image(kFlash, image_));
        ASSERT_TRUE(machine().reset().has_value());
    }
    RunResult run_slots(uint64_t count) {
        const auto before = cycles();
        const auto result = engine_.run(count);
        EXPECT_EQ(result.reason, StopReason::kBudgetExhausted);
        EXPECT_EQ(cycles(), before + count);
        return result;
    }
    void enable_irq(unsigned irq) { write32(kNvic + (irq / 32) * 4, 1u << (irq % 32)); }
    void pend_irq(unsigned irq) { write32(kNvic + 0x100 + (irq / 32) * 4, 1u << (irq % 32)); }
    uint32_t pending(unsigned bank = 0) { return read32(kNvic + 0x100 + bank * 4); }

    // Exception numbers include the 16 system vectors. The handler writes a
    // concrete SRAM value, then BX LR exercises the hardware EXC_RETURN frame.
    void marker_handler(unsigned exception, uint32_t offset, uint8_t marker) {
        put32(exception * 4, (kFlash + offset) | 1u);
        put16(offset, 0x4801); // LDR r0, [PC, #4] -> literal at offset+8
        put16(offset + 2, static_cast<uint16_t>(0x2100u | marker)); // MOVS r1, #marker
        put16(offset + 4, 0x6001);                                  // STR r1, [r0]
        put16(offset + 6, 0x4770);                                  // BX LR
        put32(offset + 8, kMarker);
    }
    void serial_handler(unsigned irq, uint32_t offset, uint32_t usart, uint32_t destination) {
        put32((irq + 16) * 4, (kFlash + offset) | 1u);
        put16(offset, 0x4802);      // LDR r0, [PC, #8] -> USART DR address
        put16(offset + 2, 0x6801);  // LDR r1, [r0], clears RXNE
        put16(offset + 4, 0x4A02);  // LDR r2, [PC, #8] -> destination address
        put16(offset + 6, 0x6011);  // STR r1, [r2]
        put16(offset + 8, 0x4770);  // BX LR
        put16(offset + 10, 0xBF00); // Align literals
        put32(offset + 12, usart + 4);
        put32(offset + 16, destination);
    }
};

TEST_F(Stm32F103ExecutionTest, FlashVectorBootUsesPhysicalImageAndZeroAlias) {
    put16(0x100, 0x202A); // MOVS r0, #42
    put16(0x102, 0xE7FE);
    ASSERT_TRUE(engine_.load_image(kFlash, image_));
    EXPECT_EQ(cpu().state().value(), CPU::State::kHalted);
    EXPECT_EQ(cycles(), 0u); // Loading alone does not reset or launch.
    EXPECT_EQ(read32(0), kStack);
    EXPECT_EQ(read32(4), kEntry | 1u);
    EXPECT_EQ(read32(kFlash), kStack);
    EXPECT_EQ(read32(kFlash + 4), kEntry | 1u);
    ASSERT_TRUE(machine().reset().has_value());
    EXPECT_EQ(cpu().state().value(), CPU::State::kRunning);
    EXPECT_EQ(reg(13), kStack);
    EXPECT_EQ(cpu().msp(), kStack);
    EXPECT_EQ(cpu().pc().value(), kEntry);
    EXPECT_EQ(cpu().xpsr(), cortex::kPsrT);
    const auto result = run_slots(1);
    EXPECT_EQ(result.pc, kEntry + 2);
    EXPECT_EQ(reg(0), 42u);

    const auto last = Stm32F103Machine::kFlashSize - 4;
    EXPECT_EQ(read32(last), read32(kFlash + last));
    EXPECT_FALSE(machine().bus().write_u32(kFlash, 0).has_value());
    EXPECT_FALSE(machine().bus().write_u32(0, 0).has_value());
    write32(kStack - 4, 0x12345678);
    EXPECT_EQ(read32(kStack - 4), 0x12345678u);
    EXPECT_FALSE(machine().bus().read_u32(kStack).has_value());
}

TEST_F(Stm32F103ExecutionTest, ExternalIrq28StacksExactFrameAndReturnsToThread) {
    marker_handler(16 + 28, 0x200, 28);
    boot();
    ASSERT_TRUE(cpu().set_register_value(0, 0x11111111).has_value());
    ASSERT_TRUE(cpu().set_register_value(1, 0x22222222).has_value());
    ASSERT_TRUE(cpu().set_register_value(2, 0x33333333).has_value());
    ASSERT_TRUE(cpu().set_register_value(3, 0x44444444).has_value());
    ASSERT_TRUE(cpu().set_register_value(12, 0xCCCCCCCC).has_value());
    enable_irq(28);
    pend_irq(28);
    run_slots(1); // Entry consumes one slot without executing the handler.
    EXPECT_TRUE(cpu().in_handler_mode());
    EXPECT_EQ(cpu().pc().value(), kHandler);
    EXPECT_EQ(reg(13), kStack - 32);
    EXPECT_EQ(reg(14), 0xFFFFFFF9u);
    const uint32_t frame[] = {0x11111111, 0x22222222, 0x33333333, 0x44444444,
                              0xCCCCCCCC, 0xFFFFFFFF, kEntry,     cortex::kPsrT};
    for (unsigned i = 0; i < 8; ++i) {
        EXPECT_EQ(read32(kStack - 32 + i * 4), frame[i]) << i;
    }
    EXPECT_EQ(pending() & (1u << 28), 0u);
    EXPECT_EQ(read32(kMarker), 0u);
    run_slots(4);
    EXPECT_EQ(read32(kMarker), 28u);
    EXPECT_FALSE(cpu().in_handler_mode());
    EXPECT_EQ(cpu().pc().value(), kEntry);
    EXPECT_EQ(reg(13), kStack);
    EXPECT_EQ(reg(14), 0xFFFFFFFFu);
    EXPECT_EQ(reg(0), frame[0]);
    EXPECT_EQ(reg(1), frame[1]);
    EXPECT_EQ(cpu().xpsr(), cortex::kPsrT);
}

TEST_F(Stm32F103ExecutionTest, RealSysTickWakesWfiThroughSystemVector15) {
    put16(0x100, 0xBF30); // WFI
    put16(0x102, 0xE7FE);
    marker_handler(15, 0x200, 15);
    marker_handler(16 + 15, 0x300, 99); // Wrong external-vector route must not win.
    boot();
    write32(kSysTick + 4, 3); // Four CPU clocks per period.
    write32(kSysTick + 8, 0);
    write32(kSysTick, 7); // ENABLE | TICKINT | CLKSOURCE
    run_slots(3);
    EXPECT_TRUE(cpu().is_sleeping());
    EXPECT_EQ(cpu().pc().value(), kEntry + 2);
    EXPECT_EQ(read32(kMarker), 0u);
    run_slots(1); // Wrap pends the system exception after the sleeping slot.
    EXPECT_TRUE(cpu().is_sleeping());
    EXPECT_NE(read32(kSysTick) & (1u << 16), 0u);
    EXPECT_EQ(pending(), 0u); // SysTick is not any external NVIC line.
    write32(kSysTick, 0);     // Stop repeated ticks while inspecting one exception.
    run_slots(1);
    EXPECT_FALSE(cpu().is_sleeping());
    EXPECT_TRUE(cpu().in_handler_mode());
    EXPECT_EQ(cpu().pc().value(), kHandler);
    run_slots(4);
    EXPECT_EQ(read32(kMarker), 15u);
    EXPECT_FALSE(cpu().in_handler_mode());
    EXPECT_EQ(cpu().pc().value(), kEntry + 2);
    EXPECT_EQ(reg(13), kStack);
}

TEST_F(Stm32F103ExecutionTest, PrimaskRetainsPendingIrqUntilCpsieInstruction) {
    put16(0x100, 0xB672); // CPSID i
    put16(0x102, 0xBF00); // NOP while masked
    put16(0x104, 0xB662); // CPSIE i
    put16(0x106, 0xE7FE);
    marker_handler(16 + 28, 0x200, 28);
    boot();
    run_slots(1);
    ASSERT_EQ(cpu().primask(), 1u);
    enable_irq(28);
    pend_irq(28);
    run_slots(1);
    EXPECT_FALSE(cpu().in_handler_mode());
    EXPECT_EQ(cpu().pc().value(), kEntry + 4);
    EXPECT_NE(pending() & (1u << 28), 0u);
    run_slots(1);
    EXPECT_EQ(cpu().primask(), 0u);
    EXPECT_FALSE(cpu().in_handler_mode());
    EXPECT_EQ(cpu().pc().value(), kEntry + 6);
    run_slots(1);
    EXPECT_EQ(cpu().pc().value(), kHandler);
    EXPECT_TRUE(cpu().in_handler_mode());
    run_slots(4);
    EXPECT_EQ(read32(kMarker), 28u);
    EXPECT_EQ(cpu().pc().value(), kEntry + 6);
    EXPECT_FALSE(cpu().in_handler_mode());
}

TEST_F(Stm32F103ExecutionTest, ScbVtorRelocationSelectsSramVectorTable) {
    marker_handler(16 + 28, 0x200, 28);
    marker_handler(16 + 29, 0x300, 88);
    boot();
    constexpr auto relocated = kSram + 0x800;
    write32(relocated + (16 + 28) * 4, (kFlash + 0x300) | 1u);
    write32(0xE000ED08, relocated); // SCB.VTOR
    EXPECT_EQ(read32(0xE000ED08), relocated);
    enable_irq(28);
    pend_irq(28);
    run_slots(1);
    EXPECT_EQ(cpu().pc().value(), kFlash + 0x300);
    run_slots(4);
    EXPECT_EQ(read32(kMarker), 88u);
    EXPECT_EQ(cpu().pc().value(), kEntry);
    EXPECT_EQ(reg(13), kStack);
}

TEST_F(Stm32F103ExecutionTest, Tim2ClockEnableAndRealRegisterLayoutDriveNvic28) {
    marker_handler(16 + 28, 0x200, 28);
    boot();
    enable_irq(28);
    write32(kTim2 + 0x28, 0); // PSC
    write32(kTim2 + 0x2C, 3); // ARR -> period ARR+1 = 4
    write32(kTim2 + 0x14, 1); // EGR.UG resets prescaler phase and CNT.
    write32(kTim2 + 0x10, 0); // Clear the software-generated UIF.
    write32(kTim2 + 0x0C, 1); // DIER.UIE
    write32(kTim2, 1);        // CR1.CEN
    run_slots(4);             // APB1 clock is still disabled.
    EXPECT_EQ(read32(kTim2 + 0x24), 0u);
    EXPECT_EQ(pending() & (1u << 28), 0u);
    write32(kRcc + 0x1C, 1); // APB1ENR.TIM2EN
    run_slots(3);
    EXPECT_EQ(read32(kTim2 + 0x24), 3u);
    EXPECT_EQ(read32(kTim2 + 0x10) & 1u, 0u);
    EXPECT_EQ(pending() & (1u << 28), 0u);
    run_slots(1);
    EXPECT_EQ(read32(kTim2 + 0x24), 0u);
    EXPECT_NE(read32(kTim2 + 0x10) & 1u, 0u);
    EXPECT_NE(pending() & (1u << 28), 0u);
    write32(kTim2, 0);
    run_slots(1);
    EXPECT_EQ(cpu().pc().value(), kHandler);
    run_slots(4);
    EXPECT_EQ(read32(kMarker), 28u);
    EXPECT_EQ(reg(13), kStack);
}

TEST_F(Stm32F103ExecutionTest, Usart1AndUsart2RxneInterruptsUseIndependentVectors) {
    serial_handler(37, 0x200, kUsart1, kMarker);
    serial_handler(38, 0x300, kUsart2, kMarker + 4);
    for (unsigned unit = 1; unit <= 2; ++unit) {
        SCOPED_TRACE(unit);
        boot();
        write32(kMarker, 0);
        write32(kMarker + 4, 0);
        const unsigned irq = unit == 1 ? 37 : 38;
        const uint32_t peripheral = unit == 1 ? kUsart1 : kUsart2;
        enable_irq(irq);
        write32(kRcc + 0x18, (1u << 14) | (1u << 2)); // USART1 + GPIOA
        write32(kRcc + 0x1C, 1u << 17);               // USART2
        write32(peripheral + 0x0C, (1u << 13) | (1u << 5) | (1u << 2));
        const uint8_t byte = unit == 1 ? 'Q' : 'Z';
        if (unit == 1) {
            engine_.inject_serial(byte);
        } else {
            machine().inject_serial2(byte);
        }
        EXPECT_NE(read32(peripheral) & (1u << 5), 0u);
        EXPECT_EQ(pending(1), 1u << (irq - 32));
        run_slots(1);
        EXPECT_EQ(cpu().pc().value(), kFlash + (unit == 1 ? 0x200 : 0x300));
        EXPECT_EQ(reg(14), 0xFFFFFFF9u);
        run_slots(5);
        EXPECT_EQ(read32(kMarker + (unit - 1) * 4), byte);
        EXPECT_EQ(read32(kMarker + (2 - unit) * 4), 0u);
        EXPECT_EQ(read32(peripheral) & (1u << 5), 0u);
        EXPECT_EQ(pending(1), 0u);
        EXPECT_FALSE(cpu().in_handler_mode());
        EXPECT_EQ(cpu().pc().value(), kEntry);
        EXPECT_EQ(reg(13), kStack);
    }
}

TEST_F(Stm32F103ExecutionTest, AfioRoutedGpioInputEdgeReachesExti3AndNvic9) {
    marker_handler(16 + 9, 0x200, 9);
    boot();
    write32(kRcc + 0x18, (1u << 0) | (1u << 2) | (1u << 3));
    write32(kAfio + 8, 1u << 12); // EXTI3 selects PB3, not PA3.
    write32(kExti, 1u << 3);      // IMR
    write32(kExti + 8, 1u << 3);  // RTSR
    enable_irq(9);
    ASSERT_NE(machine().gpio('A'), nullptr);
    ASSERT_NE(machine().gpio('B'), nullptr);
    machine().gpio('A')->simulate_input(3, true);
    EXPECT_EQ(read32(kExti + 0x14) & (1u << 3), 0u);
    EXPECT_EQ(pending() & (1u << 9), 0u);
    run_slots(1);
    EXPECT_EQ(cpu().pc().value(), kEntry);
    machine().gpio('B')->simulate_input(3, true);
    EXPECT_NE(read32(kExti + 0x14) & (1u << 3), 0u);
    EXPECT_NE(pending() & (1u << 9), 0u);
    run_slots(1);
    EXPECT_EQ(cpu().pc().value(), kHandler);
    run_slots(4);
    EXPECT_EQ(read32(kMarker), 9u);
    EXPECT_EQ(cpu().pc().value(), kEntry);
    write32(kExti + 0x14, 1u << 3); // PR is write-one-to-clear.
    EXPECT_EQ(read32(kExti + 0x14) & (1u << 3), 0u);
}

TEST_F(Stm32F103ExecutionTest, SchedulerUsesCpuSlotsIncludingWfiAndRunZeroDoesNothing) {
    put16(0x100, 0xBF30);
    put16(0x102, 0xE7FE);
    marker_handler(16 + 28, 0x200, 28);
    boot();
    enable_irq(28);
    bool immediate_fired = false;
    uint64_t irq_cycle = UINT64_MAX;
    machine().scheduler().schedule(0, [&] { immediate_fired = true; });
    machine().scheduler().schedule(3, [&] {
        irq_cycle = cycles();
        pend_irq(28);
    });
    const auto zero = run_slots(0);
    EXPECT_EQ(zero.pc, kEntry);
    EXPECT_FALSE(immediate_fired);
    EXPECT_EQ(irq_cycle, UINT64_MAX);
    run_slots(1);
    EXPECT_TRUE(immediate_fired);
    EXPECT_TRUE(cpu().is_sleeping());
    run_slots(1);
    EXPECT_EQ(irq_cycle, UINT64_MAX);
    run_slots(1);
    EXPECT_EQ(irq_cycle, 3u);
    EXPECT_TRUE(cpu().is_sleeping());
    run_slots(1);
    EXPECT_EQ(cycles(), 4u);
    EXPECT_FALSE(cpu().is_sleeping());
    EXPECT_TRUE(cpu().in_handler_mode());
    EXPECT_EQ(cpu().pc().value(), kHandler);
}

TEST_F(Stm32F103ExecutionTest, RepeatedResetClearsSleepingPendingDevicesAndKeepsGpioObservers) {
    put16(0x100, 0xBF30);
    put16(0x102, 0xE7FE);
    marker_handler(16 + 28, 0x200, 28);
    std::vector<micro_forge::device::hooks::GpioEdge> edges;
    auto& events = machine().events();
    const auto token = events.gpio.connect([&](const auto& edge) { edges.push_back(edge); });
    boot();
    write32(kRcc + 0x18, 1u << 2);
    write32(kGpioA, 0x44442444); // PA3 output push-pull, 2 MHz.
    write32(kGpioA + 0x0C, 1u << 3);
    run_slots(1);
    ASSERT_TRUE(cpu().is_sleeping());
    enable_irq(28);
    pend_irq(28);
    write32(kSysTick + 4, 3);
    write32(kSysTick, 7);
    write32(kUsart1 + 0x0C, 1u << 5);
    engine_.inject_serial('P');
    write32(kTim2, 1);
    bool stale_callback_fired = false;
    machine().scheduler().schedule(2, [&] { stale_callback_fired = true; });

    ASSERT_TRUE(machine().reset().has_value());
    EXPECT_EQ(&machine().events(), &events);
    EXPECT_FALSE(cpu().is_sleeping());
    EXPECT_FALSE(cpu().in_handler_mode());
    EXPECT_FALSE(cpu().last_fault().has_value());
    EXPECT_EQ(cpu().state().value(), CPU::State::kRunning);
    EXPECT_EQ(cycles(), 0u);
    EXPECT_EQ(cpu().pc().value(), kEntry);
    EXPECT_EQ(read32(kNvic), 0u);
    EXPECT_EQ(pending(), 0u);
    EXPECT_EQ(pending(1), 0u);
    EXPECT_EQ(read32(kSysTick), 0u);
    EXPECT_EQ(read32(kTim2), 0u);
    EXPECT_EQ(read32(kUsart1) & (1u << 5), 0u);
    EXPECT_EQ(read32(kUsart1 + 0x0C), 0u);
    EXPECT_EQ(read32(kGpioA + 0x0C), 0u);
    write32(kRcc + 0x18, 1u << 2);
    write32(kGpioA, 0x44442444); // PA3 output push-pull, 2 MHz.
    write32(kGpioA + 0x0C, 1u << 3);
    ASSERT_EQ(edges.size(), 2u);
    for (const auto& edge : edges) {
        EXPECT_EQ(edge.port, 'A');
        EXPECT_EQ(edge.pin, 3u);
        EXPECT_TRUE(edge.rising);
        EXPECT_EQ(edge.cycle, 0u);
    }
    run_slots(3);
    EXPECT_TRUE(cpu().is_sleeping());
    EXPECT_FALSE(stale_callback_fired);
    events.gpio.disconnect(token);
}

TEST_F(Stm32F103ExecutionTest, ResetRecoversFaultWithoutRemovingUartObserver) {
    put16(0x100, 0xDE00); // UDF, with no HardFault handler installed.
    std::vector<micro_forge::device::hooks::UartByte> bytes;
    auto& events = machine().events();
    const auto token = events.uart.connect([&](const auto& byte) { bytes.push_back(byte); });
    boot();
    const auto fault = engine_.run(1);
    EXPECT_EQ(fault.reason, StopReason::kUnsupported);
    EXPECT_EQ(fault.pc, kEntry);
    EXPECT_EQ(fault.code, static_cast<uint64_t>(CPU::CPUError::kIllegalInstruction));
    EXPECT_EQ(cpu().state().value(), CPU::State::kFaulted);
    ASSERT_TRUE(cpu().last_fault().has_value());

    put16(0x100, 0xE7FE);
    ASSERT_TRUE(engine_.load_image(kFlash, image_));
    EXPECT_EQ(cpu().state().value(), CPU::State::kFaulted); // Loading is not reset.
    ASSERT_TRUE(machine().reset().has_value());
    EXPECT_FALSE(cpu().last_fault().has_value());
    EXPECT_FALSE(cpu().in_handler_mode());
    run_slots(1);
    EXPECT_EQ(cpu().pc().value(), kEntry);
    write32(kUsart1 + 4, 'A');
    ASSERT_EQ(bytes.size(), 1u);
    EXPECT_EQ(bytes[0].unit, 1u);
    EXPECT_EQ(bytes[0].byte, 'A');
    EXPECT_EQ(bytes[0].cycle, 1u);
    ASSERT_TRUE(machine().reset().has_value());
    EXPECT_TRUE(engine_.console().empty());
    write32(kUsart1 + 4, 'B');
    ASSERT_EQ(bytes.size(), 2u);
    EXPECT_EQ(bytes[1].unit, 1u);
    EXPECT_EQ(bytes[1].byte, 'B');
    EXPECT_EQ(bytes[1].cycle, 0u);
    events.uart.disconnect(token);
}

TEST_F(Stm32F103ExecutionTest, SysTickReferenceClockDividesByEightAndCtrlReadClearsCountflag) {
    boot();
    write32(kSysTick + 4, 1); // LOAD+1 = two reference-clock ticks.
    write32(kSysTick + 8, 0);
    write32(kSysTick, 1); // ENABLE, reference clock HCLK/8, no interrupt.
    run_slots(15);
    EXPECT_EQ(read32(kSysTick) & (1u << 16), 0u);
    EXPECT_EQ(read32(kSysTick + 8), 1u);
    run_slots(1);
    EXPECT_NE(read32(kSysTick) & (1u << 16), 0u);
    EXPECT_EQ(read32(kSysTick) & (1u << 16), 0u);
    EXPECT_EQ(read32(kSysTick + 8), 0u);
    EXPECT_FALSE(cpu().in_handler_mode());

    write32(kSysTick + 8, 0);
    write32(kSysTick, 5); // Switch to direct CPU clock.
    run_slots(1);
    EXPECT_EQ(read32(kSysTick + 8), 1u);
    EXPECT_EQ(read32(kSysTick) & (1u << 16), 0u);
    run_slots(1);
    EXPECT_EQ(read32(kSysTick + 8), 0u);
    EXPECT_NE(read32(kSysTick) & (1u << 16), 0u);
}

} // namespace
