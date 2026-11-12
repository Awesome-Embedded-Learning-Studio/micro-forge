/**
 * @file jit_templates_mem.hpp
 * @brief Memory-access copy-and-patch templates: load, store, indexed and
 *        rip-relative forms. Split from jit_templates.hpp for the file
 *        budget; both headers are header-only constexpr tables.
 */
#ifndef MICRO_FORGE_BASE_JIT_TEMPLATES_MEM_HPP
#define MICRO_FORGE_BASE_JIT_TEMPLATES_MEM_HPP

#include <array>
#include <cstdint>

namespace micro_forge::base::jit_templates {

// mov dst,[base+disp] via helper:
//   push rdi ; mov rsi,[rdi+base] ; add rsi,disp32 ; mov rdi,<cpu64> ;
//   mov edx,width32 ; movabs r11,jit_load ; call r11 ; pop rdi ; mov [rdi+dst],rax
constexpr std::array<std::uint8_t, 53> kLoadSlot = {
    0x57,                                     // @0: push rdi (1)
    0x48, 0x8B, 0xB4, 0x27, 0, 0, 0, 0,       // @1: mov rsi,[rdi+base32] (8)
    0x48, 0x81, 0xC6, 0,    0, 0, 0,          // @9: add rsi,disp32 (7)
    0x48, 0xBF, 0,    0,    0, 0, 0, 0, 0, 0, // @16: mov rdi,cpu64 (10)
    0xBA, 0,    0,    0,    0,                // @26: mov edx,width32 (5)
    0x49, 0xBB, 0,    0,    0, 0, 0, 0, 0, 0, // @31: movabs r11,helper64 (10)
    0x41, 0xFF, 0xD3,                         // @41: call r11 (3)
    0x5F,                                     // @44: pop rdi (1)
    0x48, 0x89, 0x84, 0x27, 0, 0, 0, 0,       // @45: mov [rdi+dst32],rax (8)
};
// Holes: base32@5, disp32@12, cpu64@18, width32@27, helper64@33, dst32@49.
// Indexed load: cumulative starts 0+1+8+8+4+3+7+10+5+10+3+1+8 = 68.
// REX.R (4C, not 48) selects r8 for the index slot load.
constexpr std::array<std::uint8_t, 68> kLoadIdxSlot = {
    0x57,                                     // @0: push rdi (1)
    0x48, 0x8B, 0xB4, 0x27, 0, 0, 0, 0,       // @1: mov rsi,[rdi+base32] (8)
    0x4C, 0x8B, 0x84, 0x27, 0, 0, 0, 0,       // @9: mov r8,[rdi+idx32] (8)
    0x49, 0xC1, 0xE0, 0,                      // @17: shl r8,shift8 (4)
    0x4C, 0x01, 0xC6,                         // @21: add rsi,r8 (3)
    0x48, 0x81, 0xC6, 0,    0, 0, 0,          // @24: add rsi,disp32 (7)
    0x48, 0xBF, 0,    0,    0, 0, 0, 0, 0, 0, // @31: mov rdi,cpu64 (10)
    0xBA, 0,    0,    0,    0,                // @41: mov edx,width32 (5)
    0x49, 0xBB, 0,    0,    0, 0, 0, 0, 0, 0, // @46: movabs r11,helper64 (10)
    0x41, 0xFF, 0xD3,                         // @56: call r11 (3)
    0x5F,                                     // @59: pop rdi (1)
    0x48, 0x89, 0x84, 0x27, 0, 0, 0, 0,       // @60: mov [rdi+dst32],rax (8)
};
// Holes: base32@5, idx32@13, shift8@20, disp32@27, cpu64@33,
// width32@42, helper64@48, dst32@64; later holes shift by 8+4+3 = 15.
// mov dst,[rip+disp] (absolute vaddr precomputed) via helper:
//   push rdi ; mov rsi,<abs64> ; mov rdi,<cpu64> ; mov edx,width32 ;
//   movabs r11,jit_load ; call r11 ; pop rdi ; mov [rdi+dst],rax
constexpr std::array<std::uint8_t, 48> kLoadRip = {
    0x57,                                     // @0: push rdi (1)
    0x48, 0xBE, 0,    0,    0, 0, 0, 0, 0, 0, // @1: mov rsi,abs64 (10)
    0x48, 0xBF, 0,    0,    0, 0, 0, 0, 0, 0, // @11: mov rdi,cpu64 (10)
    0xBA, 0,    0,    0,    0,                // @21: mov edx,width32 (5)
    0x49, 0xBB, 0,    0,    0, 0, 0, 0, 0, 0, // @26: movabs r11,helper64 (10)
    0x41, 0xFF, 0xD3,                         // @36: call r11 (3)
    0x5F,                                     // @39: pop rdi (1)
    0x48, 0x89, 0x84, 0x27, 0, 0, 0, 0,       // @40: mov [rdi+dst32],rax (8)
};
// Holes: abs64@3, cpu64@13, width32@22, helper64@28, dst32@44.
// mov [base+disp],src via helper:
//   push rdi ; mov rcx,[rdi+src] ; mov rsi,[rdi+base] ; add rsi,disp32 ;
//   mov rdi,<cpu64> ; mov edx,width32 ; movabs r11,jit_store ; call r11 ; pop rdi
// ABI: rdi=cpu, rsi=vaddr, edx=width, rcx=value.
constexpr std::array<std::uint8_t, 53> kStoreSlot = {
    0x57,                                     // @0: push rdi (1)
    0x48, 0x8B, 0x8C, 0x27, 0, 0, 0, 0,       // @1: mov rcx,[rdi+src32] (8)
    0x48, 0x8B, 0xB4, 0x27, 0, 0, 0, 0,       // @9: mov rsi,[rdi+base32] (8)
    0x48, 0x81, 0xC6, 0,    0, 0, 0,          // @17: add rsi,disp32 (7)
    0x48, 0xBF, 0,    0,    0, 0, 0, 0, 0, 0, // @24: mov rdi,cpu64 (10)
    0xBA, 0,    0,    0,    0,                // @34: mov edx,width32 (5)
    0x49, 0xBB, 0,    0,    0, 0, 0, 0, 0, 0, // @39: movabs r11,helper64 (10)
    0x41, 0xFF, 0xD3,                         // @49: call r11 (3)
    0x5F,                                     // @52: pop rdi (1)
};
// Holes: src32@5, base32@13, disp32@20, cpu64@26, width32@35,
// helper64@41.
// mov [base+disp],imm via helper (rcx=value, full imm64 so the C7
// sign-extended qword form is exact):
//   push rdi ; movabs rcx,imm64 ; mov rsi,[rdi+base] ; add rsi,disp32 ;
//   mov rdi,<cpu64> ; mov edx,width32 ; movabs r11,jit_store ; call r11 ; pop rdi
constexpr std::array<std::uint8_t, 55> kStoreImmSlot = {
    0x57,                                     // @0: push rdi (1)
    0x48, 0xB9, 0,    0,    0, 0, 0, 0, 0, 0, // @1: movabs rcx,imm64 (10)
    0x48, 0x8B, 0xB4, 0x27, 0, 0, 0, 0,       // @11: mov rsi,[rdi+base32] (8)
    0x48, 0x81, 0xC6, 0,    0, 0, 0,          // @19: add rsi,disp32 (7)
    0x48, 0xBF, 0,    0,    0, 0, 0, 0, 0, 0, // @26: mov rdi,cpu64 (10)
    0xBA, 0,    0,    0,    0,                // @36: mov edx,width32 (5)
    0x49, 0xBB, 0,    0,    0, 0, 0, 0, 0, 0, // @41: movabs r11,helper64 (10)
    0x41, 0xFF, 0xD3,                         // @51: call r11 (3)
    0x5F,                                     // @54: pop rdi (1)
};
// Holes: imm64@3, base32@15, disp32@22, cpu64@28, width32@37,
// helper64@43.
// mov r32,r32 with zero-extension: 32-bit load zeroes the top half.
//   mov eax,[rdi+src32] ; mov [rdi+dst32],rax
constexpr std::array<std::uint8_t, 15> kMovSlotSlot32 = {
    0x8B, 0x84, 0x27, 0,    0, 0, 0,    // @0: mov eax,[rdi+src32] (7)
    0x48, 0x89, 0x84, 0x27, 0, 0, 0, 0, // @7: mov [rdi+dst32],rax (8)
};
// Holes: src32@3, dst32@11.
// Indexed store: cumulative starts 0+1+8+8+8+4+3+7+10+5+10+3+1 = 68.
constexpr std::array<std::uint8_t, 68> kStoreIdxSlot = {
    0x57,                                     // @0: push rdi (1)
    0x48, 0x8B, 0x8C, 0x27, 0, 0, 0, 0,       // @1: mov rcx,[rdi+src32] (8)
    0x48, 0x8B, 0xB4, 0x27, 0, 0, 0, 0,       // @9: mov rsi,[rdi+base32] (8)
    0x4C, 0x8B, 0x84, 0x27, 0, 0, 0, 0,       // @17: mov r8,[rdi+idx32] (8)
    0x49, 0xC1, 0xE0, 0,                      // @25: shl r8,shift8 (4)
    0x4C, 0x01, 0xC6,                         // @29: add rsi,r8 (3)
    0x48, 0x81, 0xC6, 0,    0, 0, 0,          // @32: add rsi,disp32 (7)
    0x48, 0xBF, 0,    0,    0, 0, 0, 0, 0, 0, // @39: mov rdi,cpu64 (10)
    0xBA, 0,    0,    0,    0,                // @49: mov edx,width32 (5)
    0x49, 0xBB, 0,    0,    0, 0, 0, 0, 0, 0, // @54: movabs r11,helper64 (10)
    0x41, 0xFF, 0xD3,                         // @64: call r11 (3)
    0x5F,                                     // @67: pop rdi (1)
};
// Holes: src32@5, base32@13, idx32@21, shift8@28, disp32@35,
// cpu64@41, width32@50, helper64@56; later holes shift by 8+4+3 = 15.
// add qword [base+disp],src via helper (rdi=cpu, rsi=vaddr, rdx=value):
//   push rdi ; mov rdx,[rdi+src] ; mov rsi,[rdi+base] ; add rsi,disp32 ;
//   mov rdi,<cpu64> ; movabs r11,jit_add_mem ; call r11 ; pop rdi
constexpr std::array<std::uint8_t, 48> kAddMemSlot = {
    0x57,                                     // @0: push rdi (1)
    0x48, 0x8B, 0x94, 0x27, 0, 0, 0, 0,       // @1: mov rdx,[rdi+src32] (8)
    0x48, 0x8B, 0xB4, 0x27, 0, 0, 0, 0,       // @9: mov rsi,[rdi+base32] (8)
    0x48, 0x81, 0xC6, 0,    0, 0, 0,          // @17: add rsi,disp32 (7)
    0x48, 0xBF, 0,    0,    0, 0, 0, 0, 0, 0, // @24: mov rdi,cpu64 (10)
    0x49, 0xBB, 0,    0,    0, 0, 0, 0, 0, 0, // @34: movabs r11,helper64 (10)
    0x41, 0xFF, 0xD3,                         // @44: call r11 (3)
    0x5F,                                     // @47: pop rdi (1)
};
// Data holes: src32@5, base32@13, disp32@20 (four bytes each).
// Relocations: cpu64@26 (8 bytes), helper64@36 (8 bytes).
// add reg,imm via helper (helper updates the value and flags):
//   push rdi ; mov esi,<slot32> ; mov edx,<imm32> ; mov ecx,<width32> ; mov rdi,<cpu64> ;
//   movabs r11,jit_add ; call r11 ; pop rdi
constexpr std::array<std::uint8_t, 40> kAddImmHelper = {
    0x57,                                  // @0: push rdi (1)
    0xBE, 0,    0,    0, 0,                // @1: mov esi,slot32 (5)
    0xBA, 0,    0,    0, 0,                // @6: mov edx,imm32 (5)
    0xB9, 0,    0,    0, 0,                // @11: mov ecx,width32 (5)
    0x48, 0xBF, 0,    0, 0, 0, 0, 0, 0, 0, // @16: mov rdi,cpu64 (10)
    0x49, 0xBB, 0,    0, 0, 0, 0, 0, 0, 0, // @26: movabs r11,helper64 (10)
    0x41, 0xFF, 0xD3,                      // @36: call r11 (3)
    0x5F,                                  // @39: pop rdi (1)
};
// Holes: slot32@2, imm32@7, width32@12, cpu64@18, helper64@28.
// movups family, xmm <- [base+disp] via helper (ABI: rdi=cpu, rsi=vaddr,
// edx=xmm byte slot; helper writes both qword halves):
//   push rdi ; mov rsi,[rdi+base] ; add rsi,disp32 ; mov rdi,<cpu64> ;
//   mov edx,slot32 ; movabs r11,jit_load_xmm ; call r11 ; pop rdi
constexpr std::array<std::uint8_t, 45> kMovxLoadSlot = {
    0x57,                                     // @0: push rdi (1)
    0x48, 0x8B, 0xB4, 0x27, 0, 0, 0, 0,       // @1: mov rsi,[rdi+base32] (8)
    0x48, 0x81, 0xC6, 0,    0, 0, 0,          // @9: add rsi,disp32 (7)
    0x48, 0xBF, 0,    0,    0, 0, 0, 0, 0, 0, // @16: mov rdi,cpu64 (10)
    0xBA, 0,    0,    0,    0,                // @26: mov edx,slot32 (5)
    0x49, 0xBB, 0,    0,    0, 0, 0, 0, 0, 0, // @31: movabs r11,helper64 (10)
    0x41, 0xFF, 0xD3,                         // @41: call r11 (3)
    0x5F,                                     // @44: pop rdi (1)
};
// Holes: base32@5, disp32@12, cpu64@18, slot32@27, helper64@33.
// movups family, [base+disp] <- xmm via helper (same ABI, reads both halves):
// movups-family with a SIB index (rsi=vaddr, edx=xmm slot): same helper as
// the base form, only the address arithmetic is longer.
constexpr std::array<std::uint8_t, 60> kMovxIdxSlot = {
    0x57,                                     // @0: push rdi (1)
    0x48, 0x8B, 0xB4, 0x27, 0, 0, 0, 0,       // @1: mov rsi,[rdi+base32] (8)
    0x4C, 0x8B, 0x84, 0x27, 0, 0, 0, 0,       // @9: mov r8,[rdi+idx32] (8)
    0x49, 0xC1, 0xE0, 0,                      // @17: shl r8,shift8 (4)
    0x4C, 0x01, 0xC6,                         // @21: add rsi,r8 (3)
    0x48, 0x81, 0xC6, 0,    0, 0, 0,          // @24: add rsi,disp32 (7)
    0x48, 0xBF, 0,    0,    0, 0, 0, 0, 0, 0, // @31: mov rdi,cpu64 (10)
    0xBA, 0,    0,    0,    0,                // @41: mov edx,slot32 (5)
    0x49, 0xBB, 0,    0,    0, 0, 0, 0, 0, 0, // @46: movabs r11,helper64 (10)
    0x41, 0xFF, 0xD3,                         // @56: call r11 (3)
    0x5F,                                     // @59: pop rdi (1)
};
// Holes: base32@5, idx32@13, shift8@20, disp32@27, slot32@42;
// cpu64@33, helper64@48.
constexpr std::array<std::uint8_t, 45> kMovxStoreSlot = {
    0x57,                                     // @0: push rdi (1)
    0x48, 0x8B, 0xB4, 0x27, 0, 0, 0, 0,       // @1: mov rsi,[rdi+base32] (8)
    0x48, 0x81, 0xC6, 0,    0, 0, 0,          // @9: add rsi,disp32 (7)
    0xBA, 0,    0,    0,    0,                // @16: mov edx,slot32 (5)
    0x48, 0xBF, 0,    0,    0, 0, 0, 0, 0, 0, // @21: mov rdi,cpu64 (10)
    0x49, 0xBB, 0,    0,    0, 0, 0, 0, 0, 0, // @31: movabs r11,helper64 (10)
    0x41, 0xFF, 0xD3,                         // @41: call r11 (3)
    0x5F,                                     // @44: pop rdi (1)
};
// Holes: base32@5, disp32@12, slot32@17, cpu64@23, helper64@33.
// ret C3 via helper (ABI: rdi=cpu, rsi=rsp value, rdx=ret's own rip).
// The helper pops [rsp] into the block exit target; the host ret terminates
// the block, so no trailing template ret is appended.
constexpr std::array<std::uint8_t, 44> kRetHelper = {
    0x57,                                     // @0: push rdi (1)
    0x48, 0x8B, 0xB4, 0x27, 0, 0, 0, 0,       // @1: mov rsi,[rdi+rsp32] (8)
    0x48, 0xBA, 0,    0,    0, 0, 0, 0, 0, 0, // @9: movabs rdx,my_rip64 (10)
    0x48, 0xBF, 0,    0,    0, 0, 0, 0, 0, 0, // @19: mov rdi,cpu64 (10)
    0x49, 0xBB, 0,    0,    0, 0, 0, 0, 0, 0, // @29: movabs r11,helper64 (10)
    0x41, 0xFF, 0xD3,                         // @39: call r11 (3)
    0x5F,                                     // @42: pop rdi (1)
    0xC3,                                     // @43: ret (1)
};
// Holes: rsp32@5, my_rip64@11; relocations cpu64@21, helper64@31.
// call reg64 via helper (ABI: rdi=cpu, rsi=target, rdx=call's rip, rcx=ret
// addr). The helper pushes the return address and records the exit target;
// the host ret terminates the block.
constexpr std::array<std::uint8_t, 54> kCallRegHelper = {
    0x57,                                     // @0: push rdi (1)
    0x48, 0x8B, 0xB4, 0x27, 0, 0, 0, 0,       // @1: mov rsi,[rdi+reg32] (8)
    0x48, 0xBA, 0,    0,    0, 0, 0, 0, 0, 0, // @9: movabs rdx,my_rip64 (10)
    0x48, 0xB9, 0,    0,    0, 0, 0, 0, 0, 0, // @19: movabs rcx,ret_addr64 (10)
    0x48, 0xBF, 0,    0,    0, 0, 0, 0, 0, 0, // @29: mov rdi,cpu64 (10)
    0x49, 0xBB, 0,    0,    0, 0, 0, 0, 0, 0, // @39: movabs r11,helper64 (10)
    0x41, 0xFF, 0xD3,                         // @49: call r11 (3)
    0x5F,                                     // @52: pop rdi (1)
    0xC3,                                     // @53: ret (1)
};
// Holes: reg32@5, my_rip64@11, ret_addr64@21; cpu64@31, helper64@41.
// call rel32: same ABI with the target as a patched imm64 (no regs read).
// The push keeps rsp 16-byte aligned at the helper call (SysV ABI).
constexpr std::array<std::uint8_t, 56> kCallImmHelper = {
    0x57,                                  // @0: push rdi (1)
    0x48, 0xBE, 0,    0, 0, 0, 0, 0, 0, 0, // @1: movabs rsi,target64 (10)
    0x48, 0xBA, 0,    0, 0, 0, 0, 0, 0, 0, // @11: movabs rdx,my_rip64 (10)
    0x48, 0xB9, 0,    0, 0, 0, 0, 0, 0, 0, // @21: movabs rcx,ret_addr64 (10)
    0x48, 0xBF, 0,    0, 0, 0, 0, 0, 0, 0, // @31: mov rdi,cpu64 (10)
    0x49, 0xBB, 0,    0, 0, 0, 0, 0, 0, 0, // @41: movabs r11,helper64 (10)
    0x41, 0xFF, 0xD3,                      // @51: call r11 (3)
    0x5F,                                  // @54: pop rdi (1)
    0xC3,                                  // @55: ret (1)
};
// Holes: target64@3, my_rip64@13, ret_addr64@23; cpu64@33, helper64@43.
// pure host nop: guest nops (incl. 0F 1F multi-byte padding) do nothing.
constexpr std::array<std::uint8_t, 1> kNopJit = {0x90};
// mov r64,imm64 into a slot: host-only, no helper, no flags.
constexpr std::array<std::uint8_t, 17> kMovAbsSlot = {
    0x48, 0xB8, 0,    0, 0, 0, 0, 0, 0, 0, // @0: movabs rax,imm64 (10)
    0x48, 0x89, 0x87, 0, 0, 0, 0,          // @10: mov [rdi+dst32],rax (7)
};
// Holes: imm64@2, dst32@13.
// movzx/mov-r16 reg,reg via helper (rsi=value, edx=width, ecx=dst, r8d=zx).
constexpr std::array<std::uint8_t, 48> kMovSlotW = {
    0x57,                                  // @0: push rdi (1)
    0x48, 0x8B, 0xB7, 0, 0, 0, 0,          // @1: mov rsi,[rdi+src32] (7)
    0xBA, 0,    0,    0, 0,                // @8: mov edx,srcw32 (5)
    0xB9, 0,    0,    0, 0,                // @13: mov ecx,dst32 (5)
    0x41, 0xB8, 0,    0, 0, 0,             // @18: mov r8d,zx32 (6)
    0x48, 0xBF, 0,    0, 0, 0, 0, 0, 0, 0, // @24: mov rdi,cpu64 (10)
    0x49, 0xBB, 0,    0, 0, 0, 0, 0, 0, 0, // @34: movabs r11,helper64 (10)
    0x41, 0xFF, 0xD3,                      // @44: call r11 (3)
    0x5F,                                  // @47: pop rdi (1)
};
// Holes: src32@4, srcw32@9, dst32@14, zx32@20; cpu64@26, helper64@36.
// Mid-block template: falls through, no host ret.

} // namespace micro_forge::base::jit_templates

#endif // MICRO_FORGE_BASE_JIT_TEMPLATES_MEM_HPP
