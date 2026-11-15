#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

#include "micro_forge/arch/arm/cortex_m3/cortex_m3.hpp"
#include "micro_forge/basic_engine.hpp"
#include "micro_forge/kernel/bus/bus.hpp"
#include "micro_forge/machines/stm32f103.hpp"

namespace {
using Machine = micro_forge::machines::Stm32F103Machine;
using Error = micro_forge::machines::Stm32Error;

void put16(std::vector<uint8_t>& bytes, std::size_t offset, uint16_t value) {
    bytes.at(offset) = static_cast<uint8_t>(value);
    bytes.at(offset + 1) = static_cast<uint8_t>(value >> 8U);
}
void put32(std::vector<uint8_t>& bytes, std::size_t offset, uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) {
        bytes.at(offset + i) = static_cast<uint8_t>(value >> (8U * i));
    }
}
struct Payload {
    uint32_t vaddr;
    uint32_t paddr;
    uint32_t memsz;
    std::vector<uint8_t> data;
};
std::vector<uint8_t> elf(std::span<const Payload> payloads) {
    std::vector<uint8_t> bytes(52 + 32 * payloads.size());
    bytes[0] = 0x7F;
    bytes[1] = 'E';
    bytes[2] = 'L';
    bytes[3] = 'F';
    bytes[4] = 1;
    bytes[5] = 1;
    bytes[6] = 1;
    put16(bytes, 16, 2);
    put16(bytes, 18, 40);
    put32(bytes, 20, 1);
    put32(bytes, 24, 0xDEADBEEF); // Reset must use vectors, not this ELF entry.
    put32(bytes, 28, 52);
    put16(bytes, 40, 52);
    put16(bytes, 42, 32);
    put16(bytes, 44, static_cast<uint16_t>(payloads.size()));
    for (std::size_t i = 0; i < payloads.size(); ++i) {
        const auto& payload = payloads[i];
        const auto header = 52 + i * 32;
        put32(bytes, header, 1);
        put32(bytes, header + 4, static_cast<uint32_t>(bytes.size()));
        put32(bytes, header + 8, payload.vaddr);
        put32(bytes, header + 12, payload.paddr);
        put32(bytes, header + 16, static_cast<uint32_t>(payload.data.size()));
        put32(bytes, header + 20, payload.memsz);
        put32(bytes, header + 24, 7);
        put32(bytes, header + 28, 1);
        bytes.insert(bytes.end(), payload.data.begin(), payload.data.end());
    }
    return bytes;
}

TEST(Stm32ElfTest, ScatterLoadedDataIsPlacedAtFlashLmaAndCopiedByActualStartupCode) {
    std::vector<uint8_t> code(0x118);
    put32(code, 0, Machine::kSramBase + Machine::kSramSize);
    put32(code, 4, Machine::kFlashBase + 0x101);
    put16(code, 0x100, 0x4803); // ldr r0,[pc,#12] -> Flash LMA
    put16(code, 0x102, 0x6801); // ldr r1,[r0]
    put16(code, 0x104, 0x4A03); // ldr r2,[pc,#12] -> SRAM VMA
    put16(code, 0x106, 0x6011); // str r1,[r2]
    put16(code, 0x108, 0xE7FE); // b . -- no debugger attached; BKPT would HardFault
    put32(code, 0x110, Machine::kFlashBase + 0x200);
    put32(code, 0x114, Machine::kSramBase);
    const std::vector payloads = {
        Payload{.vaddr = Machine::kFlashBase,
                .paddr = Machine::kFlashBase,
                .memsz = static_cast<uint32_t>(code.size()),
                .data = code},
        Payload{.vaddr = Machine::kSramBase,
                .paddr = Machine::kFlashBase + 0x200,
                .memsz = 16,
                .data = {0x78, 0x56, 0x34, 0x12}},
    };
    micro_forge::BasicEngine<Machine> engine(std::in_place);
    auto& machine = engine.machine();
    ASSERT_TRUE(machine.load_elf(elf(payloads)));
    EXPECT_EQ(machine.bus().read_u32(Machine::kFlashBase + 0x200), 0x12345678U);
    EXPECT_EQ(machine.bus().read_u32(Machine::kSramBase), 0U);
    EXPECT_EQ(machine.bus().read_u8(Machine::kFlashBase + 0x204), 0xFFU);
    ASSERT_TRUE(machine.reset());
    EXPECT_EQ(machine.cpu().pc(), Machine::kFlashBase + 0x100);
    EXPECT_EQ(engine.run(20).reason, micro_forge::StopReason::kBudgetExhausted);
    EXPECT_EQ(machine.bus().read_u32(Machine::kSramBase), 0x12345678U);
}

TEST(Stm32ElfTest, DirectSramSegmentZeroFillsOnlyItsOwnBss) {
    Machine machine;
    const std::vector<uint8_t> poison(12, 0xA5);
    ASSERT_TRUE(machine.load_image(Machine::kSramBase, poison));
    const std::vector payloads = {
        Payload{
            .vaddr = Machine::kSramBase, .paddr = Machine::kSramBase, .memsz = 8, .data = {1, 2}},
    };
    ASSERT_TRUE(machine.load_elf(elf(payloads)));
    EXPECT_EQ(machine.bus().read_u32(Machine::kSramBase), 0x00000201U);
    EXPECT_EQ(machine.bus().read_u32(Machine::kSramBase + 4), 0U);
    EXPECT_EQ(machine.bus().read_u32(Machine::kSramBase + 8), 0xA5A5A5A5U);
}

TEST(Stm32ElfTest, PhysicalAddressZeroIsARealFlashAliasNotAnUnsetAddressSentinel) {
    Machine machine;
    const std::vector payloads = {
        Payload{.vaddr = Machine::kSramBase, .paddr = 0, .memsz = 4, .data = {1, 2, 3, 4}},
    };
    ASSERT_TRUE(machine.load_elf(elf(payloads)));
    EXPECT_EQ(machine.bus().read_u32(Machine::kFlashBase), 0x04030201U);
    EXPECT_EQ(machine.bus().read_u32(Machine::kSramBase), 0U);
}

TEST(Stm32ElfTest, InvalidLaterSegmentDoesNotPartiallyProgramEarlierSegments) {
    Machine machine;
    const std::vector payloads = {
        Payload{
            .vaddr = Machine::kFlashBase, .paddr = Machine::kFlashBase, .memsz = 1, .data = {0xAA}},
        Payload{.vaddr = Machine::kSramBase, .paddr = 0x4001'3804, .memsz = 1, .data = {0x42}},
    };
    const auto result = machine.load_elf(elf(payloads));
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().kind, Error::Kind::kUnmappedImage);
    EXPECT_EQ(machine.bus().read_u8(Machine::kFlashBase), 0xFFU);
    EXPECT_TRUE(machine.console().empty());
}

TEST(Stm32ElfTest, RejectsOverlappingAliasAndPhysicalSegmentsBeforeMutation) {
    Machine machine;
    const std::vector payloads = {
        Payload{.vaddr = 0, .paddr = 0, .memsz = 1, .data = {1}},
        Payload{
            .vaddr = Machine::kFlashBase, .paddr = Machine::kFlashBase, .memsz = 1, .data = {2}},
    };
    const auto result = machine.load_elf(elf(payloads));
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().kind, Error::Kind::kInvalidSegment);
    EXPECT_EQ(machine.bus().read_u8(0), 0xFFU);
}

TEST(Stm32ElfTest, RejectsForeignMalformedTruncatedAndOverflowingProgramHeaders) {
    const std::vector payloads = {
        Payload{.vaddr = Machine::kFlashBase,
                .paddr = Machine::kFlashBase,
                .memsz = 4,
                .data = {1, 2, 3, 4}},
    };
    const auto valid = elf(payloads);
    for (const auto [offset, value] : std::vector<std::pair<std::size_t, uint8_t>>{
             {0, 0}, {4, 2}, {5, 2}, {18, 62}, {40, 51}, {42, 31}}) {
        Machine machine;
        auto invalid = valid;
        invalid[offset] = value;
        EXPECT_FALSE(machine.load_elf(invalid)) << offset;
        EXPECT_EQ(machine.bus().read_u8(0), 0xFFU);
    }
    Machine machine;
    EXPECT_FALSE(machine.load_elf(std::span(valid).first(51)));
    auto invalid = valid;
    put32(invalid, 28, 0xFFFF'FFF0);
    put16(invalid, 44, 0xFFFF);
    EXPECT_FALSE(machine.load_elf(invalid));
    invalid = valid;
    put32(invalid, 52 + 20, 3); // filesz > memsz
    EXPECT_FALSE(machine.load_elf(invalid));
    invalid = valid;
    put32(invalid, 52 + 28, 3); // non-power-of-two alignment
    EXPECT_FALSE(machine.load_elf(invalid));
    invalid = valid;
    invalid.pop_back();
    EXPECT_FALSE(machine.load_elf(invalid));
}

} // namespace
