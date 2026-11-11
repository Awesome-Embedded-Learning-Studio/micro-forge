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
    return {
        .name = name,
        .code = std::move(code),
        .initial = 0,
        .expected = store ? 0 : kLoaded,
        .store = store,
        .base = base,
        .result_reg = store ? Reg::kR9 : Reg::kRax,
        .base_reg = Reg::kRbx,
        .source_reg = Reg::kRax,
        .index = index,
        .index_reg = index_reg,
    };
}

DiffCase add_mem_case(const char* name, uint64_t initial, uint64_t source, uint64_t expected,
                      uint64_t flags, uint64_t base, std::vector<uint8_t> code) {
    return {
        .name = name,
        .code = std::move(code),
        .initial = 0,
        .expected = 0,
        .flags = flags,
        .store = false,
        .memory_initial = initial,
        .source = source,
        .memory_expected = expected,
        .base = base,
    };
}

std::vector<DiffCase> width_cases() {
    std::vector<DiffCase> cases;
    constexpr uint64_t dirty = 0xABCDEF1289ABCDEFULL;
    constexpr std::array<uint64_t, 4> loaded = {
        0xABCDEF1289ABCD21ULL,
        0xABCDEF1289AB4321ULL,
        0x87654321,
        0xDEADBEEF87654321ULL,
    };
    constexpr std::array<uint64_t, 4> stored = {
        0xFEDCBA9876543211ULL,
        0xFEDCBA9876542211ULL,
        0xFEDCBA9844332211ULL,
        kStored,
    };
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
                unsigned mov_op = store ? 0x89U : 0x8BU;
                if (width == 1) {
                    mov_op = store ? 0x88U : 0x8AU;
                }
                code.push_back(mov_op);
                unsigned modrm = 0x44U;
                if (form == 0) {
                    modrm = 0x03U;
                } else if (form == 1) {
                    modrm = 0x43U;
                }
                code.push_back(modrm);
                if (form == 2) {
                    code.push_back(0x8B); // [rbx+rcx*4-32]
                }
                if (form != 0) {
                    code.push_back(form == 1 ? 0xF0 : 0xE0);
                }
                auto test = indexed_case(name.c_str(), std::move(code), store,
                                         form == 0 ? kData : kData + (form == 1 ? 16U : 20U));
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
        cases.push_back({
            .name = name,
            .code = std::move(code),
            .initial = initial,
            .expected = expected,
            .flags = flags,
        });
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
    DiffCase imul64{
        .name = "Imul64RegReg",
        .code = {0x48, 0x0F, 0xAF, 0xFA},
        .initial = 0x100000001ULL,
        .expected = 0xCCAA886644332211ULL,
        .flags = kFlags | 0x801U,
    };
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
            {0x08, 0xC8}, {0x20, 0xC8},       {0x30, 0xC8},       {0x28, 0xC8},       {0x38, 0xC8},
            {0x84, 0xC8}, {0x66, 0x09, 0xC8}, {0x66, 0x29, 0xC8}, {0x66, 0x39, 0xC8}, {0x3A, 0x0B},
        };
        for (std::size_t k = 0; k < std::size(enc); ++k) {
            DiffCase w{
                .name = std::get<0>(enc[k]),
                .code = code[k],
                .initial = kLoaded,
                .expected = std::get<1>(enc[k]),
                .flags = std::get<2>(enc[k]),
            };
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
        const std::tuple<const char*, std::vector<uint8_t>, uint64_t, uint64_t, uint64_t, Reg,
                         uint64_t>
            enc[] = {
                {
                    "i8_or",
                    {0x80, 0xC8, 0x77},
                    0xFEDCBA9876543277ULL,
                    4,
                    kLoaded,
                    Reg::kRax,
                    kLoaded,
                },
                {
                    "i8_sub",
                    {0x80, 0xE8, 0x77},
                    0xFEDCBA9876543299ULL,
                    149,
                    kLoaded,
                    Reg::kRax,
                    kLoaded,
                },
                {"i8_cmp", {0x80, 0xF8, 0x88}, kLoaded, 2181, kLoaded, Reg::kRax, kLoaded},
                {
                    "i8_and",
                    {0x80, 0xE0, 0x0F},
                    0xFEDCBA9876543200ULL,
                    68,
                    kLoaded,
                    Reg::kRax,
                    kLoaded,
                },
                {
                    "i16_sub",
                    {0x66, 0x81, 0xE9, 0x11, 0x22},
                    0x1020304050604F70ULL,
                    0,
                    0x1020304050607181ULL,
                    Reg::kRcx,
                    0x1020304050607181ULL,
                },
                {
                    "i16_or",
                    {0x66, 0x81, 0xC9, 0x34, 0x12},
                    0x10203040506073B5ULL,
                    0,
                    0x1020304050607181ULL,
                    Reg::kRcx,
                    0x1020304050607181ULL,
                },
        };
        for (const auto& [n, c, e, fl, init, rr, exp] : enc) {
            DiffCase w{.name = n, .code = c, .initial = init, .expected = e, .flags = fl};
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
            DiffCase w{
                .name = n,
                .code = c,
                .initial = kLoaded,
                .expected = kLoaded,
                .flags = kFlags,
            };
            w.result_reg = Reg::kRax;
            w.require_lowered = false; // must stay with the interpreter
            cases.push_back(w);
        }
    }

    // CMOV reg,reg across every interesting condition. Seed flags 0x8D5 give
    // CF=1 PF=1 ZF=0 SF=0 OF=0, so taken/not-taken is fully determined.
    {
        DiffCase cm45_w64{
            .name = "cm45_w64",
            .code = {0x48, 0x0F, 0x45, 0xCA},
            .initial = kLoaded,
            .expected = 0xfedcba9876543210ULL,
            .flags = kFlags,
        };
        DiffCase cm45_w32{
            .name = "cm45_w32",
            .code = {0x0F, 0x45, 0xCA},
            .initial = kLoaded,
            .expected = 0xfedcba9876543210ULL,
            .flags = kFlags,
        };
        DiffCase cm44_w64{
            .name = "cm44_w64",
            .code = {0x48, 0x0F, 0x44, 0xCA},
            .initial = kLoaded,
            .expected = 0x8877665544332211ULL,
            .flags = kFlags,
        };
        DiffCase cm44_w32{
            .name = "cm44_w32",
            .code = {0x0F, 0x44, 0xCA},
            .initial = kLoaded,
            .expected = 0x0000000044332211ULL,
            .flags = kFlags,
        };
        DiffCase cm42_w64{
            .name = "cm42_w64",
            .code = {0x48, 0x0F, 0x42, 0xCA},
            .initial = kLoaded,
            .expected = 0x8877665544332211ULL,
            .flags = kFlags,
        };
        DiffCase cm42_w32{
            .name = "cm42_w32",
            .code = {0x0F, 0x42, 0xCA},
            .initial = kLoaded,
            .expected = 0x0000000044332211ULL,
            .flags = kFlags,
        };
        DiffCase cm43_w64{
            .name = "cm43_w64",
            .code = {0x48, 0x0F, 0x43, 0xCA},
            .initial = kLoaded,
            .expected = 0xfedcba9876543210ULL,
            .flags = kFlags,
        };
        DiffCase cm43_w32{
            .name = "cm43_w32",
            .code = {0x0F, 0x43, 0xCA},
            .initial = kLoaded,
            .expected = 0xfedcba9876543210ULL,
            .flags = kFlags,
        };
        DiffCase cm4_c_w64{
            .name = "cm4C_w64",
            .code = {0x48, 0x0F, 0x4C, 0xCA},
            .initial = kLoaded,
            .expected = 0xfedcba9876543210ULL,
            .flags = kFlags,
        };
        DiffCase cm4_c_w32{
            .name = "cm4C_w32",
            .code = {0x0F, 0x4C, 0xCA},
            .initial = kLoaded,
            .expected = 0xfedcba9876543210ULL,
            .flags = kFlags,
        };
        DiffCase cm4_f_w64{
            .name = "cm4F_w64",
            .code = {0x48, 0x0F, 0x4F, 0xCA},
            .initial = kLoaded,
            .expected = 0xfedcba9876543210ULL,
            .flags = kFlags,
        };
        DiffCase cm4_f_w32{
            .name = "cm4F_w32",
            .code = {0x0F, 0x4F, 0xCA},
            .initial = kLoaded,
            .expected = 0xfedcba9876543210ULL,
            .flags = kFlags,
        };
        DiffCase cm4_e_w64{
            .name = "cm4E_w64",
            .code = {0x48, 0x0F, 0x4E, 0xCA},
            .initial = kLoaded,
            .expected = 0x8877665544332211ULL,
            .flags = kFlags,
        };
        DiffCase cm4_e_w32{
            .name = "cm4E_w32",
            .code = {0x0F, 0x4E, 0xCA},
            .initial = kLoaded,
            .expected = 0x0000000044332211ULL,
            .flags = kFlags,
        };
        DiffCase cm47_w64{
            .name = "cm47_w64",
            .code = {0x48, 0x0F, 0x47, 0xCA},
            .initial = kLoaded,
            .expected = 0xfedcba9876543210ULL,
            .flags = kFlags,
        };
        DiffCase cm47_w32{
            .name = "cm47_w32",
            .code = {0x0F, 0x47, 0xCA},
            .initial = kLoaded,
            .expected = 0xfedcba9876543210ULL,
            .flags = kFlags,
        };
        for (const auto& w : {
                 cm45_w64,
                 cm44_w64,
                 cm42_w64,
                 cm43_w64,
                 cm4_c_w64,
                 cm4_f_w64,
                 cm4_e_w64,
                 cm47_w64,
                 cm45_w32,
                 cm44_w32,
                 cm42_w32,
                 cm43_w32,
                 cm4_c_w32,
                 cm4_f_w32,
                 cm4_e_w32,
                 cm47_w32,
             }) {
            DiffCase c = w;
            c.result_reg = Reg::kRcx; // encoding CA: cmovcc rcx,rdx
            c.source_reg = Reg::kRdx;
            c.source = kStored;
            cases.push_back(c);
        }
    }

    // rdi = -1 * kStored: both sides negative, the product fits, CF=OF=0.
    DiffCase imul_of{
        .name = "Imul64Overflow",
        .code = {0x48, 0x0F, 0xAF, 0xFA},
        .initial = ~0ULL,
        .expected = 0x778899AABBCCDDEFULL,
        .flags = 0xD4,
    };
    imul_of.result_reg = Reg::kRdi;
    imul_of.source_reg = Reg::kRdx;
    imul_of.jit_insns = 1;
    cases.push_back(imul_of);
    // test cl,cl (84 C9): byte-width flags from the masked AND.
    DiffCase test8{
        .name = "Test8RegReg",
        .code = {0x84, 0xC9},
        .initial = 0x80ULL,
        .expected = 0x80ULL,
        .flags = 0x80,
    };
    test8.result_reg = Reg::kRcx; // test cl,cl: rcx kept, SF set
    cases.push_back(test8);
    DiffCase mov32_r8{
        .name = "Mov32RegRegRexBoth",
        .code = {0x45, 0x89, 0xC1},
        .initial = ~0ULL,
        .expected = 0x76543210,
    };
    mov32_r8.source_reg = Reg::kR8; // mov r9d,r8d: REX.R+B at once
    mov32_r8.source = kLoaded;      // r8 seeds the source constant
    cases.push_back(mov32_r8);
    DiffCase store_imm8{
        .name = "StoreImm8Base",
        .code = {0xC6, 0x03, 0x77},
        .initial = 0,
        .expected = 0,
        .flags = kFlags,
        .store = true,
        .memory_initial = 0x1122334455667788ULL,
    };
    store_imm8.base_reg = Reg::kRbx; // [rbx] hits kData byte 0
    store_imm8.base = kData;
    store_imm8.memory_expected = 0x1122334455667777ULL;
    cases.push_back(store_imm8);
    // mov qword [rbx-8],-1: verified via the 24-byte snapshot compare.
    DiffCase store_imm64{
        .name = "StoreImm64SignExt",
        .code = {0x48, 0xC7, 0x43, 0xF8, 0xFF, 0xFF, 0xFF, 0xFF},
        .initial = 0,
        .expected = 0,
        .flags = kFlags,
        .store = true,
        .memory_initial = 0x1122334455667788ULL,
    };
    store_imm64.base_reg = Reg::kRbx; // [rbx-8] takes the full sign-extended imm
    store_imm64.memory_expected = 0x1122334455667788ULL;
    cases.push_back(store_imm64);
    // lea r9,[rbx+rcx*8+0x30]: flags untouched.
    auto lea_idx = indexed_case("LeaIndexScale8", {0x48, 0x8D, 0x44, 0xCB, 0x30}, false, kData);
    lea_idx.expected = kData + 24U + 0x30U;
    lea_idx.jit_insns = 1;
    cases.push_back(std::move(lea_idx));
    // lea r9,[rbx+rcx*2-1]: disp8 negative, scale 2.
    auto lea_neg = indexed_case("LeaIndexScale2Neg", {0x48, 0x8D, 0x44, 0x4B, 0xFF}, false, kData);
    lea_neg.expected = kData + 6U - 1U;
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

} // namespace jit_diff_cases
