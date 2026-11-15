# ALU reg,[base+disp] 落地与失败统计回归（2026-11-12，5943876）

## 模板

`kAluMemSlot`（55 字节）：

```
push rdi ; mov esi,slot32 ; mov rdx,[rdi+base32] ; add rdx,disp32 ;
mov ecx,mnem32 ; mov r8d,width32 ; mov rdi,cpu64 ; movabs r11,helper64 ;
call r11 ; pop rdi
```

地址必须在模板里算：base 是运行时寄存器值，编译期只固定它的槽偏移和
displacement。helper 收到算好的 vaddr，自己 `load` 内存、`read_gpr` 目标、
组合、`write_gpr`。

覆盖 add/adc/sub/sbb/and/or/xor 的 `reg,[base+disp]`，宽度与目标一致。
`load` 失败时 helper 直接返回不写回，和解释器 `read_operand` 失败时
不执行写回一致。

## 覆盖率几乎没动的原因

这批做完，老内核从 97.12% 到 97.12%，只少 17 个失败块。翻失败统计才
看清：剩下的 `sub reg,[mem]`、`movzx reg,[mem]` 大多是**带 index 的
形式**（`[rbx+rcx*8]`），我的分支条件要求 `!has_index`，全被挡在门外。

**跑完要有统计，不能只看百分比**。百分比动得少时，失败清单的第一行
往往直接指出缺哪个变体。

## 失败统计加回来了

`MICRO_FORGE_JIT_FAILSTATS=1` 打印 `jfail <mnemonic> o<opsize> k0_<kind> s0_<size> k1_<kind>`，
一眼能看出源操作数是不是内存形式。之前为了行数删掉，这轮 jit_cpu.cpp
有余量就恢复了，并写进 CLAUDE.md。

当前构成（按编译失败次数）：`sub reg,[mem]` 141、`lea reg,[mem]` 115、
`cmp [mem],imm` 89、`pxor xmm,xmm` 75、`xchg [mem],reg` 71、
`mov [mem],imm` 133、`movzx reg,[mem]` 60、`shl reg,cl` 57、`imul reg,reg` 56。

## 结果

305 差分用例，九段验收全绿，双内核 console 逐字节一致。Mips 约 70-77。
下一批的主线是带 index 的内存形式，那是模板里要多一段 SIB 计算的活。
