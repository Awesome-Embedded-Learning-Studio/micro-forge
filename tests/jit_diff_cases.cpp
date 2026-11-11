// NOLINTBEGIN -- test scaffolding: fork/setenv/env capture are deliberate
/// @file jit_diff_cases.cpp
/// @brief Case tables for the JIT differential fixture (split from
///        test_jit_diff.cpp to respect the 600-line file budget).

#include "jit_diff_cases.hpp"

#include <cstdio>

namespace jit_diff_cases {

using namespace jit_diff;

DiffCase indexed_case(const char* name, std::vector<uint8_t> code, bool store, uint64_t base,
                      uint64_t index, Reg index_reg) {
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
                      uint64_t flags, uint64_t base, std::vector<uint8_t> code) {
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
    // mov eax,eax-style 32-bit reg-to-reg: zeroes the dirty upper half.
    append("Mov32RegRegZeroExt", {0x45, 0x89, 0xD1}, 0xABCDEF1289ABCDEFULL, 0x44332211);
    // imul rdi,rdx (48 0F AF FA): slot*kLoaded truncated to width.
    // rdi = (2^32+1) * kStored: the 128-bit product spills, so CF=OF=1.
    DiffCase imul64{"Imul64RegReg",
                    {0x48, 0x0F, 0xAF, 0xFA},
                    0x100000001ULL,
                    0xCCAA886644332211ULL,
                    kFlags | 0x801};
    imul64.result_reg = Reg::kRdi; // imul rdi,rdx
    imul64.source_reg = Reg::kRdx; // rdx carries kStored
    imul64.jit_insns = 1;
    cases.push_back(imul64);
    // 8/16-bit ALU reg-reg and cmp-mem forms: writes keep upper bytes.
    {
        // dst=al/ax (rax=kLoaded), src=cl/cx (rcx=0x1122334455667788).
        // 8-bit writes keep rax's upper bytes; 16-bit writes zero-extend.
        const std::tuple<const char*, uint64_t, uint64_t> enc[] = {
            {"w8_or", 0xFEDCBA9876543298ULL, 128},   {"w8_and", 0xFEDCBA9876543200ULL, 68},
            {"w8_xor", 0xFEDCBA9876543298ULL, 128},  {"w8_sub", 0xFEDCBA9876543288ULL, 2181},
            {"w8_cmp", 0xFEDCBA9876543210ULL, 2181}, {"w8_test", 0xFEDCBA9876543210ULL, 68},
            {"w16_or", 0xFEDCBA9876547798ULL, 0},    {"w16_sub", 0xFEDCBA987654BA88ULL, 133},
            {"w16_cmp", 0xFEDCBA9876543210ULL, 133}, {"w8_cmp_mem", 0xFEDCBA9876543210ULL, 2052},
        };
        const std::vector<uint8_t> code[] = {
            {0x08, 0xC8}, {0x20, 0xC8}, {0x30, 0xC8}, {0x28, 0xC8}, {0x38, 0xC8},
            {0x84, 0xC8}, {0x66, 0x09, 0xC8}, {0x66, 0x29, 0xC8}, {0x66, 0x39, 0xC8},
            {0x3A, 0x0B},
        };
        for (std::size_t k = 0; k < std::size(enc); ++k) {
            DiffCase w{std::get<0>(enc[k]), code[k], kLoaded, std::get<1>(enc[k]),
                      std::get<2>(enc[k])};
            w.result_reg = Reg::kRax;
            w.source_reg = Reg::kRcx;
            w.source = 0x1122334455667788ULL;
            w.jit_insns = 1;
            w.base_reg = Reg::kRbx;
            w.base = kData; // cmp al,[rbx] reads kData
            cases.push_back(w);
        }
    }

    // 8/16-bit immediate ALU forms. dst is al for the byte encodings and cx
    // for the word encodings (kernel-typical /ri layouts, imm hex is LE).
    {
        const std::tuple<const char*, std::vector<uint8_t>, uint64_t, uint64_t, uint64_t,
                         Reg, uint64_t>
            enc[] = {
                {"i8_or", {0x80, 0xC8, 0x77}, 0xFEDCBA9876543277ULL, 4, kLoaded, Reg::kRax,
                 kLoaded},
                {"i8_sub", {0x80, 0xE8, 0x77}, 0xFEDCBA9876543299ULL, 149, kLoaded, Reg::kRax,
                 kLoaded},
                {"i8_cmp", {0x80, 0xF8, 0x88}, kLoaded, 2181, kLoaded, Reg::kRax, kLoaded},
                {"i8_and", {0x80, 0xE0, 0x0F}, 0xFEDCBA9876543200ULL, 68, kLoaded, Reg::kRax,
                 kLoaded},
                {"i16_sub", {0x66, 0x81, 0xE9, 0x11, 0x22}, 0x1020304050604F70ULL, 0,
                 0x1020304050607181ULL, Reg::kRcx, 0x1020304050607181ULL},
                {"i16_or", {0x66, 0x81, 0xC9, 0x34, 0x12}, 0x10203040506073B5ULL, 0,
                 0x1020304050607181ULL, Reg::kRcx, 0x1020304050607181ULL},
        };
        for (const auto& [n, c, e, fl, init, rr, exp] : enc) {
            DiffCase w{n, c, init, e, fl};
            w.result_reg = rr;
            w.jit_insns = 1;
            cases.push_back(w);
        }
    }

    // AH/CH/DH/BH destinations: the helper ABI addresses whole slots, so these
    // must fall back to the interpreter instead of corrupting the parent reg.
    {
        const std::pair<const char*, std::vector<uint8_t>> enc[] = {
            {"h8_and_ah", {0x80, 0xE4, 0xF7}}, {"h8_and_bh", {0x80, 0xE7, 0xFB}},
            {"h8_sub_ah", {0x80, 0xEC, 0x77}}, {"h8_cmp_ah", {0x80, 0xFC, 0x00}},
            {"h8_xor_ah", {0x80, 0xF4, 0x0D}}, {"h8_and_dh", {0x80, 0xE6, 0x06}},
        };
        for (const auto& [n, c] : enc) {
            DiffCase w{n, c, kLoaded, kLoaded, kFlags};
            w.result_reg = Reg::kRax;
            w.require_lowered = false; // must stay with the interpreter
            cases.push_back(w);
        }
    }

    // rdi = -1 * kStored: both sides negative, the product fits, CF=OF=0.
    DiffCase imul_of{
        "Imul64Overflow", {0x48, 0x0F, 0xAF, 0xFA}, ~0ULL, 0x778899AABBCCDDEFULL, 0xD4};
    imul_of.result_reg = Reg::kRdi;
    imul_of.source_reg = Reg::kRdx;
    imul_of.jit_insns = 1;
    cases.push_back(imul_of);
    // test cl,cl (84 C9): byte-width flags from the masked AND.
    DiffCase test8{"Test8RegReg", {0x84, 0xC9}, 0x80ULL, 0x80ULL, 0x80};
    test8.result_reg = Reg::kRcx; // test cl,cl: rcx kept, SF set
    cases.push_back(test8);
    DiffCase mov32_r8{"Mov32RegRegRexBoth", {0x45, 0x89, 0xC1}, ~0ULL, 0x76543210};
    mov32_r8.source_reg = Reg::kR8; // mov r9d,r8d: REX.R+B at once
    mov32_r8.source = kLoaded;      // r8 seeds the source constant
    cases.push_back(mov32_r8);
    DiffCase store_imm8{"StoreImm8Base",      {0xC6, 0x03, 0x77}, 0, 0, kFlags, true,
                        0x1122334455667788ULL};
    store_imm8.base_reg = Reg::kRbx; // [rbx] hits kData byte 0
    store_imm8.base = kData;
    store_imm8.memory_expected = 0x1122334455667777ULL;
    cases.push_back(store_imm8);
    // mov qword [rbx-8],-1: verified via the 24-byte snapshot compare.
    DiffCase store_imm64{
        "StoreImm64SignExt",  {0x48, 0xC7, 0x43, 0xF8, 0xFF, 0xFF, 0xFF, 0xFF}, 0, 0, kFlags, true,
        0x1122334455667788ULL};
    store_imm64.base_reg = Reg::kRbx; // [rbx-8] takes the full sign-extended imm
    store_imm64.memory_expected = 0x1122334455667788ULL;
    cases.push_back(store_imm64);
    // lea r9,[rbx+rcx*8+0x30]: flags untouched.
    auto lea_idx = indexed_case("LeaIndexScale8", {0x48, 0x8D, 0x44, 0xCB, 0x30}, false, kData);
    lea_idx.expected = kData + 3 * 8 + 0x30;
    lea_idx.jit_insns = 1;
    cases.push_back(std::move(lea_idx));
    // lea r9,[rbx+rcx*2-1]: disp8 negative, scale 2.
    auto lea_neg = indexed_case("LeaIndexScale2Neg", {0x48, 0x8D, 0x44, 0x4B, 0xFF}, false, kData);
    lea_neg.expected = kData + 3 * 2 - 1;
    lea_neg.jit_insns = 1;
    cases.push_back(std::move(lea_neg));
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

} // namespace jit_diff_cases
