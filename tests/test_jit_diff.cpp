// NOLINTBEGIN -- test scaffolding: fork/setenv/env capture are deliberate
/// @file test_jit_diff.cpp
/// @brief Native JIT/interpreter differential tests with process isolation.

#include "jit_diff_cases.hpp"
#include "jit_diff_support.hpp"
#include <micro_forge/base/jit_templates_generated.hpp>

namespace {

using namespace jit_diff;
using jit_diff_cases::add_mem_case;
using jit_diff_cases::alu_cases;
using jit_diff_cases::indexed_case;
using jit_diff_cases::width_cases;

class JitDiffTest : public ::testing::TestWithParam<DiffCase> {};

TEST_P(JitDiffTest, MatchesInterpreter) {
    const auto& test = GetParam();
    const auto decoded = micro_forge::arch::x86_64::decode_one(test.code);
    ASSERT_TRUE(decoded);
    const auto lowered = translate_insn(*decoded);
    if (test.require_lowered) {
        ASSERT_TRUE(lowered);
    } else {
        ASSERT_FALSE(lowered); // form must stay with the interpreter
        return;
    }
    ASSERT_TRUE(lowered->has_valid_layout());
    if (lowered->helper == micro_forge::base::JitHelper::kAdd) {
        EXPECT_EQ(40U, lowered->code.size());
        EXPECT_EQ(18U, lowered->cpu_offset);
        EXPECT_EQ(28U, lowered->call_offset);
        EXPECT_EQ(decoded->ops[0].size, lowered->patches.back().value);
    }
    State interpreter{}, jit{};
    ASSERT_NO_FATAL_FAILURE(run_child(test, false, interpreter));
    ASSERT_NO_FATAL_FAILURE(run_child(test, true, jit));
    EXPECT_EQ(0U, interpreter.blocks);
    EXPECT_EQ(0U, interpreter.insns);
    ASSERT_GT(jit.blocks, 0U);
    ASSERT_GT(jit.insns, 0U);
    // Only the instruction under test can compile; no padding mov passes.
    EXPECT_EQ(1U, jit.blocks);
    EXPECT_EQ(test.jit_insns, jit.insns);
    EXPECT_EQ(interpreter.exit, jit.exit);
    EXPECT_EQ(interpreter.vec[0], jit.vec[0]) << "XMM low";
    EXPECT_EQ(interpreter.vec[1], jit.vec[1]) << "XMM high";
    EXPECT_EQ(interpreter.rip, jit.rip);
    EXPECT_EQ(interpreter.rflags, jit.rflags);
    for (unsigned i = 0; i < jit.gprs.size(); ++i) {
        EXPECT_EQ(interpreter.gprs[i], jit.gprs[i]) << "GPR " << i;
    }
    constexpr std::array flag_names = {"CF", "OF", "SF", "ZF", "AF", "PF"};
    for (unsigned i = 0; i < jit.flags.size(); ++i) {
        EXPECT_EQ(interpreter.flags[i], jit.flags[i]) << flag_names[i];
    }
    EXPECT_EQ(interpreter.memory, jit.memory);
}

INSTANTIATE_TEST_SUITE_P(
    Native, JitDiffTest,
    ::testing::Values(
        DiffCase{"MovImm32", {0x49, 0xC7, 0xC1, 0x78, 0x56, 0x34, 0x12}, 0, 0x12345678},
        DiffCase{"MovNegativeImm32", {0x49, 0xC7, 0xC1, 0xFF, 0xFF, 0xFF, 0xFF}, 0, UINT64_MAX},
        DiffCase{"AddPositive", {0x49, 0x81, 0xC1, 0x01, 0x01, 0x00, 0x00}, 0xF, 0x110, 0x10},
        DiffCase{
            "AddNegative", {0x49, 0x81, 0xC1, 0x00, 0xFF, 0xFF, 0xFF}, 5, UINT64_MAX - 250, 0x84},
        DiffCase{"AddCarry", {0x49, 0x81, 0xC1, 1, 0, 0, 0}, UINT64_MAX, 0, 0x55},
        DiffCase{"AddOverflow",
                 {0x49, 0x81, 0xC1, 1, 0, 0, 0},
                 0x7FFFFFFFFFFFFFFFULL,
                 0x8000000000000000ULL,
                 0x894},
        DiffCase{"AddNegativeOverflow",
                 {0x49, 0x81, 0xC1, 0xFF, 0xFF, 0xFF, 0xFF},
                 0x8000000000000000ULL,
                 0x7FFFFFFFFFFFFFFFULL,
                 0x805},
        DiffCase{"MovRegReg", {0x4D, 0x89, 0xD1}, 0, kStored}, // mov r9,r10
        DiffCase{"LeaBaseDisp", {0x4D, 0x8D, 0x8C, 0x24, 0xB8, 0xFF, 0xFF, 0xFF}, 0, kData},
        DiffCase{"LoadBaseDisp", {0x4D, 0x8B, 0x8C, 0x24, 0xB8, 0xFF, 0xFF, 0xFF}, 0, kLoaded},
        DiffCase{"LoadRipDisp", {0x4C, 0x8B, 0x0D, 0xF9, 0x2F, 0, 0}, 0, kLoaded},
        DiffCase{
            "StoreBaseDisp", {0x4D, 0x89, 0x94, 0x24, 0xB8, 0xFF, 0xFF, 0xFF}, 0, 0, kFlags, true},
        // Exact requested forms: mov rax,[rbx+rcx*8+0x10] and the scale-4 store.
        indexed_case("LoadIdxScale8", {0x48, 0x8B, 0x44, 0xCB, 0x10}, false, kData - 0x28),
        indexed_case("StoreIdxScale4", {0x48, 0x89, 0x44, 0x8B, 0x20}, true, kData - 0x2C),
        indexed_case("LoadIdxScale4NoDisp", {0x48, 0x8B, 0x04, 0x8B}, false, kData - 12),
        indexed_case("StoreIdxScale1NoDisp", {0x48, 0x89, 0x04, 0x0B}, true, kData - 3),
        indexed_case("LoadIdxScale2NegativeDisp", {0x48, 0x8B, 0x44, 0x4B, 0xE0}, false,
                     kData + 0x20 - 6),
        indexed_case("StoreIdxScale8NegativeDisp", {0x48, 0x89, 0x44, 0xCB, 0xE0}, true,
                     kData + 0x20 - 24),
        indexed_case("LoadIdxScale1AliasedNegativeIndex", {0x48, 0x8B, 0x04, 0x03}, false,
                     kData + 3, UINT64_MAX - 2, Reg::kRax),
        indexed_case("StoreIdxScale2HighIndexReg", {0x4A, 0x89, 0x04, 0x6B}, true, kData - 6, 3,
                     Reg::kR13),
        add_mem_case("AddMemCarry", UINT64_MAX, 1, 0, 0x55),
        add_mem_case("AddMemOverflow", 0x7FFFFFFFFFFFFFFFULL, 1, 0x8000000000000000ULL, 0x894),
        add_mem_case("AddMemNegativeOverflow", 0x8000000000000000ULL, UINT64_MAX,
                     0x7FFFFFFFFFFFFFFFULL, 0x805),
        add_mem_case("AddMemAuxCarry", 15, 1, 16, 0x10),
        add_mem_case("AddMemNoAuxCarry", 7, 1, 8, 0),
        add_mem_case("AddMemNoDisp", 2, 0x100000001ULL, 0x100000003ULL, 4, kData,
                     {0x4D, 0x01, 0x14, 0x24}),
        add_mem_case("AddMemPositiveDisp8", 0, 0, 0, 0x44, kData - 0x48,
                     {0x4D, 0x01, 0x54, 0x24, 0x48}),
        add_mem_case("AddMemAliasedBase", 0, kStored, kData + 0x48, 4, kData + 0x48,
                     {0x4D, 0x01, 0x64, 0x24, 0xB8})),
    [](const ::testing::TestParamInfo<DiffCase>& info) { return info.param.name; });

INSTANTIATE_TEST_SUITE_P(Widths, JitDiffTest, ::testing::ValuesIn(width_cases()),
                         [](const ::testing::TestParamInfo<DiffCase>& info) {
                             return info.param.name;
                         });

INSTANTIATE_TEST_SUITE_P(Alu, JitDiffTest, ::testing::ValuesIn(alu_cases()),
                         [](const ::testing::TestParamInfo<DiffCase>& info) {
                             return info.param.name;
                         });

TEST(JitLayoutTest, RejectsBadRelocationsAndPatches) {
    const auto decoded = micro_forge::arch::x86_64::decode_one(
        std::array<uint8_t, 4>{0x48, 0x8B, 0x48, 8}); // mov rcx,[rax+8]
    ASSERT_TRUE(decoded);
    const auto good = translate_insn(*decoded);
    ASSERT_TRUE(good);
    ASSERT_TRUE(good->has_valid_layout());

    auto bad = *good;
    bad.call_offset = *good->call_offset + 1; // rel32 is not E8
    EXPECT_FALSE(bad.has_valid_layout());
    bad = *good;
    bad.cpu_offset = *good->cpu_offset - 1; // immediate must follow 48 BF
    EXPECT_FALSE(bad.has_valid_layout());
    bad = *good;
    bad.cpu_offset = 1; // guard subtraction before reading opcode bytes
    EXPECT_FALSE(bad.has_valid_layout());
    bad = *good;
    bad.call_offset = std::numeric_limits<std::size_t>::max();
    EXPECT_FALSE(bad.has_valid_layout());
    bad = *good;
    bad.cpu_offset.reset();
    EXPECT_FALSE(bad.has_valid_layout());
    bad = *good;
    bad.patches[0].offset = 4; // SIB byte, not the displacement
    EXPECT_FALSE(bad.has_valid_layout());
    bad = *good;
    bad.patches[0].offset = std::numeric_limits<std::size_t>::max();
    EXPECT_FALSE(bad.has_valid_layout());
    bad = *good;
    bad.patches[0].size = 0;
    EXPECT_FALSE(bad.has_valid_layout());
    bad = *good;
    bad.patches.push_back(good->patches.front());
    EXPECT_FALSE(bad.has_valid_layout());
    bad = *good;
    bad.patches[0].offset = *good->cpu_offset; // overlaps Cpu* relocation
    EXPECT_FALSE(bad.has_valid_layout());
}

TEST(JitLayoutTest, RejectsOriginalLeaAndStoreOffsets) {
    const auto lea =
        micro_forge::arch::x86_64::decode_one(std::array<uint8_t, 4>{0x48, 0x8D, 0x48, 8});
    const auto store =
        micro_forge::arch::x86_64::decode_one(std::array<uint8_t, 4>{0x48, 0x89, 0x48, 8});
    ASSERT_TRUE(lea);
    ASSERT_TRUE(store);
    auto bad_lea = translate_insn(*lea);
    auto bad_store = translate_insn(*store);
    ASSERT_TRUE(bad_lea);
    ASSERT_TRUE(bad_store);
    ASSERT_TRUE(bad_lea->has_valid_layout());
    ASSERT_TRUE(bad_store->has_valid_layout());
    bad_lea->patches.back().offset = 16;
    EXPECT_FALSE(bad_lea->has_valid_layout());
    bad_store->call_offset = 36;
    EXPECT_FALSE(bad_store->has_valid_layout());
    bad_store->call_offset = 39;
    bad_store->patches.back().offset = 32;
    EXPECT_FALSE(bad_store->has_valid_layout());
}

TEST(JitLayoutTest, UnsupportedMemoryFormDoesNotBecomeImmediateMov) {
    const auto indexed = micro_forge::arch::x86_64::decode_one(
        std::array<uint8_t, 8>{0x48, 0x8B, 0x04, 0xC5, 0, 0, 0, 0}); // no base
    ASSERT_TRUE(indexed);
    EXPECT_FALSE(translate_insn(*indexed));
}

TEST(JitLayoutTest, IndexedMovValidatesLayoutsAndRejectsUnsupportedForms) {
    for (const bool store : {false, true}) {
        const std::array<uint8_t, 5> bytes = {0x48, static_cast<uint8_t>(store ? 0x89 : 0x8B), 0x44,
                                              0xCB, 0x10};
        const auto decoded = micro_forge::arch::x86_64::decode_one(bytes);
        ASSERT_TRUE(decoded);
        const auto memory = store ? 0U : 1U;
        for (const uint8_t scale : {1, 2, 4, 8}) {
            auto insn = *decoded;
            insn.ops[memory].scale = scale;
            const auto lowered = translate_insn(insn);
            ASSERT_TRUE(lowered);
            EXPECT_TRUE(lowered->has_valid_layout());
            EXPECT_EQ(68U, lowered->code.size());
            EXPECT_EQ(store ? 56U : 48U, lowered->call_offset);
            EXPECT_EQ(store ? 41U : 33U, lowered->cpu_offset);
            auto bad = *lowered;
            bad.patches.push_back({.offset = store ? 28U : 20U, .size = 1, .value = 0});
            EXPECT_FALSE(bad.has_valid_layout()); // shift8 already occupies this byte
        }
        for (const uint8_t scale : {0, 3, 16}) {
            auto insn = *decoded;
            insn.ops[memory].scale = scale;
            EXPECT_FALSE(translate_insn(insn));
        }
        auto insn = *decoded;
        insn.ops[memory].index = Reg::kXmm0;
        EXPECT_FALSE(translate_insn(insn));
        insn = *decoded;
        insn.ops[memory].base = Reg::kCr0;
        EXPECT_FALSE(translate_insn(insn));
        insn = *decoded;
        insn.ops[memory].disp = static_cast<int64_t>(INT32_MAX) + 1;
        EXPECT_FALSE(translate_insn(insn));
        // Unsupported prefixes must leave these with the interpreter.
        for (const std::vector<uint8_t>& prefixed :
             {std::vector<uint8_t>{0x64}, {0x65}, {0x67}, {0xF0}}) {
            auto code = prefixed;
            code.insert(code.end(), bytes.begin(), bytes.end());
            const auto unsupported = micro_forge::arch::x86_64::decode_one(code);
            if (unsupported) {
                EXPECT_FALSE(translate_insn(*unsupported));
            }
        }
        const auto dword =
            micro_forge::arch::x86_64::decode_one(std::span<const uint8_t>(bytes).subspan(1));
        ASSERT_TRUE(dword);
        ASSERT_TRUE(translate_insn(*dword));
        EXPECT_TRUE(translate_insn(*dword)->has_valid_layout());
        for (const uint8_t width : {0, 3, 16}) {
            insn = *decoded;
            insn.ops[0].size = insn.ops[1].size = width;
            EXPECT_FALSE(translate_insn(insn));
        }
    }
}

TEST(JitLayoutTest, AddMemHasValidFortyEightByteLayout) {
    const auto decoded = micro_forge::arch::x86_64::decode_one(
        std::array<uint8_t, 8>{0x4D, 0x01, 0x94, 0x24, 0xB8, 0xFF, 0xFF, 0xFF});
    ASSERT_TRUE(decoded);
    const auto lowered = translate_insn(*decoded);
    ASSERT_TRUE(lowered);
    ASSERT_TRUE(lowered->has_valid_layout());
    EXPECT_EQ(micro_forge::base::JitHelper::kAddMem, lowered->helper);
    // Length and offsets come from the generated template, not from literals:
    // the assembler picked a shorter encoding, and copying byte offsets into
    // the test is the same mistake this migration is removing.
    namespace jg = micro_forge::base::jit_gen;
    EXPECT_EQ(jg::kAddMemSlot.size(), lowered->code.size());
    EXPECT_EQ(jg::hole_patch(jg::kAddMemSlotHoles, jg::HoleKind::kCpu64, 0).offset,
              lowered->cpu_offset);
    EXPECT_EQ(jg::hole_patch(jg::kAddMemSlotHoles, jg::HoleKind::kHelper64, 0).offset,
              lowered->call_offset);
    ASSERT_EQ(3U, lowered->patches.size());
    constexpr std::array<jg::HoleKind, 3> kinds = {jg::HoleKind::kSrc32, jg::HoleKind::kBase32,
                                                   jg::HoleKind::kDisp32};
    constexpr std::array<std::uint64_t, 3> values = {80, 96, 0xFFFFFFB8};
    for (std::size_t i = 0; i < kinds.size(); ++i) {
        EXPECT_EQ(jg::hole_patch(jg::kAddMemSlotHoles, kinds[i], 0).offset,
                  lowered->patches[i].offset);
        EXPECT_EQ(4U, lowered->patches[i].size);
        EXPECT_EQ(values[i], lowered->patches[i].value);
    }
}

TEST(JitLayoutTest, AddMemRejectsUnsupportedForms) {
    const std::vector<std::vector<uint8_t>> unsupported = {
        {0x01, 0x08},                         // add dword [rax],ecx
        {0x66, 0x01, 0x08},                   // add word [rax],cx
        {0x00, 0x08},                         // add byte [rax],cl
        {0x48, 0x01, 0x0C, 0xD8},             // add [rax+rbx*8],rcx
        {0x48, 0x01, 0x0D, 0, 0, 0, 0},       // add [rip],rcx
        {0x48, 0x01, 0x0C, 0x25, 0, 0, 0, 0}, // add [abs32],rcx
        {0x64, 0x48, 0x01, 0x08},             // FS base is not in the ABI
        {0x65, 0x48, 0x01, 0x08},             // GS base is not in the ABI
        {0xF0, 0x48, 0x01, 0x08},             // lock add
    };
    for (const auto& bytes : unsupported) {
        const auto decoded = micro_forge::arch::x86_64::decode_one(bytes);
        ASSERT_TRUE(decoded);
        EXPECT_FALSE(translate_insn(*decoded));
    }
    const auto decoded =
        micro_forge::arch::x86_64::decode_one(std::array<uint8_t, 3>{0x48, 0x01, 0x08});
    ASSERT_TRUE(decoded);
    auto insn = *decoded;
    insn.ops[0].base = Reg::kXmm0;
    EXPECT_FALSE(translate_insn(insn));
    insn = *decoded;
    insn.ops[1].reg = Reg::kCr0;
    EXPECT_FALSE(translate_insn(insn));
    insn = *decoded;
    insn.addr_size = 4;
    EXPECT_FALSE(translate_insn(insn));
}

} // namespace

// NOLINTEND
