/**
 * @file jit_templates.hpp
 * @brief Copy-and-patch machine-code bytes and patch-hole documentation.
 * @version 0.1.0
 */

#pragma once

#include <algorithm>
#include <array>
#include <cstdint>

namespace micro_forge::base::jit_templates {

// --- templates ---------------------------------------------------------
// All templates operate on the guest register file through RDI, with the
// register slot carried in disp32. SIB 0x27 = scale 1, index none, base rdi.
// movabs rax,imm64 ; mov [rdi+slot32],rax. Holes: imm64@2, slot32@14.
// A full immediate permits both zero-extended dwords and sign-extended qwords.
constexpr std::array<std::uint8_t, 18> kMovSlotImm = {
    0x48, 0xB8, 0, 0, 0, 0, 0, 0, 0, 0, 0x48, 0x89, 0x84, 0x27, 0, 0, 0, 0,
};
// add qword [rdi+disp32], imm32                 (48 81 84 27 <disp32> <imm32>)
constexpr std::array<std::uint8_t, 12> kAddSlotImm = {
    0x48, 0x81, 0x84, 0x27, 0, 0, 0, 0, 0, 0, 0, 0,
};
// sub qword [rdi+disp32], imm32                 (48 81 AC 27 <disp32> <imm32>)
constexpr std::array<std::uint8_t, 12> kSubSlotImm = {
    0x48, 0x81, 0xAC, 0x27, 0, 0, 0, 0, 0, 0, 0, 0,
};
// and qword [rdi+disp32], imm32                 (48 81 A4 27 <disp32> <imm32>)
constexpr std::array<std::uint8_t, 12> kAndSlotImm = {
    0x48, 0x81, 0xA4, 0x27, 0, 0, 0, 0, 0, 0, 0, 0,
};
// or qword [rdi+disp32], imm32                  (48 81 8C 27 <disp32> <imm32>)
constexpr std::array<std::uint8_t, 12> kOrSlotImm = {
    0x48, 0x81, 0x8C, 0x27, 0, 0, 0, 0, 0, 0, 0, 0,
};
// xor qword [rdi+disp32], imm32                 (48 81 B4 27 <disp32> <imm32>)
constexpr std::array<std::uint8_t, 12> kXorSlotImm = {
    0x48, 0x81, 0xB4, 0x27, 0, 0, 0, 0, 0, 0, 0, 0,
};
// mov rax, [rdi+src] ; mov [rdi+dst], rax   (reg-to-reg slot copy)
// 48 8B 84 27 <src32>  48 89 84 27 <dst32>
constexpr std::array<std::uint8_t, 16> kMovSlotSlot = {
    0x48, 0x8B, 0x84, 0x27, 0, 0, 0, 0, 0x48, 0x89, 0x84, 0x27, 0, 0, 0, 0,
};
// Holes: src32@4, dst32@12.
// lea dst,[base+disp]: mov rax,[rdi+base] ; add rax,disp32 ; mov [rdi+dst],rax
// 48 8B 84 27 <base32>  48 05 <disp32>  48 89 84 27 <dst32>
constexpr std::array<std::uint8_t, 22> kLeaSlot = {
    0x48, 0x8B, 0x84, 0x27, 0, 0, 0, 0, 0x48, 0x05, 0, 0, 0, 0, 0x48, 0x89, 0x84, 0x27, 0, 0, 0, 0,
};
// Holes: base32@4, disp32@10, dst32@18 (the final mov starts at 14).
// lea dst,[base+index*scale+disp]:
//   mov rax,[rdi+base] ; mov r8,[rdi+idx] ; shl r8,log2(scale) ;
//   add rax,r8 ; add rax,disp32 ; mov [rdi+dst],rax
// 48 8B 84 27 <base32>  4C 8B 84 27 <idx32>  49 C1 E0 <scale8>
// 4C 01 C0  48 05 <disp32>  48 89 84 27 <dst32>
constexpr std::array<std::uint8_t, 37> kLeaIdxSlot = {
    0x48, 0x8B, 0x84, 0x27, 0, 0, 0, 0, // @0:  mov rax,[rdi+base] (8)
    0x4C, 0x8B, 0x84, 0x27, 0, 0, 0, 0, // @8:  mov r8,[rdi+idx] (8)
    0x49, 0xC1, 0xE0, 0,                // @16: shl r8,scale8 (4)
    0x4C, 0x01, 0xC0,                   // @20: add rax,r8 (3)
    0x48, 0x05, 0,    0,    0, 0,       // @23: add rax,disp32 (6)
    0x48, 0x89, 0x84, 0x27, 0, 0, 0, 0, // @29: mov [rdi+dst],rax (8)
};
// Holes: base32@4, idx32@12, scale8@19 (log2 scale), disp32@25, dst32@33.
// lea r32,[base+index*scale+disp]: same arithmetic, then `mov eax,eax`
// truncates and zero-extends before the slot write.
// ... 48 89 84 27 <dst32> becomes 89 C0 48 89 84 27 <dst32>
constexpr std::array<std::uint8_t, 39> kLeaIdxSlot32 = {
    0x48, 0x8B, 0x84, 0x27, 0, 0, 0, 0, // @0:  mov rax,[rdi+base] (8)
    0x4C, 0x8B, 0x84, 0x27, 0, 0, 0, 0, // @8:  mov r8,[rdi+idx] (8)
    0x49, 0xC1, 0xE0, 0,                // @16: shl r8,scale8 (4)
    0x4C, 0x01, 0xC0,                   // @20: add rax,r8 (3)
    0x48, 0x05, 0,    0,    0, 0,       // @23: add rax,disp32 (6)
    0x89, 0xC0,                         // @29: mov eax,eax (2)
    0x48, 0x89, 0x84, 0x27, 0, 0, 0, 0, // @31: mov [rdi+dst],rax (8)
};
// Holes: base32@4, idx32@12, scale8@19, disp32@25, dst32@35.
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
// ret
constexpr std::array<std::uint8_t, 1> kRet = {
    0xC3,
};

// ALU reg,imm: rdi=cpu, esi=slot, rdx=decoded immediate, ecx=width.
// movabs preserves the decoder's sign extension (imm8/imm32 for qwords).
constexpr std::array<std::uint8_t, 45> kAluImmHelper = {
    0x57,                                  // @0: push rdi (1)
    0xBE, 0,    0,    0, 0,                // @1: mov esi,slot32 (5)
    0x48, 0xBA, 0,    0, 0, 0, 0, 0, 0, 0, // @6: movabs rdx,imm64 (10)
    0xB9, 0,    0,    0, 0,                // @16: mov ecx,width32 (5)
    0x48, 0xBF, 0,    0, 0, 0, 0, 0, 0, 0, // @21: mov rdi,cpu64 (10)
    0x49, 0xBB, 0,    0, 0, 0, 0, 0, 0, 0, // @31: movabs r11,helper64 (10)
    0x41, 0xFF, 0xD3,                      // @41: call r11 (3)
    0x5F,                                  // @44: pop rdi (1)
};
// Holes: slot32@2, imm64@8, width32@17, cpu64@23, helper64@33.
// ALU reg,reg: same ABI, reading the source value before replacing rdi.
constexpr std::array<std::uint8_t, 43> kAluRegHelper = {
    0x57,                                     // @0: push rdi (1)
    0xBE, 0,    0,    0,    0,                // @1: mov esi,slot32 (5)
    0x48, 0x8B, 0x94, 0x27, 0, 0, 0, 0,       // @6: mov rdx,[rdi+src32] (8)
    0xB9, 0,    0,    0,    0,                // @14: mov ecx,width32 (5)
    0x48, 0xBF, 0,    0,    0, 0, 0, 0, 0, 0, // @19: mov rdi,cpu64 (10)
    0x49, 0xBB, 0,    0,    0, 0, 0, 0, 0, 0, // @29: movabs r11,helper64 (10)
    0x41, 0xFF, 0xD3,                         // @39: call r11 (3)
    0x5F,                                     // @42: pop rdi (1)
};
// Holes: slot32@2, src32@10, width32@15, cpu64@21, helper64@31.
// Holes: slot32@2, src32@10, width32@15, cpu64@21, helper64@31.
// CMOV reg,reg: rdi=cpu, rsi=dst slot, rdx=src value, ecx=cc, r8d=width.
constexpr std::array<std::uint8_t, 49> kCmovRegHelper = {
    0x57,                                     // @0: push rdi (1)
    0xBE, 0,    0,    0,    0,                // @1: mov esi,dst32 (5)
    0x48, 0x8B, 0x94, 0x27, 0, 0, 0, 0,       // @6: mov rdx,[rdi+src32] (8)
    0xB9, 0,    0,    0,    0,                // @14: mov ecx,cc32 (5)
    0x41, 0xB8, 0,    0,    0, 0,             // @19: mov r8d,width32 (6)
    0x48, 0xBF, 0,    0,    0, 0, 0, 0, 0, 0, // @25: mov rdi,cpu64 (10)
    0x49, 0xBB, 0,    0,    0, 0, 0, 0, 0, 0, // @35: movabs r11,helper64 (10)
    0x41, 0xFF, 0xD3,                         // @45: call r11 (3)
    0x5F,                                     // @48: pop rdi (1)
};
// Holes: dst32@2, src32@10, cc32@15, width32@21, cpu64@27, helper64@37.
// SETCC reg: rdi=cpu, esi=dst slot, edx=cc. 35 bytes.
constexpr std::array<std::uint8_t, 35> kSetccRegHelper = {
    0x57,                                  // @0: push rdi (1)
    0xBE, 0,    0,    0, 0,                // @1: mov esi,dst32 (5)
    0xBA, 0,    0,    0, 0,                // @6: mov edx,cc32 (5)
    0x48, 0xBF, 0,    0, 0, 0, 0, 0, 0, 0, // @11: mov rdi,cpu64 (10)
    0x49, 0xBB, 0,    0, 0, 0, 0, 0, 0, 0, // @21: movabs r11,helper64 (10)
    0x41, 0xFF, 0xD3,                      // @31: call r11 (3)
    0x5F,                                  // @34: pop rdi (1)
};
// Holes: dst32@2, cc32@7, cpu64@13, helper64@23.

// PUSH reg64: rdi=cpu, rsi=src slot. 33 bytes.
constexpr std::array<std::uint8_t, 33> kPushRegHelper = {
    0x57,                                     // @0: push rdi (1)
    0x48, 0x8B, 0xB4, 0x27, 0, 0, 0, 0,       // @1: mov rsi,[rdi+slot32] (8)
    0x48, 0xBF, 0,    0,    0, 0, 0, 0, 0, 0, // @9: mov rdi,cpu64 (10)
    0x49, 0xBB, 0,    0,    0, 0, 0, 0, 0, 0, // @19: movabs r11,helper64 (10)
    0x41, 0xFF, 0xD3,                         // @29: call r11 (3)
    0x5F,                                     // @32: pop rdi (1)
};
// Holes: slot32@4, cpu64@11, helper64@21.

// PUSH imm64 (sign-extended imm8/imm32 arrive pre-extended): 35 bytes.
constexpr std::array<std::uint8_t, 35> kPushImmHelper = {
    0x57,                                  // @0: push rdi (1)
    0x48, 0xBE, 0,    0, 0, 0, 0, 0, 0, 0, // @1: movabs rsi,imm64 (10)
    0x48, 0xBF, 0,    0, 0, 0, 0, 0, 0, 0, // @11: mov rdi,cpu64 (10)
    0x49, 0xBB, 0,    0, 0, 0, 0, 0, 0, 0, // @21: movabs r11,helper64 (10)
    0x41, 0xFF, 0xD3,                      // @31: call r11 (3)
    0x5F,                                  // @34: pop rdi (1)
};
// Holes: imm64@3, cpu64@13, helper64@23.

// POP reg64: rdi=cpu, esi=dst slot. 30 bytes.
constexpr std::array<std::uint8_t, 30> kPopRegHelper = {
    0x57,                                  // @0: push rdi (1)
    0xBE, 0,    0,    0, 0,                // @1: mov esi,slot32 (5)
    0x48, 0xBF, 0,    0, 0, 0, 0, 0, 0, 0, // @6: mov rdi,cpu64 (10)
    0x49, 0xBB, 0,    0, 0, 0, 0, 0, 0, 0, // @16: movabs r11,helper64 (10)
    0x41, 0xFF, 0xD3,                      // @26: call r11 (3)
    0x5F,                                  // @29: pop rdi (1)
};
// Holes: slot32@2, cpu64@8, helper64@18.

// CMP memory tail: rdi=cpu, rsi=vaddr (from load prefix), edx=slot, ecx=width.
constexpr std::array<std::uint8_t, 34> kCmpMemCall = {
    0xBA, 0,    0,    0, 0,                // @0: mov edx,slot32 (5)
    0xB9, 0,    0,    0, 0,                // @5: mov ecx,width32 (5)
    0x48, 0xBF, 0,    0, 0, 0, 0, 0, 0, 0, // @10: mov rdi,cpu64 (10)
    0x49, 0xBB, 0,    0, 0, 0, 0, 0, 0, 0, // @20: movabs r11,helper64 (10)
    0x41, 0xFF, 0xD3,                      // @30: call r11 (3)
    0x5F,                                  // @33: pop rdi (1)
};
// Tail holes: slot32@1, width32@6, cpu64@12, helper64@22.
// Reuse the exact load address calculation, omitting its call and slot write.
/// @brief Appends the CMP helper call to an existing load address prefix.
/// @return Fixed template bytes with zero-filled patch holes.
template <auto LoadCode, std::size_t Prefix>
constexpr auto cmp_memory_code() {
    std::array<std::uint8_t, Prefix + kCmpMemCall.size()> code{};
    std::copy_n(LoadCode.begin(), Prefix, code.begin());
    std::ranges::copy(kCmpMemCall, code.begin() + Prefix);
    return code;
}
constexpr auto kCmpMemSlot = cmp_memory_code<kLoadSlot, 16>();
// 16+34=50 bytes; base@5, disp@12, slot@17, width@22, cpu@28, helper@38.
constexpr auto kCmpMemIdxSlot = cmp_memory_code<kLoadIdxSlot, 31>();
// 31+34=65 bytes; base@5, idx@13, shift@20, disp@27,
// slot@32, width@37, cpu@43, helper@53. All latter holes move by 15 bytes.

} // namespace micro_forge::base::jit_templates
