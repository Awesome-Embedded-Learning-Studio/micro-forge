// Exercise peripheral events independently of machine and CPU assembly.
#include "micro_forge/device/base/hooks/event_bus.hpp"
#include "micro_forge/device/base/hooks/events.hpp"
#include "micro_forge/device/base/hooks/ring_sink.hpp"
#include "micro_forge/device/base/hooks/signal.hpp"
#include "micro_forge/device/stm32f1/stm32f1_gpio.hpp"
#include "micro_forge/device/stm32f1/stm32f1_usart.hpp"

#include <cstdint>
#include <gtest/gtest.h>

using micro_forge::device::hooks::EventBus;
using micro_forge::device::hooks::GpioEdge;
using micro_forge::device::hooks::RingSink;
using micro_forge::device::hooks::Signal;
using micro_forge::device::hooks::UartByte;
using micro_forge::device::stm32f1::Stm32f1Gpio;
using micro_forge::device::stm32f1::Stm32f1Usart;

namespace {

class EventBusTest : public ::testing::Test {
protected:
    EventBusTest() {
        bus_.set_cycle_source([this] { return cycle_; });
        for (auto* gpio : {&gpio_a_, &gpio_b_}) {
            gpio->set_cycle_source([this] { return bus_.now(); });
            gpio->edge_signal().connect([this](const GpioEdge& edge) { bus_.gpio.emit(edge); });
        }
        usart_.set_output(
            [this](uint8_t byte) { bus_.uart.emit(UartByte{{bus_.now()}, 1U, byte}); });
    }

    uint64_t cycle_ = 0U;
    EventBus bus_;
    Stm32f1Gpio gpio_a_{'A'};
    Stm32f1Gpio gpio_b_{'B'};
    Stm32f1Usart usart_;
};

TEST_F(EventBusTest, GpioEdgeFlowsToRingSink) {
    RingSink<GpioEdge> sink(64U);
    bus_.gpio.connect(sink.slot());

    gpio_a_.set_pin(5U, true);

    auto events = sink.drain();
    ASSERT_EQ(events.size(), 1U);
    EXPECT_EQ(events[0U].port, 'A');
    EXPECT_EQ(events[0U].pin, 5U);
    EXPECT_TRUE(events[0U].rising);
}

TEST_F(EventBusTest, EachEdgeIsASeparateEvent) {
    RingSink<GpioEdge> sink(64U);
    bus_.gpio.connect(sink.slot());

    gpio_b_.set_pin(3U, true);
    gpio_b_.set_pin(3U, false);

    auto events = sink.drain();
    ASSERT_EQ(events.size(), 2U);
    EXPECT_TRUE(events[0U].rising);
    EXPECT_FALSE(events[1U].rising);
    EXPECT_EQ(events[0U].pin, 3U);
}

TEST_F(EventBusTest, UartByteFlowsToRingSink) {
    RingSink<UartByte> sink(64U);
    bus_.uart.connect(sink.slot());

    usart_.send_byte('X');

    auto events = sink.drain();
    ASSERT_EQ(events.size(), 1U);
    EXPECT_EQ(events[0U].byte, static_cast<uint8_t>('X'));
    EXPECT_EQ(events[0U].unit, 1U);
}

TEST_F(EventBusTest, CycleSourceStampsGpioAndUartEvents) {
    RingSink<GpioEdge> gpio_sink(4U);
    RingSink<UartByte> uart_sink(4U);
    bus_.gpio.connect(gpio_sink.slot());
    bus_.uart.connect(uart_sink.slot());

    cycle_ = 41U;
    gpio_a_.set_pin(2U, true);
    cycle_ = 42U;
    usart_.send_byte('Y');

    auto gpio_events = gpio_sink.drain();
    auto uart_events = uart_sink.drain();
    ASSERT_EQ(gpio_events.size(), 1U);
    ASSERT_EQ(uart_events.size(), 1U);
    EXPECT_EQ(gpio_events[0U].cycle, 41U);
    EXPECT_EQ(uart_events[0U].cycle, 42U);
}

TEST_F(EventBusTest, RepeatedLevelDoesNotEmitAnEdge) {
    RingSink<GpioEdge> sink(4U);
    bus_.gpio.connect(sink.slot());

    gpio_a_.set_pin(5U, true);
    gpio_a_.set_pin(5U, true);

    EXPECT_EQ(sink.drain().size(), 1U);
    EXPECT_TRUE(sink.drain().empty());
}

TEST(EventSignalTest, DisconnectRemovesOnlySelectedSlot) {
    Signal<UartByte> signal;
    unsigned first = 0U;
    unsigned second = 0U;
    auto const token = signal.connect([&](const UartByte&) { ++first; });
    signal.connect([&](const UartByte&) { ++second; });
    EXPECT_FALSE(signal.empty());

    signal.emit(UartByte{.unit = 1U, .byte = 'A'});
    signal.disconnect(token);
    signal.disconnect(token);
    signal.emit(UartByte{.unit = 1U, .byte = 'B'});

    EXPECT_EQ(first, 1U);
    EXPECT_EQ(second, 2U);
}

TEST(EventRingSinkTest, OverflowKeepsNewestEventsAndCountsDrops) {
    RingSink<UartByte> sink(2U);
    auto const slot = sink.slot();
    slot(UartByte{{1U}, 1U, 'A'});
    slot(UartByte{{2U}, 1U, 'B'});
    slot(UartByte{{3U}, 1U, 'C'});

    auto events = sink.drain();
    ASSERT_EQ(events.size(), 2U);
    EXPECT_EQ(events[0U].byte, 'B');
    EXPECT_EQ(events[1U].byte, 'C');
    EXPECT_EQ(sink.dropped(), 1U);
    EXPECT_TRUE(sink.drain().empty());
    EXPECT_EQ(sink.dropped(), 1U);
}

TEST(EventRingSinkTest, ZeroCapacityStillCollectsOneEvent) {
    RingSink<UartByte> sink(0U);
    sink.slot()(UartByte{.unit = 1U, .byte = 'Z'});

    auto events = sink.drain();
    ASSERT_EQ(events.size(), 1U);
    EXPECT_EQ(events[0U].byte, 'Z');
    EXPECT_EQ(sink.dropped(), 0U);
}

} // namespace
