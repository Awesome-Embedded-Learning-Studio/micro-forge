#include "micro_forge/device/arm/nvic.hpp"
#include "micro_forge/device/arm/scb.hpp"
#include "micro_forge/device/arm/systick.hpp"
#include "micro_forge/device/base/device.hpp"
#include "micro_forge/device/base/hooks/events.hpp"
#include "micro_forge/device/base/types.hpp"
#include "micro_forge/device/base/weak_ptr.hpp"
#include "micro_forge/device/stm32f1/stm32f1_afio.hpp"
#include "micro_forge/device/stm32f1/stm32f1_exti.hpp"
#include "micro_forge/device/stm32f1/stm32f1_flash.hpp"
#include "micro_forge/device/stm32f1/stm32f1_gpio.hpp"
#include "micro_forge/device/stm32f1/stm32f1_rcc.hpp"
#include "micro_forge/device/stm32f1/stm32f1_timer.hpp"
#include "micro_forge/device/stm32f1/stm32f1_usart.hpp"

#include <cstdint>
#include <gtest/gtest.h>
#include <initializer_list>
#include <utility>

namespace {
using namespace micro_forge::device;
using namespace micro_forge::device::arm;
using namespace micro_forge::device::stm32f1;
using RegisterValues = std::initializer_list<std::pair<addr_t, data_t>>;

void write_registers(Device& device, RegisterValues values) {
    for (auto [offset, value] : values) {
        ASSERT_TRUE(device.write(offset, value, Width::kWord).has_value()) << offset;
    }
}

template <typename T>
void check_reset(T& device, RegisterValues expected) {
    WeakPtr<Device> weak = device.get_weak();
    Device& base = device;
    base.reset();
    ASSERT_TRUE(weak.is_valid());
    EXPECT_EQ(weak.get(), &base);
    for (auto [offset, value] : expected) {
        auto actual = weak->read(offset, Width::kWord);
        ASSERT_TRUE(actual.has_value()) << offset;
        EXPECT_EQ(*actual, value) << offset;
    }
}

TEST(PeripheralResetTest, NvicClearsEnablePendingPrioritiesAndCache) {
    NvicPeripheral device;
    write_registers(device, {
                                {0x000U, 0xFFFFFFFFU},
                                {0x01CU, 0xFFFFU},
                                {0x300U, 0x10203040U},
                                {0x3ECU, 0x8090A0B0U},
                            });
    device.set_pending(0U);
    device.set_pending(239U);
    EXPECT_TRUE(device.has_pending_irq());
    check_reset(device, {
                            {0x000U, 0U},
                            {0x01CU, 0U},
                            {0x100U, 0U},
                            {0x11CU, 0U},
                            {0x300U, 0U},
                            {0x3ECU, 0U},
                        });
    EXPECT_EQ(device.enabled_count(), 0U);
    EXPECT_FALSE(device.has_pending_irq());
    EXPECT_EQ(device.highest_priority_pending_irq(), 0xFFU);
    EXPECT_EQ(device.irq_priority(239U), 0U);
}

TEST(PeripheralResetTest, ScbRestoresRegistersAndPreservesCallbacks) {
    ScbPeripheral device;
    unsigned vtor_calls = 0U;
    unsigned group_calls = 0U;
    device.set_vtor_callback([&](uint32_t) { ++vtor_calls; });
    device.set_prigroup_callback([&](uint8_t) { ++group_calls; });
    write_registers(device, {
                                {0x04U, 0xFFFFFFFFU},
                                {0x08U, 0x08001000U},
                                {0x0CU, 0x05FA0700U},
                                {0x10U, 7U},
                                {0x14U, 0U},
                                {0x18U, 0xFFFFFFFFU},
                                {0x1CU, 0xFFFFFFFFU},
                                {0x20U, 0xFFFFFFFFU},
                                {0x24U, 0xFFFFFFFFU},
                            });
    check_reset(device, {
                            {0x00U, 0x412FC230U},
                            {0x04U, 0U},
                            {0x08U, 0U},
                            {0x0CU, 0xFA050000U},
                            {0x10U, 0U},
                            {0x14U, 0x200U},
                            {0x18U, 0U},
                            {0x1CU, 0U},
                            {0x20U, 0U},
                            {0x24U, 0U},
                        });
    EXPECT_EQ(vtor_calls, 1U);
    EXPECT_EQ(group_calls, 1U);
    write_registers(device, {{0x08U, 0x08002000U}, {0x0CU, 0x05FA0300U}});
    EXPECT_EQ(vtor_calls, 2U);
    EXPECT_EQ(group_calls, 2U);
}

TEST(PeripheralResetTest, SysTickClearsCounterAndPreservesIrqCallback) {
    SysTickPeripheral device;
    unsigned irqs = 0U;
    device.set_irq_callback([&] { ++irqs; });
    write_registers(device, {{0x04U, 3U}, {0x00U, 7U}});
    device.tick(4U);
    EXPECT_EQ(irqs, 1U);
    check_reset(device, {{0x00U, 0U}, {0x04U, 0U}, {0x08U, 0U}});
    EXPECT_EQ(device.cycles_until_next_tick(), 0U);
    device.tick(100U);
    EXPECT_EQ(irqs, 1U);
    write_registers(device, {{0x04U, 3U}, {0x00U, 3U}});
    device.tick(4U);
    EXPECT_EQ(irqs, 2U);
}

TEST(PeripheralResetTest, RccRestoresClockAndEnableRegisters) {
    Stm32f1Rcc device;
    write_registers(device, {
                                {0x00U, 0xFFFFFFFFU},
                                {0x04U, 0xFFFFFFFFU},
                                {0x08U, 0xFFFFFFFFU},
                                {0x14U, 0xFFFFFFFFU},
                                {0x18U, 0xFFFFFFFFU},
                                {0x1CU, 0xFFFFFFFFU},
                            });
    EXPECT_TRUE(device.is_clock_enabled(0x40010800U));
    check_reset(device, {
                            {0x00U, 0x83U},
                            {0x04U, 0U},
                            {0x08U, 0U},
                            {0x14U, 0U},
                            {0x18U, 0U},
                            {0x1CU, 0U},
                        });
    EXPECT_FALSE(device.is_clock_enabled(0x40010800U));
}

TEST(PeripheralResetTest, AfioRestoresAllRoutingRegisters) {
    Stm32f1Afio device;
    write_registers(device, {
                                {0x00U, 0xFFFFFFFFU},
                                {0x04U, 0xFFFFFFFFU},
                                {0x08U, 0x11111111U},
                                {0x0CU, 0x22222222U},
                                {0x10U, 0x33333333U},
                                {0x14U, 0x44444444U},
                                {0x18U, 0xFFFFFFFFU},
                            });
    check_reset(device, {
                            {0x00U, 0U},
                            {0x04U, 0U},
                            {0x08U, 0U},
                            {0x0CU, 0U},
                            {0x10U, 0U},
                            {0x14U, 0U},
                            {0x18U, 0U},
                        });
    for (uint8_t line = 0U; line < 16U; ++line) {
        EXPECT_EQ(device.exti_line_port(line), 0U);
    }
}

TEST(PeripheralResetTest, ExtiPreservesAfioConnectionAndIrqCallback) {
    Stm32f1Afio afio;
    write_registers(afio, {{0x08U, 1U << 12U}}); // EXTI3 selects GPIOB.
    Stm32f1Exti device;
    device.set_afio(afio);
    unsigned irqs = 0U;
    device.set_irq_callback([&](intr::intr_n_t irq) {
        EXPECT_EQ(irq, 9U);
        ++irqs;
    });
    write_registers(device, {
                                {0x00U, 8U},
                                {0x04U, 8U},
                                {0x08U, 8U},
                                {0x0CU, 8U},
                                {0x10U, 8U},
                            });
    EXPECT_TRUE(device.pending(3U));
    check_reset(device, {
                            {0x00U, 0U},
                            {0x04U, 0U},
                            {0x08U, 0U},
                            {0x0CU, 0U},
                            {0x10U, 0U},
                            {0x14U, 0U},
                        });
    EXPECT_EQ(irqs, 1U);
    write_registers(device, {{0x00U, 8U}, {0x08U, 8U}});
    device.on_gpio_edge({.port = 'A', .pin = 3U, .rising = true});
    EXPECT_FALSE(device.pending(3U));
    device.on_gpio_edge({.port = 'B', .pin = 3U, .rising = true});
    EXPECT_TRUE(device.pending(3U));
    EXPECT_EQ(irqs, 2U);
}

TEST(PeripheralResetTest, FlashRestoresControlRegisters) {
    Stm32f1Flash device;
    write_registers(device, {
                                {0x00U, 0xFFFFFFFFU},
                                {0x04U, 0xFFFFFFFFU},
                                {0x08U, 0xFFFFFFFFU},
                                {0x0CU, 0xFFFFFFFFU},
                                {0x10U, 0xFFFFFFFFU},
                            });
    check_reset(device, {
                            {0x00U, 0x30U},
                            {0x04U, 0U},
                            {0x08U, 0U},
                            {0x0CU, 0U},
                            {0x10U, 0x80U},
                        });
}

TEST(PeripheralResetTest, GpioPreservesPortCallbacksSignalAndCycleSource) {
    Stm32f1Gpio device('B');
    unsigned pin_changes = 0U;
    unsigned edges = 0U;
    uint64_t timestamp = 0U;
    device.set_pin_change_callback([&](uint8_t, bool) { ++pin_changes; });
    device.set_cycle_source([] { return 42U; });
    device.edge_signal().connect([&](const hooks::GpioEdge& edge) {
        ++edges;
        timestamp = edge.cycle;
        EXPECT_EQ(edge.port, 'B');
    });
    write_registers(device, {{0x00U, 0U}, {0x04U, 0U}, {0x0CU, 8U}, {0x18U, 7U}});
    device.simulate_input(2U, true);
    EXPECT_EQ(pin_changes, 1U);
    EXPECT_EQ(edges, 2U);
    check_reset(device, {
                            {0x00U, 0x44444444U},
                            {0x04U, 0x44444444U},
                            {0x08U, 0U},
                            {0x0CU, 0U},
                            {0x18U, 0U},
                        });
    EXPECT_EQ(device.name(), "GPIOB");
    EXPECT_EQ(pin_changes, 1U); // reset must not emit an output change.
    EXPECT_EQ(edges, 2U);
    device.set_pin(3U, true);
    device.simulate_input(2U, true);
    EXPECT_EQ(pin_changes, 2U);
    EXPECT_EQ(edges, 4U);
    EXPECT_EQ(timestamp, 42U);
}

TEST(PeripheralResetTest, UsartClearsRxAndPreservesOutputAndIrqCallbacks) {
    Stm32f1Usart device;
    unsigned outputs = 0U;
    unsigned irqs = 0U;
    device.set_output([&](uint8_t byte) {
        EXPECT_EQ(byte, 'X');
        ++outputs;
    });
    device.set_irq_callback([&] { ++irqs; });
    write_registers(device, {
                                {0x00U, 0U},
                                {0x04U, 'X'},
                                {0x08U, 123U},
                                {0x0CU, 1U << 5U},
                                {0x10U, 7U},
                                {0x14U, 7U},
                            });
    device.inject_rx('Y');
    EXPECT_EQ(irqs, 1U);
    check_reset(device, {
                            {0x00U, 0xC0U},
                            {0x04U, 0U},
                            {0x08U, 0U},
                            {0x0CU, 0U},
                            {0x10U, 0U},
                            {0x14U, 0U},
                        });
    EXPECT_EQ(outputs, 1U);
    EXPECT_EQ(irqs, 1U);
    device.send_byte('X');
    write_registers(device, {{0x0CU, 1U << 5U}});
    device.inject_rx('Z');
    EXPECT_EQ(outputs, 2U);
    EXPECT_EQ(irqs, 2U);
}

TEST(PeripheralResetTest, TimerClearsPrescalerPhaseAndPreservesIrqCallback) {
    Stm32f1Timer device;
    unsigned irqs = 0U;
    device.set_irq_callback([&] { ++irqs; });
    write_registers(device, {
                                {0x00U, 1U},
                                {0x04U, 0x70U},
                                {0x08U, 0x77U},
                                {0x0CU, 1U},
                                {0x10U, 1U},
                                {0x28U, 9U},
                                {0x2CU, 100U},
                                {0x24U, 2U},
                            });
    device.tick(9U); // Leave a partial prescaler interval.
    check_reset(device, {
                            {0x00U, 0U},
                            {0x04U, 0U},
                            {0x08U, 0U},
                            {0x0CU, 0U},
                            {0x10U, 0U},
                            {0x14U, 0U},
                            {0x28U, 0U},
                            {0x2CU, 0xFFFFU},
                            {0x24U, 0U},
                        });
    write_registers(device, {{0x28U, 9U}, {0x2CU, 2U}, {0x0CU, 1U}, {0x00U, 1U}});
    device.tick(1U);
    EXPECT_EQ(device.counter(), 0U);
    device.tick(29U);
    EXPECT_EQ(irqs, 1U);
    EXPECT_TRUE(device.update_flag());
}

} // namespace
