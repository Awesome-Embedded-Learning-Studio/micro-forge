/* JIT machine-code templates, assembled at build time by
 * scripts/gen/gen_jit_templates.py. Every hole carries a unique magic
 * constant; the generator scans the binary, records offset+size, and emits
 * jit_templates_generated.hpp. Do not hand-write byte arrays.
 *
 * Calling convention of a template: rdi = Cpu*, helper in r11, all thunks
 * keep rsp 16-byte aligned by pushing rdi around the call.
 */
.intel_syntax noprefix
.section .text

/* mov [rdi+base] -> rsi; add disp; rdi=cpu; edx=width; call helper;
 * pop rdi; mov rax -> [rdi+dst]. */
.globl tpl_load_slot
tpl_load_slot:
    push rdi
    mov rsi, [rdi + 0x6b415365]
    add rsi, 0x6b415367
    movabs rdi, 0x1b4153a5000000c1
    mov edx, 0x6b415369
    movabs r11, 0x2b4153a5000000c2
    call r11
    pop rdi
    mov [rdi + 0x6b41536a], rax
.globl tpl_load_slot_end
tpl_load_slot_end:
