/* JIT machine-code templates, assembled at build time by
 * scripts/gen/gen_jit_templates.py. Each hole carries a unique magic
 * constant; the generator scans the binary and emits offsets.
 * Generated from the previous hand-written byte arrays; the assembler
 * now owns every offset. Do not hand-write byte arrays again. */
.intel_syntax noprefix
.section .text

.globl tpl_load_slot
tpl_load_slot:
    push   rdi
    mov    rsi,QWORD PTR [rdi+0x6B415365]
    add    rsi,0x6B415367
    movabs rdi,0x1B4153A5000000C1
    mov    edx,0x6B415369
    movabs r11,0x2B4153A5000000C2
    call   r11
    pop    rdi
    mov    QWORD PTR [rdi+0x6B415372],rax
.globl tpl_load_slot_end
tpl_load_slot_end:

.globl tpl_load_idx_slot
tpl_load_idx_slot:
    push   rdi
    mov    rsi,QWORD PTR [rdi+0x6B415365]
    mov    r8,QWORD PTR [rdi+0x6B415370]
    shl    r8,0x5A
    add    rsi,r8
    add    rsi,0x6B415367
    movabs rdi,0x1B4153A5000000C1
    mov    edx,0x6B415369
    movabs r11,0x2B4153A5000000C2
    call   r11
    pop    rdi
    mov    QWORD PTR [rdi+0x6B415372],rax
.globl tpl_load_idx_slot_end
tpl_load_idx_slot_end:

.globl tpl_load_rip
tpl_load_rip:
    push   rdi
    movabs rsi,0x4B4153A5000000C4
    movabs rdi,0x1B4153A5000000C1
    mov    edx,0x6B415369
    movabs r11,0x2B4153A5000000C2
    call   r11
    pop    rdi
    mov    QWORD PTR [rdi+0x6B415372],rax
.globl tpl_load_rip_end
tpl_load_rip_end:

.globl tpl_store_slot
tpl_store_slot:
    push   rdi
    mov    rcx,QWORD PTR [rdi+0x6B41536B]
    mov    rsi,QWORD PTR [rdi+0x6B415365]
    add    rsi,0x6B415367
    movabs rdi,0x1B4153A5000000C1
    mov    edx,0x6B415369
    movabs r11,0x2B4153A5000000C2
    call   r11
    pop    rdi
.globl tpl_store_slot_end
tpl_store_slot_end:

.globl tpl_store_imm_slot
tpl_store_imm_slot:
    push   rdi
    movabs rcx,0x3B4153A5000000C3
    mov    rsi,QWORD PTR [rdi+0x6B415365]
    add    rsi,0x6B415367
    movabs rdi,0x1B4153A5000000C1
    mov    edx,0x6B415369
    movabs r11,0x2B4153A5000000C2
    call   r11
    pop    rdi
.globl tpl_store_imm_slot_end
tpl_store_imm_slot_end:

.globl tpl_mov_slot_slot32
tpl_mov_slot_slot32:
    mov    eax,DWORD PTR [rdi+0x6B41536B]
    mov    QWORD PTR [rdi+0x6B415372],rax
.globl tpl_mov_slot_slot32_end
tpl_mov_slot_slot32_end:

.globl tpl_store_idx_slot
tpl_store_idx_slot:
    push   rdi
    mov    rcx,QWORD PTR [rdi+0x6B41536B]
    mov    rsi,QWORD PTR [rdi+0x6B415365]
    mov    r8,QWORD PTR [rdi+0x6B415370]
    shl    r8,0x5A
    add    rsi,r8
    add    rsi,0x6B415367
    movabs rdi,0x1B4153A5000000C1
    mov    edx,0x6B415369
    movabs r11,0x2B4153A5000000C2
    call   r11
    pop    rdi
.globl tpl_store_idx_slot_end
tpl_store_idx_slot_end:

.globl tpl_add_mem_slot
tpl_add_mem_slot:
    push   rdi
    mov    rdx,QWORD PTR [rdi+0x6B41536B]
    mov    rsi,QWORD PTR [rdi+0x6B415365]
    add    rsi,0x6B415367
    movabs rdi,0x1B4153A5000000C1
    movabs r11,0x2B4153A5000000C2
    call   r11
    pop    rdi
.globl tpl_add_mem_slot_end
tpl_add_mem_slot_end:

.globl tpl_add_imm_helper
tpl_add_imm_helper:
    push   rdi
    mov    esi,0x6B41536A
    mov    edx,0x6B41536F
    mov    ecx,0x6B415369
    movabs rdi,0x1B4153A5000000C1
    movabs r11,0x2B4153A5000000C2
    call   r11
    pop    rdi
.globl tpl_add_imm_helper_end
tpl_add_imm_helper_end:

.globl tpl_movx_load_slot
tpl_movx_load_slot:
    push   rdi
    mov    rsi,QWORD PTR [rdi+0x6B415365]
    add    rsi,0x6B415367
    movabs rdi,0x1B4153A5000000C1
    mov    edx,0x6B41536A
    movabs r11,0x2B4153A5000000C2
    call   r11
    pop    rdi
.globl tpl_movx_load_slot_end
tpl_movx_load_slot_end:

.globl tpl_movx_idx_slot
tpl_movx_idx_slot:
    push   rdi
    mov    rsi,QWORD PTR [rdi+0x6B415365]
    mov    r8,QWORD PTR [rdi+0x6B415370]
    shl    r8,0x5A
    add    rsi,r8
    add    rsi,0x6B415367
    movabs rdi,0x1B4153A5000000C1
    mov    edx,0x6B41536A
    movabs r11,0x2B4153A5000000C2
    call   r11
    pop    rdi
.globl tpl_movx_idx_slot_end
tpl_movx_idx_slot_end:

.globl tpl_movx_store_slot
tpl_movx_store_slot:
    push   rdi
    mov    rsi,QWORD PTR [rdi+0x6B415365]
    add    rsi,0x6B415367
    mov    edx,0x6B41536A
    movabs rdi,0x1B4153A5000000C1
    movabs r11,0x2B4153A5000000C2
    call   r11
    pop    rdi
.globl tpl_movx_store_slot_end
tpl_movx_store_slot_end:

.globl tpl_ret_helper
tpl_ret_helper:
    push   rdi
    mov    rsi,QWORD PTR [rdi+0x6B415374]
    movabs rdx,0x5B4153A5000000C5
    movabs rdi,0x1B4153A5000000C1
    movabs r11,0x2B4153A5000000C2
    call   r11
    pop    rdi
    ret
.globl tpl_ret_helper_end
tpl_ret_helper_end:

.globl tpl_call_reg_helper
tpl_call_reg_helper:
    push   rdi
    mov    rsi,QWORD PTR [rdi+0x6B415375]
    movabs rdx,0x5B4153A5000000C5
    movabs rcx,0x6B4153A5000000C6
    movabs rdi,0x1B4153A5000000C1
    movabs r11,0x2B4153A5000000C2
    call   r11
    pop    rdi
    ret
.globl tpl_call_reg_helper_end
tpl_call_reg_helper_end:

.globl tpl_call_imm_helper
tpl_call_imm_helper:
    push   rdi
    movabs rsi,0x7B4153A5000000C7
    movabs rdx,0x5B4153A5000000C5
    movabs rcx,0x6B4153A5000000C6
    movabs rdi,0x1B4153A5000000C1
    movabs r11,0x2B4153A5000000C2
    call   r11
    pop    rdi
    ret
.globl tpl_call_imm_helper_end
tpl_call_imm_helper_end:

.globl tpl_mov_abs_slot
tpl_mov_abs_slot:
    movabs rax,0x3B4153A5000000C3
    mov    QWORD PTR [rdi+0x6B415372],rax
.globl tpl_mov_abs_slot_end
tpl_mov_abs_slot_end:

.globl tpl_mov_slot_w
tpl_mov_slot_w:
    push   rdi
    mov    rsi,QWORD PTR [rdi+0x6B41536B]
    mov    edx,0x6B415376
    mov    ecx,0x6B415372
    mov    r8d,0x6B415377
    movabs rdi,0x1B4153A5000000C1
    movabs r11,0x2B4153A5000000C2
    call   r11
    pop    rdi
.globl tpl_mov_slot_w_end
tpl_mov_slot_w_end:
