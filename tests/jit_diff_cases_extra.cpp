// NOLINTBEGIN -- test scaffolding: fork/setenv/env capture are deliberate
/// @file jit_diff_cases_extra.cpp
/// @brief Case table for the newer lowering rounds: pxor, shifts, rip forms.
///        Split from jit_diff_cases_alu.cpp to stay inside the file budget.

#include "jit_diff_cases.hpp"

namespace jit_diff_cases {

using namespace jit_diff;

std::vector<DiffCase> extra_cases() {
    std::vector<DiffCase> cases;
    // pxor on a non-zero source proves the helper really xors both halves.
    DiffCase pxor_mixed{
        .name = "pxor_mixed",
        .code = {0x66, 0x0F, 0xEF, 0xC1, 0xF4}, // pxor xmm0,xmm1; hlt
        .initial = kLoaded,
        .expected = kLoaded,
        .flags = kFlags,
        .vec_reg = Reg::kXmm0,
        .vec = {0x0123456789ABCDEFULL, 0xFEDCBA9876543210ULL},
    };
    cases.push_back(pxor_mixed);
    DiffCase shl_r64_cl{
        .name = "shl_r64_cl",
        .code = {0x48, 0xD3, 0xE0, 0xF4}, // shl rax,cl (cl masked to 1)
        .initial = kLoaded,
        .expected = kLoaded << 1U,
        .flags = 2192, // AF, SF and OF set; matches the interpreter
    };
    shl_r64_cl.result_reg = Reg::kRax;
    cases.push_back(shl_r64_cl);
    // rip-relative forms: the effective address is next-rip + disp, and the
    // harness places data at kData (0x4000) while code starts at 0x1000.
    DiffCase add_rip{
        .name = "add_rip",
        .code = {0x48, 0x03, 0x05, 0xF9, 0x2F, 0x00, 0x00, 0xF4}, // add rax,[rip+0x2ff9]
        .initial = kLoaded,
        .expected = kLoaded + kLoaded,
        .flags = 0x81, // CF and SF set
    };
    add_rip.result_reg = Reg::kRax;
    cases.push_back(add_rip);
    DiffCase movzx_rip{
        .name = "movzx_rip",
        .code = {0x0F, 0xB6, 0x05, 0xF9, 0x2F, 0x00, 0x00, 0xF4}, // movzx eax,[rip+0x2ff9]
        .initial = kLoaded,
        .expected = 0x10, // low byte of kLoaded at kData
        .flags = kFlags,
    };
    movzx_rip.result_reg = Reg::kRax;
    cases.push_back(movzx_rip);
    DiffCase lea_rip{
        .name = "lea_rip",
        .code = {0x48, 0x8D, 0x05, 0xF9, 0x2F, 0x00, 0x00, 0xF4}, // lea rax,[rip+0x2ff9]
        .initial = kLoaded,
        .expected = 0x4000, // kData
        .flags = kFlags,
    };
    lea_rip.result_reg = Reg::kRax;
    cases.push_back(lea_rip);
    // Accumulator short form: the register is in the opcode, so ops[0] is
    // kImplicit rather than kReg.
    DiffCase cmp_accum{
        .name = "cmp_accum",
        .code = {0x48, 0x3D, 0x00, 0x01, 0x00, 0x00, 0xF4}, // cmp rax,0x100
        .initial = kLoaded,
        .expected = kLoaded,
        .flags = 0x80, // SF only: the difference stays negative
    };
    cmp_accum.result_reg = Reg::kRax;
    cases.push_back(cmp_accum);
    // lea with a 32-bit destination: the whole slot is rewritten, highs zeroed.
    DiffCase lea_r32{
        .name = "lea_r32",
        .code = {0x8D, 0x79, 0x01, 0xF4}, // lea edi,[rcx+1]
        .initial = kLoaded,
        .expected = 0x50607182ULL, // (rcx seed 0x1020304050607181) + 1, 32-bit
        .flags = kFlags,
    };
    lea_r32.result_reg = Reg::kRdi;
    cases.push_back(lea_r32);
    // movzx through a SIB index: rcx is zeroed so the address stays at kData.
    DiffCase movzx_idx{
        .name = "movzx_idx",
        .code = {0x41, 0x0F, 0xB6, 0x04, 0x0C, 0xF4}, // movzx eax,byte [r12+rcx]
        .initial = kLoaded,
        .expected = 0x10, // low byte of kLoaded
        .flags = kFlags,
        .base = kData,
        .index = 0,
    };
    movzx_idx.result_reg = Reg::kRax;
    cases.push_back(movzx_idx);
    // One-operand div: rdx:rax / rbx. xor edx,edx first keeps the quotient
    // inside 64 bits so no #DE is raised.
    DiffCase div_reg{
        .name = "div_reg",
        .code = {0x31, 0xD2, 0x48, 0xF7, 0xF3, 0xF4}, // xor edx,edx ; div rbx
        .initial = kLoaded,
        .expected = 0xF, // quotient, remainder 0x0cf9e6d3c0ad6d63 in rdx
        .flags = 0x44,   // xor edx,edx sets ZF and PF
    };
    div_reg.result_reg = Reg::kRax;
    div_reg.jit_insns = 2;
    cases.push_back(div_reg);
    return cases;
}

} // namespace jit_diff_cases
// NOLINTEND
