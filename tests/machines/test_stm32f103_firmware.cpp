#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "micro_forge/arch/arm/cortex_m3/cortex_m3.hpp"
#include "micro_forge/basic_engine.hpp"
#include "micro_forge/kernel/bus/bus.hpp"
#include "micro_forge/machines/stm32f103.hpp"
#include "micro_forge/device/base/hooks/event_bus.hpp"

namespace {
using Machine = micro_forge::machines::Stm32F103Machine;

uint16_t le16(std::span<const uint8_t> bytes, std::size_t offset) {
    return static_cast<uint16_t>(bytes[offset] | (uint16_t{bytes[offset + 1]} << 8U));
}
uint32_t le32(std::span<const uint8_t> bytes, std::size_t offset) {
    return uint32_t{bytes[offset]} | (uint32_t{bytes[offset + 1]} << 8U) |
           (uint32_t{bytes[offset + 2]} << 16U) | (uint32_t{bytes[offset + 3]} << 24U);
}
bool fits(uint64_t offset, uint64_t size, uint64_t extent) {
    return offset <= extent && size <= extent - offset;
}

// Read a named guest symbol from the fixture, rather than hardcoding SRAM
// addresses which change with the compiler optimization level.
std::optional<uint32_t> symbol(std::span<const uint8_t> image, std::string_view name) {
    if (image.size() < 52 || le16(image, 46) != 40) {
        return std::nullopt;
    }
    const uint64_t shoff = le32(image, 32);
    const uint64_t count = le16(image, 48);
    if (!fits(shoff, count * 40, image.size())) {
        return std::nullopt;
    }
    for (uint64_t i = 0; i < count; ++i) {
        const auto section = image.subspan(static_cast<std::size_t>(shoff + i * 40), 40);
        if (le32(section, 4) != 2 || le32(section, 36) != 16) {
            continue;
        }
        const uint64_t offset = le32(section, 16);
        const uint64_t size = le32(section, 20);
        const uint64_t strings_index = le32(section, 24);
        if (!fits(offset, size, image.size()) || strings_index >= count || size % 16 != 0) {
            return std::nullopt;
        }
        const auto strings_header =
            image.subspan(static_cast<std::size_t>(shoff + strings_index * 40), 40);
        const uint64_t strings_offset = le32(strings_header, 16);
        const uint64_t strings_size = le32(strings_header, 20);
        if (!fits(strings_offset, strings_size, image.size())) {
            return std::nullopt;
        }
        const auto strings = image.subspan(static_cast<std::size_t>(strings_offset),
                                           static_cast<std::size_t>(strings_size));
        for (uint64_t entry = 0; entry < size; entry += 16) {
            const auto item = image.subspan(static_cast<std::size_t>(offset + entry), 16);
            const uint64_t start = le32(item, 0);
            if (!fits(start, name.size() + 1, strings.size())) {
                continue;
            }
            const auto candidate =
                strings.subspan(static_cast<std::size_t>(start), name.size() + 1);
            if (candidate.back() == 0 &&
                std::string_view(reinterpret_cast<const char*>(candidate.data()), name.size()) ==
                    name) {
                return le32(item, 4);
            }
        }
    }
    return std::nullopt;
}

struct FirmwareCase {
    const char* family;
    const char* suffix;
    const char* label;
};
class Stm32FirmwareTest : public testing::TestWithParam<FirmwareCase> {};

TEST_P(Stm32FirmwareTest, RealArmCompilerImageRunsWithObservableHardwareProgress) {
    const auto fixture = GetParam();
    const std::string path = std::string(STM32_FIRMWARE_DIR) + "/nucleo_f103rb_" + fixture.family +
                             ".ac6" + fixture.suffix + ".axf";
    std::ifstream stream(path, std::ios::binary);
    ASSERT_TRUE(stream) << path;
    const std::vector<uint8_t> image{std::istreambuf_iterator<char>(stream), {}};
    ASSERT_FALSE(image.empty());
    micro_forge::BasicEngine<Machine> engine(std::in_place);
    auto& machine = engine.machine();
    // The committed AC6 UART_Printf images use BKPT 0xAB / SYS_WRITEC in
    // fputc, not HAL_UART_Transmit. Enable that debugger service explicitly.
    machine.set_semihosting_enabled(true);
    unsigned gpio_edges = 0;
    machine.events().gpio.connect([&](const auto&) { ++gpio_edges; });
    const auto loaded = machine.load_elf(image);
    ASSERT_TRUE(loaded) << "load error=" << static_cast<unsigned>(loaded.error().kind);
    ASSERT_TRUE(machine.reset());
    const auto result = engine.run(2'000'000);
    ASSERT_EQ(result.reason, micro_forge::StopReason::kBudgetExhausted)
        << "pc=" << std::hex << result.pc << " code=" << result.code << " detail=" << result.detail;
    ASSERT_FALSE(machine.cpu().last_fault().has_value());
    EXPECT_GE(machine.cpu().cycles().value_or(0), 2'000'000U);
    const auto tick_address = symbol(image, "uwTick");
    ASSERT_TRUE(tick_address.has_value()) << "fixture lacks HAL tick symbol";
    const auto ticks = machine.bus().read_u32(*tick_address);
    ASSERT_TRUE(ticks);
    EXPECT_GT(*ticks, 0U) << "real SysTick/TIM2 handlers must advance HAL's tick";
    if (std::string_view(fixture.family) == "uart_printf") {
        const auto console = engine.console();
        ASSERT_FALSE(console.empty())
            << "printf must reach its opt-in semihost console, not just a delay loop";
        EXPECT_NE(std::string_view(console.data(), console.size()).find("UART"),
                  std::string_view::npos);
    }
    if (std::string_view(fixture.family) == "gpio_iotoggle") {
        EXPECT_GT(gpio_edges, 0U) << "firmware must drive a GPIO output";
    }
}

INSTANTIATE_TEST_SUITE_P(
    ArmCompiler6, Stm32FirmwareTest,
    testing::Values(FirmwareCase{.family = "gpio_iotoggle", .suffix = "", .label = "GpioDefault"},
                    FirmwareCase{.family = "gpio_iotoggle", .suffix = "-O0", .label = "GpioO0"},
                    FirmwareCase{.family = "gpio_iotoggle", .suffix = "-O2", .label = "GpioO2"},
                    FirmwareCase{.family = "gpio_iotoggle", .suffix = "-Oz", .label = "GpioOz"},
                    FirmwareCase{.family = "tim_timebase", .suffix = "", .label = "TimerDefault"},
                    FirmwareCase{.family = "tim_timebase", .suffix = "-O0", .label = "TimerO0"},
                    FirmwareCase{.family = "tim_timebase", .suffix = "-O2", .label = "TimerO2"},
                    FirmwareCase{.family = "tim_timebase", .suffix = "-Oz", .label = "TimerOz"},
                    FirmwareCase{.family = "uart_printf", .suffix = "", .label = "UartDefault"},
                    FirmwareCase{.family = "uart_printf", .suffix = "-O0", .label = "UartO0"},
                    FirmwareCase{.family = "uart_printf", .suffix = "-O2", .label = "UartO2"},
                    FirmwareCase{.family = "uart_printf", .suffix = "-Oz", .label = "UartOz"}),
    [](const testing::TestParamInfo<FirmwareCase>& info) { return info.param.label; });

} // namespace
