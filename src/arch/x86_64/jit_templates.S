/* JIT machine-code templates. Hole magics mark patch sites;
 * scripts/gen/gen_jit_templates.py assembles this file, scans the
 * magics and emits jit_templates_generated.hpp with the offsets.
 * Never hand-write byte arrays: the assembler owns every offset. */
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

.globl tpl_nop_jit
tpl_nop_jit:
    nop
.globl tpl_nop_jit_end
tpl_nop_jit_end:

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

.globl tpl_mov_slot_imm
tpl_mov_slot_imm:
    movabs rax,0x3B4153A5000000C3
    mov    QWORD PTR [rdi+0x6B415372],rax
.globl tpl_mov_slot_imm_end
tpl_mov_slot_imm_end:

.globl tpl_add_slot_imm
tpl_add_slot_imm:
    add    QWORD PTR [rdi+0x6B415367],0x6B41536F
.globl tpl_add_slot_imm_end
tpl_add_slot_imm_end:

.globl tpl_sub_slot_imm
tpl_sub_slot_imm:
    sub    QWORD PTR [rdi+0x6B415367],0x6B41536F
.globl tpl_sub_slot_imm_end
tpl_sub_slot_imm_end:

.globl tpl_and_slot_imm
tpl_and_slot_imm:
    and    QWORD PTR [rdi+0x6B415367],0x6B41536F
.globl tpl_and_slot_imm_end
tpl_and_slot_imm_end:

.globl tpl_or_slot_imm
tpl_or_slot_imm:
    or     QWORD PTR [rdi+0x6B415367],0x6B41536F
.globl tpl_or_slot_imm_end
tpl_or_slot_imm_end:

.globl tpl_xor_slot_imm
tpl_xor_slot_imm:
    xor    QWORD PTR [rdi+0x6B415367],0x6B41536F
.globl tpl_xor_slot_imm_end
tpl_xor_slot_imm_end:

.globl tpl_mov_slot_slot
tpl_mov_slot_slot:
    mov    rax,QWORD PTR [rdi+0x6B41536B]
    mov    QWORD PTR [rdi+0x6B415372],rax
.globl tpl_mov_slot_slot_end
tpl_mov_slot_slot_end:

.globl tpl_lea_slot
tpl_lea_slot:
    mov    rax,QWORD PTR [rdi+0x6B415365]
    add    rax,0x6B415367
    mov    QWORD PTR [rdi+0x6B415372],rax
.globl tpl_lea_slot_end
tpl_lea_slot_end:

.globl tpl_lea_slot32
tpl_lea_slot32:
    mov    eax,DWORD PTR [rdi+0x6B415365]
    add    eax,0x6B415367
    mov    QWORD PTR [rdi+0x6B415372],rax
.globl tpl_lea_slot32_end
tpl_lea_slot32_end:

.globl tpl_lea_idx_slot
tpl_lea_idx_slot:
    mov    rax,QWORD PTR [rdi+0x6B415365]
    mov    r8,QWORD PTR [rdi+0x6B415370]
    shl    r8,0x5B
    add    rax,r8
    add    rax,0x6B415367
    mov    QWORD PTR [rdi+0x6B415372],rax
.globl tpl_lea_idx_slot_end
tpl_lea_idx_slot_end:

.globl tpl_lea_idx_slot32
tpl_lea_idx_slot32:
    mov    rax,QWORD PTR [rdi+0x6B415365]
    mov    r8,QWORD PTR [rdi+0x6B415370]
    shl    r8,0x5B
    add    rax,r8
    add    rax,0x6B415367
    mov    eax,eax
    mov    QWORD PTR [rdi+0x6B415372],rax
.globl tpl_lea_idx_slot32_end
tpl_lea_idx_slot32_end:

.globl tpl_shift_imm_slot
tpl_shift_imm_slot:
    push   rdi
    mov    esi,0x6B41536A
    mov    edx,0x6B415379
    mov    ecx,0x6B41536C
    mov    r8d,0x6B415369
    movabs rdi,0x1B4153A5000000C1
    movabs r11,0x2B4153A5000000C2
    call   r11
    pop    rdi
.globl tpl_shift_imm_slot_end
tpl_shift_imm_slot_end:

.globl tpl_alu_reg_slot
tpl_alu_reg_slot:
    push   rdi
    mov    esi,0x6B41536A
    mov    edx,0x6B41536B
    mov    ecx,0x6B41536C
    mov    r8d,0x6B415369
    movabs rdi,0x1B4153A5000000C1
    movabs r11,0x2B4153A5000000C2
    call   r11
    pop    rdi
.globl tpl_alu_reg_slot_end
tpl_alu_reg_slot_end:

.globl tpl_alu_mem_rip_slot
tpl_alu_mem_rip_slot:
    push   rdi
    mov    esi,0x6B41536A
    movabs rdx,0x8B4153A5000000C8
    mov    ecx,0x6B41536C
    mov    r8d,0x6B415369
    movabs rdi,0x1B4153A5000000C1
    movabs r11,0x2B4153A5000000C2
    call   r11
    pop    rdi
.globl tpl_alu_mem_rip_slot_end
tpl_alu_mem_rip_slot_end:

.globl tpl_load_rip_slot
tpl_load_rip_slot:
    push   rdi
    movabs rsi,0x8B4153A5000000C8
    movabs rdi,0x1B4153A5000000C1
    mov    edx,0x6B415369
    movabs r11,0x2B4153A5000000C2
    call   r11
    pop    rdi
    mov    QWORD PTR [rdi+0x6B415372],rax
.globl tpl_load_rip_slot_end
tpl_load_rip_slot_end:

.globl tpl_grp3_slot
tpl_grp3_slot:
    push   rdi
    mov    rsi,QWORD PTR [rdi+0x6B41536B]
    mov    edx,0x6B415369
    mov    ecx,0x6B41536C
    movabs rdi,0x1B4153A5000000C1
    movabs r11,0x2B4153A5000000C2
    call   r11
    pop    rdi
.globl tpl_grp3_slot_end
tpl_grp3_slot_end:

.globl tpl_imul23_slot
tpl_imul23_slot:
    push   rdi
    mov    esi,0x6B415372
    mov    edx,0x6B41536B
    movabs rcx,0x3B4153A5000000C3
    mov    r8d,0x6B415369
    movabs rdi,0x1B4153A5000000C1
    movabs r11,0x2B4153A5000000C2
    call   r11
    pop    rdi
.globl tpl_imul23_slot_end
tpl_imul23_slot_end:

.globl tpl_cmov_mem_slot
tpl_cmov_mem_slot:
    push   rdi
    mov    rsi,QWORD PTR [rdi+0x6B415365]
    add    rsi,0x6B415367
    mov    edx,0x6B41536D
    mov    ecx,0x6B41536A
    mov    r8d,0x6B415369
    movabs rdi,0x1B4153A5000000C1
    movabs r11,0x2B4153A5000000C2
    call   r11
    pop    rdi
.globl tpl_cmov_mem_slot_end
tpl_cmov_mem_slot_end:

.globl tpl_alu_mem_imm_slot
tpl_alu_mem_imm_slot:
    push   rdi
    mov    rsi,QWORD PTR [rdi+0x6B415365]
    add    rsi,0x6B415367
    movabs rdx,0x3B4153A5000000C3
    mov    ecx,0x6B41536C
    mov    r8d,0x6B415369
    movabs rdi,0x1B4153A5000000C1
    movabs r11,0x2B4153A5000000C2
    call   r11
    pop    rdi
.globl tpl_alu_mem_imm_slot_end
tpl_alu_mem_imm_slot_end:

.globl tpl_imul_mem_slot
tpl_imul_mem_slot:
    push   rdi
    mov    rsi,QWORD PTR [rdi+0x6B415365]
    add    rsi,0x6B415367
    mov    edx,0x6B415372
    mov    ecx,0x6B415369
    movabs rdi,0x1B4153A5000000C1
    movabs r11,0x2B4153A5000000C2
    call   r11
    pop    rdi
.globl tpl_imul_mem_slot_end
tpl_imul_mem_slot_end:

.globl tpl_ports_slot
tpl_ports_slot:
    push   rdi
    mov    esi,0x6B41536C
    mov    edx,0x6B415369
    movabs rdi,0x1B4153A5000000C1
    movabs r11,0x2B4153A5000000C2
    call   r11
    pop    rdi
.globl tpl_ports_slot_end
tpl_ports_slot_end:

.globl tpl_grp3_mem_slot
tpl_grp3_mem_slot:
    push   rdi
    mov    rsi,QWORD PTR [rdi+0x6B415365]
    add    rsi,0x6B415367
    mov    edx,0x6B415369
    mov    ecx,0x6B41536C
    movabs rdi,0x1B4153A5000000C1
    movabs r11,0x2B4153A5000000C2
    call   r11
    pop    rdi
.globl tpl_grp3_mem_slot_end
tpl_grp3_mem_slot_end:

.globl tpl_alu_mem_idx_slot
tpl_alu_mem_idx_slot:
    push   rdi
    mov    rsi,QWORD PTR [rdi+0x6B415365]
    mov    r8,QWORD PTR [rdi+0x6B415370]
    shl    r8,0x5A
    add    rsi,r8
    add    rsi,0x6B415367
    mov    edx,0x6B41536B
    mov    ecx,0x6B41536C
    mov    r8d,0x6B415369
    movabs rdi,0x1B4153A5000000C1
    movabs r11,0x2B4153A5000000C2
    call   r11
    pop    rdi
.globl tpl_alu_mem_idx_slot_end
tpl_alu_mem_idx_slot_end:

.globl tpl_shift_cl_slot
tpl_shift_cl_slot:
    push   rdi
    mov    esi,0x6B41536A
    mov    rdx,QWORD PTR [rdi+0x6B41537A]
    mov    ecx,0x6B41536C
    mov    r8d,0x6B415369
    movabs rdi,0x1B4153A5000000C1
    movabs r11,0x2B4153A5000000C2
    call   r11
    pop    rdi
.globl tpl_shift_cl_slot_end
tpl_shift_cl_slot_end:

.globl tpl_pxor_slot
tpl_pxor_slot:
    push   rdi
    mov    esi,0x6B415372
    mov    edx,0x6B41536B
    movabs rdi,0x1B4153A5000000C1
    movabs r11,0x2B4153A5000000C2
    call   r11
    pop    rdi
.globl tpl_pxor_slot_end
tpl_pxor_slot_end:

.globl tpl_alu_mem_slot
tpl_alu_mem_slot:
    push   rdi
    mov    esi,0x6B41536A
    mov    rdx,QWORD PTR [rdi+0x6B415365]
    add    rdx,0x6B415367
    mov    ecx,0x6B41536C
    mov    r8d,0x6B415369
    movabs rdi,0x1B4153A5000000C1
    movabs r11,0x2B4153A5000000C2
    call   r11
    pop    rdi
.globl tpl_alu_mem_slot_end
tpl_alu_mem_slot_end:

.globl tpl_jmp_imm_helper
tpl_jmp_imm_helper:
    push   rdi
    movabs rsi,0x7B4153A5000000C7
    movabs rdi,0x1B4153A5000000C1
    movabs r11,0x2B4153A5000000C2
    call   r11
    pop    rdi
    ret
.globl tpl_jmp_imm_helper_end
tpl_jmp_imm_helper_end:

.globl tpl_jcc_helper
tpl_jcc_helper:
    push   rdi
    movabs rsi,0x9B4153A5000000C9
    movabs rdx,0xAB4153A5000000CA
    mov    ecx,0x6B41536D
    movabs rdi,0x1B4153A5000000C1
    movabs r11,0x2B4153A5000000C2
    call   r11
    pop    rdi
    ret
.globl tpl_jcc_helper_end
tpl_jcc_helper_end:

.globl tpl_leave_helper
tpl_leave_helper:
    push   rdi
    movabs rdi,0x1B4153A5000000C1
    movabs r11,0x2B4153A5000000C2
    call   r11
    pop    rdi
.globl tpl_leave_helper_end
tpl_leave_helper_end:

.globl tpl_alu_imm_helper
tpl_alu_imm_helper:
    push   rdi
    mov    esi,0x6B41536A
    movabs rdx,0x3B4153A5000000C3
    mov    ecx,0x6B415369
    movabs rdi,0x1B4153A5000000C1
    movabs r11,0x2B4153A5000000C2
    call   r11
    pop    rdi
.globl tpl_alu_imm_helper_end
tpl_alu_imm_helper_end:

.globl tpl_alu_reg_helper
tpl_alu_reg_helper:
    push   rdi
    mov    esi,0x6B41536A
    mov    rdx,QWORD PTR [rdi+0x6B41536B]
    mov    ecx,0x6B415369
    movabs rdi,0x1B4153A5000000C1
    movabs r11,0x2B4153A5000000C2
    call   r11
    pop    rdi
.globl tpl_alu_reg_helper_end
tpl_alu_reg_helper_end:

.globl tpl_cmov_reg_helper
tpl_cmov_reg_helper:
    push   rdi
    mov    esi,0x6B415372
    mov    rdx,QWORD PTR [rdi+0x6B41536B]
    mov    ecx,0x6B41536D
    mov    r8d,0x6B415369
    movabs rdi,0x1B4153A5000000C1
    movabs r11,0x2B4153A5000000C2
    call   r11
    pop    rdi
.globl tpl_cmov_reg_helper_end
tpl_cmov_reg_helper_end:

.globl tpl_setcc_reg_helper
tpl_setcc_reg_helper:
    push   rdi
    mov    esi,0x6B415372
    mov    edx,0x6B41536D
    movabs rdi,0x1B4153A5000000C1
    movabs r11,0x2B4153A5000000C2
    call   r11
    pop    rdi
.globl tpl_setcc_reg_helper_end
tpl_setcc_reg_helper_end:

.globl tpl_push_reg_helper
tpl_push_reg_helper:
    push   rdi
    mov    rsi,QWORD PTR [rdi+0x6B41536A]
    movabs rdi,0x1B4153A5000000C1
    movabs r11,0x2B4153A5000000C2
    call   r11
    pop    rdi
.globl tpl_push_reg_helper_end
tpl_push_reg_helper_end:

.globl tpl_push_imm_helper
tpl_push_imm_helper:
    push   rdi
    movabs rsi,0x3B4153A5000000C3
    movabs rdi,0x1B4153A5000000C1
    movabs r11,0x2B4153A5000000C2
    call   r11
    pop    rdi
.globl tpl_push_imm_helper_end
tpl_push_imm_helper_end:

.globl tpl_pop_reg_helper
tpl_pop_reg_helper:
    push   rdi
    mov    esi,0x6B41536A
    movabs rdi,0x1B4153A5000000C1
    movabs r11,0x2B4153A5000000C2
    call   r11
    pop    rdi
.globl tpl_pop_reg_helper_end
tpl_pop_reg_helper_end:

.globl tpl_cmp_mem_call
tpl_cmp_mem_call:
    mov    edx,0x6B41536A
    mov    ecx,0x6B415369
    movabs rdi,0x1B4153A5000000C1
    movabs r11,0x2B4153A5000000C2
    call   r11
    pop    rdi
.globl tpl_cmp_mem_call_end
tpl_cmp_mem_call_end:

/* cmp [base+disp], slot: the load address prefix plus the cmp helper tail.
 * These used to be built by cmp_memory_code<kLoadSlot, 16>(), splicing the
 * first 16 bytes of the load template; the assembler expresses the same
 * thing directly and owns the offsets. */
.globl tpl_cmp_mem_slot
tpl_cmp_mem_slot:
    push rdi
    mov rsi, [rdi + 0x6B415365]
    add rsi, 0x6B415367
    mov edx, 0x6B41536A
    mov ecx, 0x6B415369
    movabs rdi, 0x1B4153A5000000C1
    movabs r11, 0x2B4153A5000000C2
    call r11
    pop rdi
.globl tpl_cmp_mem_slot_end
tpl_cmp_mem_slot_end:

.globl tpl_cmp_mem_idx_slot
tpl_cmp_mem_idx_slot:
    push rdi
    mov rsi, [rdi + 0x6B415365]
    mov r8, [rdi + 0x6B415370]
    shl r8, 0x5B
    add rsi, r8
    add rsi, 0x6B415367
    mov edx, 0x6B41536A
    mov ecx, 0x6B415369
    movabs rdi, 0x1B4153A5000000C1
    movabs r11, 0x2B4153A5000000C2
    call r11
    pop rdi
.globl tpl_cmp_mem_idx_slot_end
tpl_cmp_mem_idx_slot_end:
