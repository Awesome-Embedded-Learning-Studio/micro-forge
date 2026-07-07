// EventBus wiring tests — confirm GPIO edges AND USART bytes fan into the
// SoC's EventBus (and thus any connected RingSink), closing the G3 "声明了
// 从未实例化" gap. EXTI/output behaviour stays intact alongside the bus.
#include "chips/stm32f1/soc/stm32f103_soc.hpp"
#include "hooks/events.hpp"
#include "hooks/ring_sink.hpp"

#include <gtest/gtest.h>

using namespace micro_forge;
using micro_forge::chips::stm32f1::Stm32f103Soc;
using micro_forge::hooks::GpioEdge;
using micro_forge::hooks::RingSink;
using micro_forge::hooks::UartByte;

TEST(EventBusTest, GpioEdgeFlowsToRingSink) {
    auto soc = Stm32f103Soc::create();
    ASSERT_TRUE(soc.has_value());
    RingSink<GpioEdge> sink(64);
    (*soc)->parts().event_bus.gpio.connect(sink.slot());

    (*soc)->parts().gpio('A').set_pin(5, true);

    auto events = sink.drain();
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].port, 'A');
    EXPECT_EQ(events[0].pin, 5u);
    EXPECT_TRUE(events[0].rising);
}

TEST(EventBusTest, EachEdgeIsASeparateEvent) {
    auto soc = Stm32f103Soc::create();
    ASSERT_TRUE(soc.has_value());
    RingSink<GpioEdge> sink(64);
    (*soc)->parts().event_bus.gpio.connect(sink.slot());

    (*soc)->parts().gpio('B').set_pin(3, true);
    (*soc)->parts().gpio('B').set_pin(3, false);

    auto events = sink.drain();
    ASSERT_EQ(events.size(), 2u);
    EXPECT_TRUE(events[0].rising);
    EXPECT_FALSE(events[1].rising);
    EXPECT_EQ(events[0].pin, 3u);
}

TEST(EventBusTest, UartByteFlowsToRingSink) {
    auto soc = Stm32f103Soc::create();
    ASSERT_TRUE(soc.has_value());
    RingSink<UartByte> sink(64);
    (*soc)->parts().event_bus.uart.connect(sink.slot());

    (*soc)->parts().serial().send_byte('X');

    auto events = sink.drain();
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].byte, static_cast<uint8_t>('X'));
    EXPECT_EQ(events[0].unit, 1u);
}
