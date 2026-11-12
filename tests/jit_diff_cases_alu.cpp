// NOLINTBEGIN -- test scaffolding: fork/setenv/env capture are deliberate
/// @file jit_diff_cases_alu.cpp
/// @brief ALU-family case table split from jit_diff_cases.cpp (file budget).

#include "jit_diff_cases.hpp"

namespace jit_diff_cases {

using namespace jit_diff;

std::vector<DiffCase> alu_cases() {
    std::vector<DiffCase> cases;
    constexpr std::array names = {"Cmp", "Sub", "Test", "And", "Or", "Xor"};
    constexpr std::array<uint8_t, 6> opcodes = {0x39, 0x29, 0x85, 0x21, 0x09, 0x31};
    constexpr std::array<uint8_t, 6> extensions = {7, 5, 0, 4, 1, 6};
    for (const unsigned width : {4U, 8U}) {
        const uint64_t sign = uint64_t{1} << ((width * 8) - 1);
        const uint64_t mask = width == 4 ? UINT32_MAX : UINT64_MAX;
        const uint64_t dirty = width == 4 ? 0xABCDEF1200000000ULL : 0;
        for (unsigned op = 0; op < names.size(); ++op) {
            // Explicit expected results/flags, independent of the shared ALU.
            // Each row is lhs, rhs, masked result, CF/PF/AF/ZF/SF/OF.
            std::vector<std::array<uint64_t, 4>> samples;
            if (op < 2) {
                samples = {
                    {0, 0x3F, mask - 62, 0x91},
                    {sign, 1, sign - 1, 0x814},
                    {sign - 1, mask, sign, 0x885},
                    {7, 7, 0, 0x44},
                    {0, mask, 1, 0x11},
                    {mask, 1, mask - 1, 0x80},
                };
            } else if (op < 4) {
                samples = {
                    {mask, 0x3F, 0x3F, 4},
                    {mask, 0, 0, 0x44},
                    {sign, mask, sign, 0x84},
                    {3, 1, 1, 0},
                };
            } else if (op == 4) {
                samples = {
                    {0, 3, 3, 4},
                    {0, 0, 0, 0x44},
                    {sign, 1, static_cast<uint64_t>(sign) | 1U, 0x80},
                    {0, mask, mask, 0x84},
                };
            } else {
                samples = {
                    {3, 3, 0, 0x44},
                    {0, 1, 1, 0},
                    {sign, mask, sign - 1, 4},
                    {0, mask, mask, 0x84},
                };
            }
            const bool writes = op != 0 && op != 2;
            for (unsigned row = 0; row < samples.size(); ++row) {
                const auto& s = samples[row];
                for (const bool immediate : {false, true}) {
                    std::vector<uint8_t> code = {
                        static_cast<uint8_t>((width == 4 ? 0x40U : 0x48U) | (immediate ? 1U : 5U)),
                        [immediate, op, &opcodes] {
                            if (!immediate) {
                                return opcodes[op];
                            }
                            return op == 2 ? uint8_t{0xF7} : uint8_t{0x81};
                        }(),
                        immediate ? static_cast<uint8_t>(
                                        0xC1U | (static_cast<unsigned>(extensions[op]) << 3U))
                                  : uint8_t{0xD1},
                    }; // r9,r10
                    if (immediate) {
                        for (unsigned byte = 0; byte < 4; ++byte) {
                            code.push_back(static_cast<uint8_t>(s[1] >> (byte * 8)));
                        }
                    }
                    auto const name = std::string(names[op]) + std::to_string(width * 8) +
                                      (immediate ? "Imm" : "Reg") + std::to_string(row);
                    DiffCase test{
                        .name = name,
                        .code = std::move(code),
                        .initial = dirty | s[0],
                        .expected = writes ? s[2] : dirty | s[0],
                        .flags = s[3],
                    };
                    test.source = s[1] | (width == 4 ? 0x7654321000000000ULL : 0);
                    cases.push_back(std::move(test));
                }
            }
            // dst==src must read the old value before writing/zero-extending.
            DiffCase alias{
                .name = std::string(names[op]) + std::to_string(width * 8) + "Alias",
                .code = {static_cast<uint8_t>(width == 4 ? 0x45 : 0x4D), opcodes[op], 0xC9},
                .initial = dirty | 3U,
                .expected =
                    writes
                        ? uint64_t{op == 1 || op == 5 ? 0U : 3U}
                        : (dirty | 3U), // NOLINT(readability-avoid-nested-conditional-operator) //
                                        // NOLINT(readability-avoid-nested-conditional-operator)
                .flags = op < 2 || op == 5 ? 0x44U : 4U,
            };
            alias.source_reg = alias.result_reg;
            alias.source = alias.initial;
            cases.push_back(std::move(alias));
        }
        // cmp r9,[r12+disp] and all indexed scales; neither writes the slot.
        for (unsigned form = 0; form < 5; ++form) {
            const uint8_t rex = width == 4 ? 0x45 : 0x4D;
            DiffCase test{
                .name = "CmpMem" + std::to_string(width * 8) + "Form" + std::to_string(form),
                .code = {rex, 0x3B, 0x4C, 0x24, 0xB8},
                .initial = dirty | 0x10U,
                .expected = dirty | 0x10U,
                .flags = 0x91,
            };
            test.memory_initial = width == 4 ? 0x765432100000004FULL : 0x4F;
            test.memory_expected = test.memory_initial;
            if (form != 0) {
                test.code[3] = static_cast<uint8_t>(((form - 1U) << 6U) | 0x0CU);
                test.index = 3;
                test.base -= 3U << (form - 1);
            }
            cases.push_back(std::move(test));
        }
    }
    // imm8 sign extension, full-width positive dword immediate, hot cmp form.
    cases.push_back({
        .name = "Sub32NegativeImm8",
        .code = {0x41, 0x83, 0xE9, 0xFF},
        .initial = 0xABCDEF1200000000ULL,
        .expected = 1,
        .flags = 0x11,
    });
    cases.push_back({
        .name = "Sub64NegativeImm8",
        .code = {0x49, 0x83, 0xE9, 0xFF},
        .initial = 0,
        .expected = 1,
        .flags = 0x11,
    });
    cases.push_back({
        .name = "Cmp32SignBitImm",
        .code = {0x41, 0x81, 0xF9, 0, 0, 0, 0x80},
        .initial = 0xABCDEF1200000000ULL,
        .expected = 0xABCDEF1200000000ULL,
        .flags = 0x885,
    });
    DiffCase hot{
        .name = "HotCmpEdxMem",
        .code = {0x3B, 0x57, 0x3C},
        .initial = UINT64_MAX,
        .expected = UINT64_MAX,
        .flags = 0x84,
    };
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
        DiffCase alias{
            .name = wide ? "CmpMem64BaseAlias" : "CmpMem32BaseAlias",
            .code = {static_cast<uint8_t>(wide ? 0x48 : 0x40), 0x3B, 0x1B},
            .initial = kData,
            .expected = kData,
            .flags = 0x44,
        }; // cmp rbx,[rbx]
        alias.result_reg = alias.base_reg = Reg::kRbx;
        alias.base = kData;
        alias.memory_initial = kData | (wide ? 0 : 0xABCDEF1200000000ULL);
        alias.memory_expected = alias.memory_initial;
        cases.push_back(std::move(alias));
    }
    DiffCase negative{
        .name = "CmpMemNegativeAliasedIndex",
        .code = {0x48, 0x3B, 0x04, 0x03},
        .initial = UINT64_MAX - 2,
        .expected = UINT64_MAX - 2,
        .flags = 0x84,
    };
    negative.result_reg = negative.index_reg = Reg::kRax;
    negative.base_reg = Reg::kRbx;
    negative.base = kData + 3;
    negative.index = UINT64_MAX - 2;
    negative.memory_initial = 1;
    negative.memory_expected = 1;
    cases.push_back(std::move(negative));
    // setcc r8: kFlags leaves e/b/le/be/s/o/p true, the rest false; the byte
    // write must keep the upper 7 bytes of the seeded destination.
    const uint64_t kept = kLoaded & ~0xFFULL;
    const std::vector<std::pair<const char*, std::vector<uint8_t>>> setccs = {
        {"st94_e_cl", {0x0F, 0x94, 0xC1}},        {"st95_ne_cl", {0x0F, 0x95, 0xC1}},
        {"st92_b_cl", {0x0F, 0x92, 0xC1}},        {"st97_a_cl", {0x0F, 0x97, 0xC1}},
        {"st93_ae_cl", {0x0F, 0x93, 0xC1}},       {"st9e_le_cl", {0x0F, 0x9E, 0xC1}},
        {"st9f_g_cl", {0x0F, 0x9F, 0xC1}},        {"st96_be_cl", {0x0F, 0x96, 0xC1}},
        {"st94_e_r9b", {0x41, 0x0F, 0x94, 0xC1}}, {"st95_ne_r9b", {0x41, 0x0F, 0x95, 0xC1}},
    };
    for (const auto& [name, code] : setccs) {
        const unsigned cc_op = code[code.size() - 2];
        const bool taken = cc_op == 0x94U || cc_op == 0x92U || cc_op == 0x9EU || cc_op == 0x96U;
        const bool r9 = code.size() == 4;
        DiffCase c{
            .name = name,
            .code = code,
            .initial = kLoaded,
            .expected = kept | (taken ? 1U : 0U),
            .flags = kFlags,
        };
        c.result_reg = r9 ? Reg::kR9 : Reg::kRcx;
        cases.push_back(c);
    }
    // push/pop reg64 + push imm: rsp seeds at 0xF000 (writable RAM), so a
    // balanced push/pop pair must restore both the value and rsp itself.
    DiffCase push_pop{
        .name = "push_rdx_pop_rcx",
        .code = {0x52, 0x59},
        .initial = kLoaded,
        .expected = kStored,
        .flags = kFlags,
    };
    push_pop.result_reg = Reg::kRcx;
    push_pop.source_reg = Reg::kRdx;
    push_pop.jit_insns = 2;
    cases.push_back(push_pop);
    DiffCase push_r9{
        .name = "push_rcx_pop_r9",
        .code = {0x51, 0x41, 0x59},
        .initial = kLoaded,
        .expected = kStored,
        .flags = kFlags,
    };
    push_r9.source_reg = Reg::kRcx;
    push_r9.jit_insns = 2;
    cases.push_back(push_r9);
    DiffCase rsp_check{
        .name = "push_pop_rsp_back",
        .code = {0x52, 0x59, 0x52, 0x59},
        .initial = 0xF000, // seeds rsp itself: keep the writable stack top
        .expected = 0xF000,
        .flags = kFlags,
    };
    rsp_check.result_reg = Reg::kRsp;
    rsp_check.source_reg = Reg::kRdx;
    rsp_check.jit_insns = 4;
    cases.push_back(rsp_check);
    DiffCase push_rsp{
        .name = "push_rsp_pop_rcx",
        .code = {0x54, 0x59},
        .initial = kLoaded,
        .expected = 0xF000,
        .flags = kFlags,
    };
    push_rsp.result_reg = Reg::kRcx;
    push_rsp.source_reg = Reg::kRsp;
    push_rsp.source = 0xF000; // source seeds rsp: keep the writable stack top
    push_rsp.jit_insns = 2;
    cases.push_back(push_rsp);
    DiffCase push_imm32c{
        .name = "push_imm32_pop_rcx",
        .code = {0x68, 0x44, 0x33, 0x22, 0x11, 0x59},
        .initial = kLoaded,
        .expected = 0x11223344,
        .flags = kFlags,
    };
    push_imm32c.result_reg = Reg::kRcx;
    push_imm32c.jit_insns = 2;
    cases.push_back(push_imm32c);
    DiffCase push_imm8c{
        .name = "push_imm8_pop_rcx",
        .code = {0x6A, 0x93, 0x59},
        .initial = kLoaded,
        .expected = 0xFFFFFFFFFFFFFF93ULL,
        .flags = kFlags,
    };
    push_imm8c.result_reg = Reg::kRcx;
    push_imm8c.jit_insns = 2;
    cases.push_back(push_imm8c);
    DiffCase pop_rsp{
        .name = "push_imm_pop_rsp",
        .code = {0x68, 0x78, 0x56, 0x34, 0x12, 0x5C},
        .initial = 0xF000, // seeds rsp itself: keep the writable stack top
        .expected = 0x12345678,
        .flags = kFlags,
    };
    pop_rsp.result_reg = Reg::kRsp;
    pop_rsp.jit_insns = 2;
    cases.push_back(pop_rsp);
    // movups family xmm <-> [base+disp]: 16 bytes through both halves.
    DiffCase movx_store{
        .name = "movups_store_rax",
        .code = {0x0F, 0x11, 0x00}, // movups [rax],xmm0
        .initial = kLoaded,
        .expected = kLoaded,
        .flags = kFlags,
        .store = true,
        .memory_expected = kStored, // low qword of xmm0 lands at kData
    };
    movx_store.base_reg = Reg::kRax;
    movx_store.base = kData;
    movx_store.vec = {kStored, 0x1F2E3D4C5B6A7988ULL};
    cases.push_back(movx_store);
    DiffCase movx_load{
        .name = "movups_load_rbx",
        .code = {0x0F, 0x10, 0x03}, // movups xmm0,[rbx]
        .initial = kLoaded,
        .expected = kLoaded,
        .flags = kFlags,
    };
    movx_load.base_reg = Reg::kRbx;
    movx_load.base = kData;
    movx_load.vec = {0x0102030405060708ULL, 0x1112131415161718ULL};
    cases.push_back(movx_load);
    DiffCase movx_store_disp{
        .name = "movaps_store_disp",
        .code = {0x0F, 0x29, 0x40, 0xF0}, // movaps [rax-0x10],xmm0
        .initial = kLoaded,
        .expected = kLoaded,
        .flags = kFlags,
        .store = true,
        .memory_expected = kStored,
    };
    movx_store_disp.base_reg = Reg::kRax;
    movx_store_disp.base = kData + 0x10;
    movx_store_disp.vec = {kStored, kLoaded};
    cases.push_back(movx_store_disp);
    DiffCase movx_load_xmm5{
        .name = "movdqa_load_xmm5",
        .code = {0x66, 0x0F, 0x6F, 0x28}, // movdqa xmm5,[rax]
        .initial = kLoaded,
        .expected = kLoaded,
        .flags = kFlags,
    };
    movx_load_xmm5.base_reg = Reg::kRax;
    movx_load_xmm5.base = kData;
    movx_load_xmm5.vec_reg = Reg::kXmm5;
    movx_load_xmm5.vec = {0, 0};
    cases.push_back(movx_load_xmm5);
    // ret: push target; ret pops it and the hlt after proves the jump.
    DiffCase ret_push{
        .name = "push_target_ret",
        .code = {0x68, 0x06, 0x10, 0x00, 0x00, 0xC3, 0xF4}, // push 0x1006; ret; hlt
        .initial = kLoaded,
        .expected = kLoaded,
        .flags = kFlags,
    };
    ret_push.jit_insns = 2; // push + ret (block ends at the ret)
    cases.push_back(ret_push);
    // leave: rsp=rbp then pop rbp; rbp ends as [rbp] and rsp walks 8.
    DiffCase leave_rbp{
        .name = "leave_rbp",
        .code = {0x48, 0xC9, 0xF4}, // leave; hlt
        .initial = kLoaded,
        .expected = kLoaded,
        .flags = kFlags,
        .memory_initial = kStored,
        .memory_expected = kStored,
    };
    leave_rbp.base_reg = Reg::kRbp;
    leave_rbp.base = kData;
    cases.push_back(leave_rbp);
    // call reg64: the helper pushes the return address and jumps.
    DiffCase call_reg{
        .name = "call_rax",
        .code =
            {
                0x48,
                0xC7,
                0xC0,
                0x0C,
                0x10,
                0x00,
                0x00, // mov rax,0x100C
                0xFF,
                0xD0, // call rax
                0xF4,
                0x90,
                0xF4,
            }, // hlt; nop; hlt@0x100C
        .initial = kLoaded,
        .expected = kLoaded,
        .flags = kFlags,
    };
    call_reg.jit_insns = 2; // mov + call (block ends at the call)
    cases.push_back(call_reg);
    // call rel32: patched imm64 target, same helper ABI.
    DiffCase call_imm{
        .name = "call_rel32",
        .code =
            {
                0xE8,
                0x02,
                0x00,
                0x00,
                0x00, // call +2 -> 0x1007
                0xF4,
                0x90,
                0xF4,
            },
        .initial = kLoaded,
        .expected = kLoaded,
        .flags = kFlags,
    };
    call_imm.jit_insns = 1;
    cases.push_back(call_imm);
    // jmp rel: taken unconditionally via the exit target.
    DiffCase jmp_rel{
        .name = "jmp_rel8",
        .code = {0xEB, 0x02, 0xF4, 0x90, 0xF4}, // jmp +2 -> 0x1004; hlt; nop; hlt
        .initial = kLoaded,
        .expected = kLoaded,
        .flags = kFlags,
    };
    cases.push_back(jmp_rel);
    // jcc taken (kFlags has ZF set, so JE jumps).
    DiffCase jcc_taken{
        .name = "jcc_je_taken",
        .code = {0x74, 0x02, 0xF4, 0x90, 0xF4}, // je +2; hlt; nop; hlt
        .initial = kLoaded,
        .expected = kLoaded,
        .flags = kFlags,
    };
    cases.push_back(jcc_taken);
    DiffCase mb_nop{
        .name = "mb_nop",
        .code = {0x0F, 0x1F, 0x40, 0x00, 0xF4}, // 7-byte nop padding; hlt
        .initial = kLoaded,
        .expected = kLoaded,
        .flags = kFlags,
    };
    cases.push_back(mb_nop);
    DiffCase movabs64{
        .name = "movabs64",
        .code = {0x48, 0xB8, 0x34, 0x12, 0x00, 0x00, 0x80, 0xFF, 0xFF, 0xFF, 0xF4},
        .initial = kLoaded,
        .expected = kLoaded,
        .flags = kFlags,
    };
    cases.push_back(movabs64);
    DiffCase mov_r16{
        .name = "mov_r16",
        .code = {0x66, 0x89, 0xC8, 0xF4}, // mov ax,cx; hlt
        .initial = kLoaded,
        .expected = kLoaded,
        .flags = kFlags,
    };
    cases.push_back(mov_r16);
    DiffCase movzx_r32r8{
        .name = "movzx_r32r8",
        .code = {0x0F, 0xB6, 0xC1, 0xF4}, // movzx eax,cl; hlt
        .initial = kLoaded,
        .expected = kLoaded,
        .flags = kFlags,
    };
    cases.push_back(movzx_r32r8);
    DiffCase shl_r64_imm{
        .name = "shl_r64_imm",
        .code = {0x48, 0xC1, 0xE0, 0x03, 0xF4}, // shl rax,3; hlt
        .initial = kLoaded,
        .expected = kLoaded << 3U,
        .flags = 2192,
    };
    shl_r64_imm.result_reg = Reg::kRax;
    cases.push_back(shl_r64_imm);
    DiffCase shr_r32_imm{
        .name = "shr_r32_imm",
        .code = {0xC1, 0xE8, 0x04, 0xF4}, // shr eax,4; hlt
        .initial = kLoaded,
        .expected = (kLoaded & 0xFFFFFFFFU) >> 4U,
        .flags = 20,
    };
    shr_r32_imm.result_reg = Reg::kRax;
    cases.push_back(shr_r32_imm);
    DiffCase sar_r64_one{
        .name = "sar_r64_one",
        .code = {0x48, 0xD1, 0xF8, 0xF4}, // sar rax,1; hlt
        .initial = kLoaded,
        .expected = 0xFF6E5D4C3B2A1908ULL, // kLoaded >> 1, sign bit kept
        .flags = 144,
    };
    sar_r64_one.result_reg = Reg::kRax;
    cases.push_back(sar_r64_one);
    DiffCase shl_count_zero{
        .name = "shl_count_zero",
        .code = {0x48, 0xC1, 0xE0, 0x00, 0xF4}, // shl rax,0: flags untouched
        .initial = kLoaded,
        .expected = kLoaded,
        .flags = kFlags,
    };
    shl_count_zero.result_reg = Reg::kRax;
    cases.push_back(shl_count_zero);
    DiffCase add_reg_reg{
        .name = "add_reg_reg",
        .code = {0x48, 0x01, 0xD8, 0xF4}, // add rax,rbx; hlt
        .initial = kLoaded,
        .expected = 0x0EFCEAD8C6B4A593ULL, // carries out of bit 63
        .flags = 5,                        // CF and PF set, rest clear
    };
    add_reg_reg.result_reg = Reg::kRax;
    cases.push_back(add_reg_reg);
    DiffCase movzx_mem8{
        .name = "movzx_mem8",
        .code = {0x41, 0x0F, 0xB6, 0x04, 0x24, 0xF4}, // movzx eax,byte [r12]; hlt
        .initial = kLoaded,
        .expected = 0x10, // low byte of memory_initial (kLoaded)
        .flags = kFlags,
        .base = kData, // r12 must point at the seeded qword itself
    };
    movzx_mem8.result_reg = Reg::kRax;
    cases.push_back(movzx_mem8);
    DiffCase movzx_mem16_64{
        .name = "movzx_mem16_64",
        .code = {0x49, 0x0F, 0xB7, 0x04, 0x24, 0xF4}, // movzx rax,word [r12]; hlt
        .initial = kLoaded,
        .expected = 0x3210, // low word of kLoaded
        .flags = kFlags,
        .base = kData,
    };
    movzx_mem16_64.result_reg = Reg::kRax;
    cases.push_back(movzx_mem16_64);
    DiffCase add_reg_mem{
        .name = "add_reg_mem",
        .code = {0x49, 0x03, 0x04, 0x24, 0xF4}, // add rax,qword [r12]; hlt
        .initial = kLoaded,
        .expected = kLoaded + kLoaded, // rax += [r12] (memory holds kLoaded)
        .flags = 0x81,                 // CF and SF set (low byte 0x20 is odd)
        .base = kData,
    };
    add_reg_mem.result_reg = Reg::kRax;
    cases.push_back(add_reg_mem);
    DiffCase sub_reg_mem32{
        .name = "sub_reg_mem32",
        .code = {0x41, 0x2B, 0x04, 0x24, 0xF4}, // sub eax,dword [r12]; hlt
        .initial = kLoaded,
        .expected = 0, // eax - [r12], both halves equal
        .flags = 0x44, // ZF and PF (zero has even parity)
        .base = kData,
    };
    sub_reg_mem32.result_reg = Reg::kRax;
    cases.push_back(sub_reg_mem32);
    // jcc not taken (JNE with ZF set falls through).
    DiffCase jcc_fall{
        .name = "jcc_jne_fall",
        .code = {0x75, 0x02, 0xF4, 0x90, 0xF4}, // jne +2; hlt; nop; hlt
        .initial = kLoaded,
        .expected = kLoaded,
        .flags = kFlags,
    };
    cases.push_back(jcc_fall);
    return cases;
}
} // namespace jit_diff_cases
