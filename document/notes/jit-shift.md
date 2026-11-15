# 移位指令 JIT lowering（2026-11-12，000275d）

## 机制

`kShiftImmSlot`（46 字节，中段模板，fall-through）：

```
push rdi ; mov esi,slot32 ; mov edx,count32 ; mov ecx,mnem32 ;
mov r8d,width32 ; mov rdi,cpu64 ; movabs r11,helper64 ; call r11 ; pop rdi
```

四个编译期量全部 patch 进模板：目标槽偏移、移位数、助记符码、操作数宽度。

helper `jit_shift` 只做三件事：取 `regs_[slot/8]` 的引用、计数掩码后判断
是否为 0、调解释器的 `alu_shift`。标志算法因此与解释器完全同源，不存在
两套实现漂移的风险。计数为 0 时按 x86 规定不动任何标志也不写回，与
dispatch 里的 `eff == 0` 守卫一致。

覆盖 SHL/SHR/SAR/ROL/ROR/RCL/RCR，宽度 2/4/8。

## 参数语义踩错一次

第一版模板是 `mov rsi,[rdi+slot32]`，把寄存器的**值**读进 rsi 传过去，
helper 却按 `regs_[slot/8]` 把它当**槽偏移**用，于是拿寄存器值去寻址寄存器
数组，越界写当场段错误。改成模板只传槽偏移（`mov esi,slot32`，5 字节），
helper 自己读-改-写，模板反而从 48 缩到 46 字节。

教训：helper 的 ABI 里，传值还是传槽，要在模板写第一行之前定死，两者
差一个内存解引用。

## 测试

差分用例四条：`shl r64,imm8`、`shr r32,imm8`、`sar r64,1`（D1 形式）、
`shl r64,0`（标志不变）。注意差分夹具里 `.flags` 既是期望值，播种却固定
用 `kFlags` 常量，所以移位这类改标志的指令必须把执行后的预期值填进去，
否则断言必挂。

写期望值时会撞 tidy 的 `bugprone-signed-bitwise`：`static_cast<int64_t>(x) >> 1`
被当成有符号位运算。算术移位的结果直接写成常量最省事。

## 结果

老内核 96.57% → **96.99%**，新内核 96.98%，Mips 约 78-82。
300 差分用例，九段验收全绿，双内核 console 逐字节一致。

剩余失败主体：`movzx reg,[mem]`（277 次）、`add/sub reg,reg`（约 340）、
`lea reg,[mem]`（102）、`cmp [mem],imm`（86）、`pxor xmm,xmm`（75）。
