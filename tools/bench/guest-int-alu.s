// ips bench guest 1: integer ALU loop (add/sub/dec/jnz, 4 insns/iter).
// The trip count is read from the loop cell at 0x80000 so the harness
// sizes the run without reassembling.
    .intel_syntax noprefix
    .text
    .globl _start
_start:
    mov    rax, 0x80000
    mov    rcx, [rax]
    xor    r10, r10
1:  add    r10, rcx
    sub    r10, 3
    dec    rcx
    jnz    1b
    mov    rdi, 0
    mov    rax, 60
    syscall
