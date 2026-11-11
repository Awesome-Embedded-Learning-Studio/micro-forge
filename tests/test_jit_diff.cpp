// NOLINTBEGIN -- test scaffolding: fork/setenv/env capture are deliberate
/// @file test_jit_diff.cpp
/// @brief Native JIT/interpreter differential tests with process isolation.

#include "jit_diff_support.hpp"

namespace {

using namespace jit_diff;

DiffCase indexed_case(const char* name, std::vector<uint8_t> code, bool store, uint64_t base,
                      uint64_t index = 3, Reg index_reg = Reg::kRcx) {
    return {.name = name,
            .code = std::move(code),
            .initial = 0,
            .expected = store ? 0 : kLoaded,
            .store = store,
            .base = base,
            .result_reg = store ? Reg::kR9 : Reg::kRax,
            .base_reg = Reg::kRbx,
            .source_reg = Reg::kRax,
            .index = index,
            .index_reg = index_reg};
}

DiffCase add_mem_case(const char* name, uint64_t initial, uint64_t source, uint64_t expected,
                      uint64_t flags, uint64_t base = kData + 0x48,
                      std::vector<uint8_t> code = {0x4D, 0x01, 0x94, 0x24, 0xB8, 0xFF, 0xFF,
                                                   0xFF}) {
    return {name, std::move(code), 0, 0, flags, false, initial, source, expected, base};
}

std::vector<DiffCase> width_cases() {
    std::vector<DiffCase> cases;
    constexpr uint64_t dirty = 0xABCDEF1289ABCDEFULL;
    constexpr std::array<uint64_t, 4> loaded = {0xABCDEF1289ABCD21ULL, 0xABCDEF1289AB4321ULL,
                                                0x87654321, 0xDEADBEEF87654321ULL};
    constexpr std::array<uint64_t, 4> stored = {0xFEDCBA9876543211ULL, 0xFEDCBA9876542211ULL,
                                                0xFEDCBA9844332211ULL, kStored};
    for (unsigned w = 0; w < 4; ++w) {
        const unsigned width = 1U << w;
        for (unsigned form = 0; form < 3; ++form) {
            for (const bool store : {false, true}) {
                const auto name = std::string(store ? "Store" : "Load") + std::to_string(width) +
                                  "Form" + std::to_string(form);
                std::vector<uint8_t> code;
                if (width == 2 || width == 8) {
                    code.push_back(width == 2 ? 0x66 : 0x48);
                }
                code.push_back(width == 1 ? (store ? 0x88 : 0x8A) : (store ? 0x89 : 0x8B));
                code.push_back(form == 0 ? 0x03 : (form == 1 ? 0x43 : 0x44));
                if (form == 2) {
                    code.push_back(0x8B); // [rbx+rcx*4-32]
                }
                if (form != 0) {
                    code.push_back(form == 1 ? 0xF0 : 0xE0);
                }
                auto test = indexed_case(name.c_str(), std::move(code), store,
                                         form == 0 ? kData : (form == 1 ? kData + 16 : kData + 20));
                test.initial = dirty;
                test.expected = store ? dirty : loaded[w];
                test.source_reg = store ? Reg::kRax : Reg::kR10;
                test.memory_initial = store ? kLoaded : loaded.back();
                test.memory_expected = store ? stored[w] : loaded.back();
                cases.push_back(std::move(test));
            }
        }
    }
    for (const bool rex : {false, true}) {
        auto load = indexed_case(rex ? "LoadSpl" : "LoadAh", {0x8A, 0x23}, false, kData);
        load.index.reset();
        load.initial = dirty;
        load.expected = rex ? 0xABCDEF1289ABCD10ULL : 0xABCDEF1289AB10EFULL;
        load.result_reg = rex ? Reg::kRsp : Reg::kRax;
        load.source_reg = Reg::kR10;
        auto store = indexed_case(rex ? "StoreSpl" : "StoreAh", {0x88, 0x23}, true, kData);
        store.source_reg = rex ? Reg::kRsp : Reg::kRax;
        store.memory_expected = rex ? 0xFEDCBA9876543211ULL : 0xFEDCBA9876543222ULL;
        if (rex) {
            load.code.insert(load.code.begin(), 0x40);
            store.code.insert(store.code.begin(), 0x40);
        }
        cases.push_back(std::move(load));
        cases.push_back(std::move(store));
    }
    const auto append = [&](const char* name, std::vector<uint8_t> code, uint64_t initial,
                            uint64_t expected, uint64_t flags = kFlags) {
        cases.push_back({name, std::move(code), initial, expected, flags});
    };
    append("Add32CarryZero", {0x41, 0x81, 0xC1, 1, 0, 0, 0}, UINT64_MAX, 0, 0x55);
    append("Add32Overflow", {0x41, 0x81, 0xC1, 1, 0, 0, 0}, 0xABCDEF127FFFFFFFULL, 0x80000000,
           0x894);
    append("Add32NegativeOverflow", {0x41, 0x81, 0xC1, 0xFF, 0xFF, 0xFF, 0xFF},
           0xABCDEF1280000000ULL, 0x7FFFFFFF, 0x805);
    append("Add32NegativeImm8", {0x41, 0x83, 0xC1, 0xFF}, 0xABCDEF1200000001ULL, 0, 0x55);
    append("Add32SignBitImm", {0x41, 0x81, 0xC1, 0, 0, 0, 0x80}, 0xABCDEF1200000000ULL, 0x80000000,
           0x84);
    append("Add32ClearsFlags", {0x41, 0x83, 0xC1, 1}, dirty & ~0xFFFFFFFFULL, 1, 0);
    append("Mov32Zero", {0x41, 0xB9, 0, 0, 0, 0}, UINT64_MAX, 0);
    append("Mov32Unsigned", {0x41, 0xB9, 0xFF, 0xFF, 0xFF, 0xFF}, UINT64_MAX, UINT32_MAX);
    append("Mov32C7SignBit", {0x41, 0xC7, 0xC1, 0, 0, 0, 0x80}, UINT64_MAX, 0x80000000);
    auto hot = indexed_case("HotLoopPrefix",
                            {0x8B, 0x30, 0x83, 0xC2, 1, 0x89, 0x34, 0x88, 0x48, 0x83, 0xC0, 4},
                            false, kData, 0);
    hot.result_reg = hot.base_reg = Reg::kRax;
    hot.source_reg = Reg::kRdx;
    hot.source = UINT64_MAX;
    hot.expected = kData + 4;
    hot.flags = 0;
    hot.jit_insns = 4;
    cases.push_back(std::move(hot));
    return cases;
}

class JitDiffTest : public ::testing::TestWithParam<DiffCase> {};

std::vector<DiffCase> alu_cases() {
    std::vector<DiffCase> cases;
    constexpr std::array names = {"Cmp", "Sub", "Test", "And", "Or", "Xor"};
    constexpr std::array<uint8_t, 6> opcodes = {0x39, 0x29, 0x85, 0x21, 0x09, 0x31};
    constexpr std::array<uint8_t, 6> extensions = {7, 5, 0, 4, 1, 6};
    for (const unsigned width : {4U, 8U}) {
        const uint64_t sign = uint64_t{1} << (width * 8 - 1);
        const uint64_t mask = width == 4 ? UINT32_MAX : UINT64_MAX;
        const uint64_t dirty = width == 4 ? 0xABCDEF1200000000ULL : 0;
        for (unsigned op = 0; op < names.size(); ++op) {
            // Explicit expected results/flags, independent of the shared ALU.
            // Each row is lhs, rhs, masked result, CF/PF/AF/ZF/SF/OF.
            std::vector<std::array<uint64_t, 4>> samples;
            if (op < 2) {
                samples = {{0, 0x3F, mask - 62, 0x91},
                           {sign, 1, sign - 1, 0x814},
                           {sign - 1, mask, sign, 0x885},
                           {7, 7, 0, 0x44},
                           {0, mask, 1, 0x11},
                           {mask, 1, mask - 1, 0x80}};
            } else if (op < 4) {
                samples = {{mask, 0x3F, 0x3F, 4},
                           {mask, 0, 0, 0x44},
                           {sign, mask, sign, 0x84},
                           {3, 1, 1, 0}};
            } else if (op == 4) {
                samples = {{0, 3, 3, 4},
                           {0, 0, 0, 0x44},
                           {sign, 1, sign | 1, 0x80},
                           {0, mask, mask, 0x84}};
            } else {
                samples = {{3, 3, 0, 0x44},
                           {0, 1, 1, 0},
                           {sign, mask, sign - 1, 4},
                           {0, mask, mask, 0x84}};
            }
            const bool writes = op != 0 && op != 2;
            for (unsigned row = 0; row < samples.size(); ++row) {
                const auto& s = samples[row];
                for (const bool immediate : {false, true}) {
                    std::vector<uint8_t> code = {
                        static_cast<uint8_t>((width == 4 ? 0x40 : 0x48) | (immediate ? 1 : 5)),
                        immediate ? static_cast<uint8_t>(op == 2 ? 0xF7 : 0x81) : opcodes[op],
                        immediate ? static_cast<uint8_t>(0xC1 | (extensions[op] << 3))
                                  : uint8_t{0xD1}}; // r9,r10
                    if (immediate) {
                        for (unsigned byte = 0; byte < 4; ++byte) {
                            code.push_back(static_cast<uint8_t>(s[1] >> (byte * 8)));
                        }
                    }
                    auto name = std::string(names[op]) + std::to_string(width * 8) +
                                (immediate ? "Imm" : "Reg") + std::to_string(row);
                    DiffCase test{name, std::move(code), dirty | s[0], writes ? s[2] : dirty | s[0],
                                  s[3]};
                    test.source = s[1] | (width == 4 ? 0x7654321000000000ULL : 0);
                    cases.push_back(std::move(test));
                }
            }
            // dst==src must read the old value before writing/zero-extending.
            DiffCase alias{std::string(names[op]) + std::to_string(width * 8) + "Alias",
                           {static_cast<uint8_t>(width == 4 ? 0x45 : 0x4D), opcodes[op], 0xC9},
                           dirty | 3,
                           writes ? (op == 1 || op == 5 ? 0U : 3U) : dirty | 3,
                           op < 2 || op == 5 ? 0x44U : 4U};
            alias.source_reg = alias.result_reg;
            alias.source = alias.initial;
            cases.push_back(std::move(alias));
        }
        // cmp r9,[r12+disp] and all indexed scales; neither writes the slot.
        for (unsigned form = 0; form < 5; ++form) {
            const uint8_t rex = width == 4 ? 0x45 : 0x4D;
            DiffCase test{"CmpMem" + std::to_string(width * 8) + "Form" + std::to_string(form),
                          {rex, 0x3B, 0x4C, 0x24, 0xB8},
                          dirty | 0x10,
                          dirty | 0x10,
                          0x91};
            test.memory_initial = width == 4 ? 0x765432100000004FULL : 0x4F;
            test.memory_expected = test.memory_initial;
            if (form != 0) {
                test.code[3] = static_cast<uint8_t>(((form - 1) << 6) | 0x0C);
                test.index = 3;
                test.base -= 3U << (form - 1);
            }
            cases.push_back(std::move(test));
        }
    }
    // imm8 sign extension, full-width positive dword immediate, hot cmp form.
    cases.push_back(
        {"Sub32NegativeImm8", {0x41, 0x83, 0xE9, 0xFF}, 0xABCDEF1200000000ULL, 1, 0x11});
    cases.push_back({"Sub64NegativeImm8", {0x49, 0x83, 0xE9, 0xFF}, 0, 1, 0x11});
    cases.push_back({"Cmp32SignBitImm",
                     {0x41, 0x81, 0xF9, 0, 0, 0, 0x80},
                     0xABCDEF1200000000ULL,
                     0xABCDEF1200000000ULL,
                     0x885});
    DiffCase hot{"HotCmpEdxMem", {0x3B, 0x57, 0x3C}, UINT64_MAX, UINT64_MAX, 0x84};
    hot.result_reg = Reg::kRdx;
    hot.base_reg = Reg::kRdi;
    hot.base = kData - 0x3C;
    hot.memory_initial = 0;
    hot.memory_expected = 0;
    cases.push_back(std::move(hot));
    auto loop = indexed_case(
        "HotLoopThroughCmp",
        {0x8B, 0x30, 0x83, 0xC2, 1, 0x89, 0x34, 0x88, 0x48, 0x83, 0xC0, 4, 0x3B, 0x57, 0x3C}, false,
        kData - 0x3C, 0);
    loop.result_reg = Reg::kRax;
    loop.base_reg = Reg::kRdi;
    loop.source_reg = Reg::kRdx;
    loop.initial = kData;
    loop.source = UINT64_MAX;
    loop.expected = kData + 4;
    loop.flags = 0x85;
    loop.jit_insns = 5;
    cases.push_back(std::move(loop));
    for (const bool wide : {false, true}) {
        DiffCase alias{wide ? "CmpMem64BaseAlias" : "CmpMem32BaseAlias",
                       {static_cast<uint8_t>(wide ? 0x48 : 0x40), 0x3B, 0x1B},
                       kData,
                       kData,
                       0x44}; // cmp rbx,[rbx]
        alias.result_reg = alias.base_reg = Reg::kRbx;
        alias.base = kData;
        alias.memory_initial = kData | (wide ? 0 : 0xABCDEF1200000000ULL);
        alias.memory_expected = alias.memory_initial;
        cases.push_back(std::move(alias));
    }
    DiffCase negative{"CmpMemNegativeAliasedIndex",
                      {0x48, 0x3B, 0x04, 0x03},
                      UINT64_MAX - 2,
                      UINT64_MAX - 2,
                      0x84};
    negative.result_reg = negative.index_reg = Reg::kRax;
    negative.base_reg = Reg::kRbx;
    negative.base = kData + 3;
    negative.index = UINT64_MAX - 2;
    negative.memory_initial = 1;
    negative.memory_expected = 1;
    cases.push_back(std::move(negative));
    return cases;
}

TEST_P(JitDiffTest, MatchesInterpreter) {
    const auto& test = GetParam();
    const auto decoded = micro_forge::arch::x86_64::decode_one(test.code);
    ASSERT_TRUE(decoded);
    const auto lowered = translate_insn(*decoded);
    ASSERT_TRUE(lowered);
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
    // Only the instruction under test can compile; no padding mov can
    // satisfy these counters while the target falls back to interpreting.
    EXPECT_EQ(1U, jit.blocks);
    EXPECT_EQ(test.jit_insns, jit.insns);
    EXPECT_EQ(interpreter.exit, jit.exit);
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
    ASSERT_EQ(48U, lowered->code.size());
    ASSERT_EQ(26U, lowered->cpu_offset);
    ASSERT_EQ(36U, lowered->call_offset);
    EXPECT_EQ(0x48, lowered->code[24]);
    EXPECT_EQ(0xBF, lowered->code[25]);
    EXPECT_EQ(0x49, lowered->code[34]);
    EXPECT_EQ(0xBB, lowered->code[35]);
    EXPECT_EQ(0x41, lowered->code[44]);
    EXPECT_EQ(0xFF, lowered->code[45]);
    EXPECT_EQ(0xD3, lowered->code[46]);
    EXPECT_EQ(0x5F, lowered->code[47]);
    ASSERT_EQ(3U, lowered->patches.size());
    constexpr std::array<std::size_t, 3> offsets = {5, 13, 20};
    constexpr std::array<uint64_t, 3> values = {80, 96, 0xFFFFFFB8};
    for (std::size_t i = 0; i < offsets.size(); ++i) {
        EXPECT_EQ(offsets[i], lowered->patches[i].offset);
        EXPECT_EQ(4U, lowered->patches[i].size);
        EXPECT_EQ(values[i], lowered->patches[i].value);
    }
    // All five holes are disjoint, in bounds and zero before relocation.
    constexpr std::array<std::pair<std::size_t, std::size_t>, 5> holes = {
        {{5, 4}, {13, 4}, {20, 4}, {26, 8}, {36, 8}}};
    std::array<bool, 48> occupied{};
    for (const auto& [offset, size] : holes) {
        ASSERT_LE(offset + size, lowered->code.size());
        for (auto i = offset; i < offset + size; ++i) {
            EXPECT_EQ(0, lowered->code[i]);
            EXPECT_FALSE(occupied[i]);
            occupied[i] = true;
        }
    }
    auto bad = *lowered;
    bad.patches[2].offset = 21; // overlaps the mov rdi opcode at 24
    EXPECT_FALSE(bad.has_valid_layout());
    bad = *lowered;
    bad.patches[1].offset = 26; // overlaps the Cpu* relocation
    EXPECT_FALSE(bad.has_valid_layout());
    bad = *lowered;
    // Original failed attempt listed cpu64 as both a data patch and a
    // relocation. Even a zero-valued patch must not claim that hole twice.
    bad.patches.push_back({.offset = 26, .size = 8, .value = 0});
    EXPECT_FALSE(bad.has_valid_layout());
    bad = *lowered;
    bad.call_offset = 34; // movabs opcode start, not its imm64 hole
    EXPECT_FALSE(bad.has_valid_layout());
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
