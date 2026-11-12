// Bare-client smoke body (batch 2 acceptance corpus). This is the seed of
// the batch-3 execution acceptance client: integer ALU, branches, call/ret,
// and a syscall exit. Every instruction here must decode cleanly.
    .intel_syntax noprefix
    .text

    .globl _start
_start:
    endbr64
    xor    rbp, rbp
    mov    rsp, 0x90000

    // checksum loop: sum 1..10 into rax.
    xor    rax, rax
    xor    rcx, rcx
1:  inc    rcx
    add    rax, rcx
    cmp    rcx, 10
    jne    1b

    // expected 55 (0x37)
    mov    rbx, 0x37
    cmp    rax, rbx
    jne    3f

    // memory + index exercise
    lea    rsi, [rip+table]
    mov    dword ptr [rsi], 0x5150
    mov    edx, dword ptr [rsi]
    add    edx, 0x1000
    mov    dword ptr [rsi+4], edx
    movzx  r8d, byte ptr [rsi]
    movsx  r9, word ptr [rsi]
    shl    r8, 2
    shr    r9, 1

    // push/pop pairs
    push   rbx
    push   r12
    pop    r12
    pop    rbx

    // call/ret round trip
    call   2f
    jmp    4f
2:  mov    rdi, 0xAA
    ret

3:  // failure path: nonzero exit
    mov    rdi, 0x42
4:  // exit(rdi): syscall 60
    mov    rax, 60
    syscall
    hlt

    .align 4
table:
    .long 0
    .long 0
