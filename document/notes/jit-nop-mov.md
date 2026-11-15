# nop/movabs/movzx/mov r16 与两个块 bug（2026-11-12，a25dc79）

## 新增形态

- `kNopJit`：1 字节宿主 nop。单字节 nop、`xchg ax,ax`、`0F 1F` 多字节
  padding 全部无操作，不需要 helper 也不需要 flag 建模。
- `kMovAbsSlot`：17 字节纯宿主，`movabs rax,imm64` 后 `mov [rdi+dst32],rax`。
  之前 imm 超过 int32 一律回退，内核高半地址因此全落解释器。
- `kMovSlotW`：48 字节 helper，`movzx reg,reg` 与 `mov r16,r16` 共用。
  ABI rsi=src 值、edx=src 宽度、ecx=dst 槽、r8d=zx 开关。

## 两个真 bug（本轮主要收获）

### 页留在可写状态

`compile_block` 里 `unseal()` 之后有多条 `fail_compile()` 出口：emit 失败、
helper 未匹配、patch 失败、共享 ret 失败。它们直接返回，**整页停在 rw-p**。
页上已经 seal 过的旧块全部不可执行，下次命中就 SIGSEGV。

gdb 现场很有迷惑性：崩在页首块的 `push rdi`，寄存器全正常，rsp 也正常。
`info proc mappings` 一看该页是 `rw-p` 而不是 `r-xp`，真相才出来。修法是
fail_compile 里补一次 `seal()`（幂等，已 seal 时返回 false 无副作用）。

### 块尾模板不终止取指

上一轮写的 switch：

```cpp
case Mnemonic::kRet: ... --budget; break;   // break 出 switch
if (budget == 0) { break; }                  // 才 break 出 while
```

块尾模板自带宿主 ret，但 budget 减 1 后几乎不可能变 0（budget 里还压着
几十字节），于是 continue 把后面最多 64 条指令全打包进来。执行时在块尾
ret 处返回，被吞的指令从未执行，却被 `insn_count` 计进覆盖率。

症状是覆盖率虚高（97.91% → 修正后 96.57%），而且块变长让页更快填满，
正好把上面那条 fail_compile 路径踩出来。改法是独立的 `terminated` 标志。

## 统计口径

`icount` 与解释器差 1.4%（28.13M vs 27.75M），查下来不是 bug：JIT 块是
原子的，中断只在块边界检查，比解释器逐条检查晚，idle 循环里 hlt 与中断
处理次数因此不同。预算截断实验（10 万条）能看到同样的溢出（+3 条），
判据仍是 console 逐字节一致。

## 结果

覆盖率 96.57% / 96.56%，Mips 约 70-82。296 差分用例，九段验收全绿。
剩余失败主体：`shl/shr reg,imm8`（约 1100 次）、`movzx reg,[mem]`、
`add/sub reg,[mem]`、`cmp [mem],imm`。
