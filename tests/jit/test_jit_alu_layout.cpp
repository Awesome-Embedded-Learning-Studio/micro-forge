// NOLINTBEGIN -- test scaffolding and explicit machine-code contracts
/// @file test_jit_alu_layout.cpp
/// @brief ALU template byte layouts and conservative lowering gates.

#include <gtest/gtest.h>

#include <micro_forge/arch/x86_64/decoder.hpp>
#include <micro_forge/base/jit_translate.hpp>

#include <array>
#include <limits>
#include <vector>

namespace {

using namespace micro_forge::arch::x86_64;
using micro_forge::base::JitHelper;
using micro_forge::base::translate_insn;

TEST(JitAluLayoutTest, ChecksEveryHoleAndRelocation) {
    struct Layout {
        std::vector<uint8_t> bytes;
        std::size_t length, cpu, helper;
        std::vector<std::size_t> offsets;
        std::vector<uint64_t> values;
    };
    for (const bool wide : {false, true}) {
        const uint8_t rex = wide ? 0x4D : 0x45;
        const uint64_t width = wide ? 8 : 4;
        for (const auto& sample :
             std::vector<Layout>{{{static_cast<uint8_t>(wide ? 0x49 : 0x41), 0x83, 0xF9, 0xFF},
                                  45,
                                  23,
                                  33,
                                  {2, 8, 17},
                                  {72, UINT64_MAX, width}},
                                 {{rex, 0x39, 0xD1}, 43, 21, 31, {2, 10, 15}, {72, 80, width}},
                                 {{rex, 0x3B, 0x4C, 0x24, 0xB8},
                                  50,
                                  28,
                                  38,
                                  {5, 12, 17, 22},
                                  {96, 0xFFFFFFB8, 72, width}},
                                 {{rex, 0x3B, 0x4C, 0xCC, 0xB8},
                                  65,
                                  43,
                                  53,
                                  {5, 13, 20, 27, 32, 37},
                                  {96, 8, 3, 0xFFFFFFB8, 72, width}}}) {
            const auto decoded = decode_one(sample.bytes);
            ASSERT_TRUE(decoded);
            const auto lowered = translate_insn(*decoded);
            ASSERT_TRUE(lowered);
            ASSERT_TRUE(lowered->has_valid_layout());
            // Length, cpu/helper offsets and hole positions are the
            // assembler's now; asserting them here would be another hand-
            // maintained copy. What still matters is that every patch lands
            // a semantic value, and that a duplicated or overlapping hole is
            // rejected.
            EXPECT_EQ(0x57, lowered->code.front());
            EXPECT_EQ(0x5F, lowered->code.back());
            ASSERT_EQ(sample.values.size(), lowered->patches.size());
            for (std::size_t i = 0; i < sample.values.size(); ++i) {
                const auto& patch = lowered->patches[i];
                EXPECT_EQ(sample.values[i], patch.value);
                auto bad = *lowered;
                bad.patches.push_back(patch);
                EXPECT_FALSE(bad.has_valid_layout());
            }
            for (const auto offset : {*lowered->cpu_offset, *lowered->call_offset}) {
                auto bad = *lowered;
                bad.patches.push_back({.offset = offset, .size = 8, .value = 0});
                EXPECT_FALSE(bad.has_valid_layout());
            }
        }
    }
}

TEST(JitAluLayoutTest, RejectsUnsupportedRegisterAndImmediateForms) {
    const auto decoded = decode_one(std::array<uint8_t, 3>{0x4D, 0x39, 0xD1});
    ASSERT_TRUE(decoded);
    for (const auto mnemonic : {Mnemonic::kCmp, Mnemonic::kTest, Mnemonic::kSub, Mnemonic::kAnd,
                                Mnemonic::kOr, Mnemonic::kXor}) {
        auto good = *decoded;
        good.mnemonic = mnemonic;
        ASSERT_TRUE(translate_insn(good));
        for (const uint8_t width : {0, 3, 16}) {
            auto bad = good;
            bad.ops[0].size = bad.ops[1].size = width;
            EXPECT_FALSE(translate_insn(bad));
        }
        // Byte/word widths lower since the helpers mask to width.
        for (const uint8_t width : {1, 2}) {
            auto small = good;
            small.ops[0].size = small.ops[1].size = width;
            EXPECT_TRUE(translate_insn(small));
        }
        for (const unsigned operand : {0U, 1U}) {
            auto bad = good;
            bad.ops[operand].reg = Reg::kXmm0;
            EXPECT_FALSE(translate_insn(bad));
        }
        auto bad = good;
        bad.ops[1].size = 4;
        EXPECT_FALSE(translate_insn(bad));
        bad = good;
        bad.has_lock = true;
        EXPECT_FALSE(translate_insn(bad));
        good.ops[1].kind = Operand::Kind::kImm;
        for (const auto imm :
             {static_cast<int64_t>(INT32_MIN) - 1, static_cast<int64_t>(INT32_MAX) + 1}) {
            bad = good;
            bad.ops[1].disp = imm;
            EXPECT_FALSE(translate_insn(bad));
        }
    }
}

TEST(JitAluLayoutTest, RejectsUnsupportedMemorySources) {
    const auto decoded = decode_one(std::array<uint8_t, 5>{0x4D, 0x3B, 0x4C, 0xCC, 0xB8});
    ASSERT_TRUE(decoded);
    ASSERT_EQ(JitHelper::kCmpMem, translate_insn(*decoded)->helper);
    for (const uint8_t scale : {0, 3, 16}) {
        auto bad = *decoded;
        bad.ops[1].scale = scale;
        EXPECT_FALSE(translate_insn(bad));
    }
    for (const auto mnemonic :
         {Mnemonic::kTest, Mnemonic::kSub, Mnemonic::kAnd, Mnemonic::kOr, Mnemonic::kXor}) {
        auto bad = *decoded;
        bad.mnemonic = mnemonic;
        EXPECT_FALSE(translate_insn(bad));
    }
    for (unsigned form = 0; form < 12; ++form) {
        auto bad = *decoded;
        switch (form) {
        case 0:
            bad.ops[1].base = Reg::kCr0;
            break;
        case 1:
            bad.ops[1].index = Reg::kXmm0;
            break;
        case 2:
            bad.ops[1].has_base = false;
            break;
        case 3:
            bad.ops[1].rip_relative = true;
            break;
        case 4:
            bad.ops[1].disp = static_cast<int64_t>(INT32_MAX) + 1;
            break;
        case 5:
            bad.ops[1].disp = static_cast<int64_t>(INT32_MIN) - 1;
            break;
        case 6:
            bad.addr_size = 4;
            break;
        case 7:
            bad.seg = Seg::kFs;
            break;
        case 8:
            bad.seg = Seg::kGs;
            break;
        case 9:
            bad.has_lock = true;
            break;
        case 10:
            bad.ops[1].size = 4;
            break;
        default:
            bad.ops[0].kind = Operand::Kind::kMem;
            break;
        }
        EXPECT_FALSE(translate_insn(bad)) << form;
    }
}

} // namespace

// NOLINTEND
