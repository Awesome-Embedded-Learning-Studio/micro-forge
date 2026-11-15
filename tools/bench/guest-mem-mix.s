// ips bench guest 2: load/store mix (mov r64/[r64], add, dec, jnz,
// 6 insns/iter). Trip count comes from the loop cell at 0x80000.
    .intel_syntax noprefix
    .text
    .globl _start
_start:
    mov    rax, 0x80000
    mov    rcx, [rax]
    mov    rsi, 0x81000
    xor    r10, r10
1:  mov    r8, [rsi]
    add    r8, rcx
    mov    [rsi], r8
    inc    r10
    dec    rcx
    jnz    1b
    mov    rdi, 0
    mov    rax, 60
    syscall
