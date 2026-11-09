/// @file test_jit.cpp
/// @brief Copy-and-patch skeleton: emit a template, seal, invoke, verify.

#include <gtest/gtest.h>

#include <micro_forge/arch/x86_64/decoder.hpp>
#include <micro_forge/base/jit.hpp>
#include <micro_forge/base/jit_translate.hpp>

#include <array>

namespace {

using micro_forge::base::JitPage;
using micro_forge::base::JitTemplate;

TEST(JitTest, MovAlImm8PatchesTheHole) {
    auto page = JitPage::create();
    ASSERT_TRUE(page.has_value());
    const auto spec = micro_forge::base::template_spec(JitTemplate::kMovAlImm8);
    const auto offset = page->emit(spec.bytes, spec.hole_offset, spec.hole_size, 0x41);
    ASSERT_TRUE(offset.has_value());
    ASSERT_TRUE(page->seal());

    std::array<std::uint64_t, 16> regs{};
    page->invoke(*offset, regs.data());
    EXPECT_EQ(0x41U, regs[0] & 0xFFU); // guest AL
}

TEST(JitTest, AddRaxImm32RunsTwice) {
    auto page = JitPage::create();
    ASSERT_TRUE(page.has_value());
    const auto spec = micro_forge::base::template_spec(JitTemplate::kAddRaxImm32);
    const auto offset = page->emit(spec.bytes, spec.hole_offset, spec.hole_size, 5);
    ASSERT_TRUE(offset.has_value());
    ASSERT_TRUE(page->seal());

    std::array<std::uint64_t, 16> regs{};
    page->invoke(*offset, regs.data());
    page->invoke(*offset, regs.data());
    EXPECT_EQ(10U, regs[0]);
}

TEST(JitTest, EmitRejectsAfterSeal) {
    auto page = JitPage::create();
    ASSERT_TRUE(page.has_value());
    ASSERT_TRUE(page->seal());
    const auto spec = micro_forge::base::template_spec(JitTemplate::kMovAlImm8);
    EXPECT_FALSE(page->emit(spec.bytes, spec.hole_offset, spec.hole_size, 1).has_value());
}

TEST(JitTest, BlockOfTemplatesRunsAsOneCall) {
    // A block is several templates laid out back to back: move the guest
    // RAX slot to 1 and add 2 to it, then return. Offsets 0 and 8 in the
    // register file are RDI-relative displacements patched per template.
    auto page = JitPage::create();
    ASSERT_TRUE(page.has_value());
    // SIB 0x27 = scale 1, index none, base rdi (0x07 would index RAX).
    // mov qword [rdi+disp32], imm32  = 48 C7 84 27 <disp32> <imm32> (12 bytes)
    static constexpr std::array<std::uint8_t, 12> mov_slot = {
        0x48, 0xC7, 0x84, 0x27, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    };
    // add qword [rdi+disp32], imm32  = 48 81 84 27 <disp32> <imm32> (12 bytes)
    static constexpr std::array<std::uint8_t, 12> add_slot = {
        0x48, 0x81, 0x84, 0x27, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    };
    const std::array<JitPage::Patch, 2> mov_patches = {
        JitPage::Patch{.offset = 4, .size = 4, .value = 0},
        JitPage::Patch{.offset = 8, .size = 4, .value = 1},
    };
    const std::array<JitPage::Patch, 2> add_patches = {
        JitPage::Patch{.offset = 4, .size = 4, .value = 0},
        JitPage::Patch{.offset = 8, .size = 4, .value = 2},
    };
    const auto entry = page->emit_multi(mov_slot, mov_patches);
    ASSERT_TRUE(entry.has_value());
    ASSERT_TRUE(page->emit_multi(add_slot, add_patches).has_value());
    const std::array<std::uint8_t, 1> ret = {0xC3};
    ASSERT_TRUE(page->emit_multi(ret, {}).has_value());
    ASSERT_TRUE(page->seal());

    std::array<std::uint64_t, 16> regs{};
    page->invoke(*entry, regs.data());
    EXPECT_EQ(3U, regs[0]) << "bytes: " << std::hex << [&] {
        std::string dump;
        for (std::size_t i = 0; i < 25; ++i) {
            dump += " " + std::to_string(page->base()[i]);
        }
        return dump;
    }();
}

TEST(JitTest, TranslatesDecodedGuestCode) {
    // Guest bytes: mov rax,1 ; mov rax,2 -- both lower to templates.
    static constexpr std::array<std::uint8_t, 14> guest = {
        0x48, 0xC7, 0xC0, 0x01, 0x00, 0x00, 0x00, 0x48, 0xC7, 0xC0, 0x02, 0x00, 0x00, 0x00,
    };
    auto page = JitPage::create();
    ASSERT_TRUE(page.has_value());
    std::size_t pc = 0;
    std::optional<std::size_t> entry;
    while (pc < guest.size()) {
        const auto decoded = micro_forge::arch::x86_64::decode_one(
            std::span<const std::uint8_t>(guest.data() + pc, guest.size() - pc));
        ASSERT_TRUE(decoded.has_value());
        const auto lowered = micro_forge::base::translate_insn(*decoded);
        if (!lowered.has_value()) {
            // ret is interpreter-only; the block ends here.
            break;
        }
        const auto off = page->emit_multi(lowered->code, lowered->patches);
        ASSERT_TRUE(off.has_value());
        if (!entry.has_value()) {
            entry = off;
        }
        pc += decoded->length;
    }
    // The block ends with an explicit ret.
    const std::array<std::uint8_t, 1> ret_byte = {0xC3};
    ASSERT_TRUE(page->emit_multi(ret_byte, {}).has_value());
    ASSERT_TRUE(entry.has_value());
    ASSERT_TRUE(page->seal());
    std::array<std::uint64_t, 16> regs{};
    page->invoke(*entry, regs.data());
    EXPECT_EQ(2U, regs[0]); // second mov wins
}

} // namespace
