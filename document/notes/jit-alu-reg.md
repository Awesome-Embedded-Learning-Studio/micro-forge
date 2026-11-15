# add/adc/sbb reg,reg 与 thunk 声明集中（2026-11-12，b1b1191）

## 一个反直觉的发现

`add reg,reg` 一直落解释器，原因不在模板缺失，而在 `alu_helper` 表里
**根本没有 kAdd**。那张表只有 cmp/test/sub/and/or/xor/imul——add 因为
早期走专用模板（`kAddImmHelper`、`kAddMemSlot`），没有登记到通用表。

`translate_alu` 的 reg,reg 分支其实一直在，只是轮到 add 时 helper 是
`kNone`，被跳过，然后一路落到函数末尾的 nullopt。查这个花了插桩才看清：
先在 `translate_insn` 里打印 mnemoic 与 helper，一眼看到 `helper=0`。

教训：**"分支已存在"和"分支会被走到"是两件事**。看到通用分支别假设它
覆盖了某个助记符，先确认调度表里有没有那一项。

## 实现

`kAluRegSlot`（46 字节，与移位模板同构，只是第二个洞是源槽而不是计数）：

```
push rdi ; mov esi,slot32 ; mov edx,src32 ; mov ecx,mnem32 ;
mov r8d,width32 ; mov rdi,cpu64 ; movabs r11,helper64 ; call r11 ; pop rdi
```

helper 复用 `read_gpr` / `alu_add` / `alu_sub` / `write_gpr`，宽度语义
（32 位零扩展、16/8 位保留高位、AH 非对齐槽已在上游拒绝）全部走解释器
同一套函数。adc/sbb 的 carry_in 直接取 `cf_`。

**只喂 add/adc/sbb**。and/or/xor/cmp/sub 已有更短的 43 字节路径
（`kAluRegHelper`），新分支不去抢。第一版把八种全接过来，撞了
`JitAluLayoutTest`——它逐字节验证旧的洞与重定位，正好挡住了这次覆盖。

## 结构改进

26 行 `extern "C"` thunk 声明抽到 `src/arch/x86_64/jit_thunks.hpp`。
之前这些声明只在 jit_cpu.cpp 里有一份，定义散在 jit_control.cpp 和
jit_alu.cpp，签名对不上时靠链接器报错。现在定义侧也 include 同一个头，
两份签名不可能漂移。顺带把 jit_cpu.cpp 从 604 行压到 574。

内部头的 Doxygen 要写成 `/** @file ... */` 的块注释，`/// @file` 形式
通的过 lint 文字但在 file-level 检查里不被认（踩了一次）。

## 结果

老内核 96.99% → **97.09%**，新内核 97.07%，双内核 console 逐字节一致。
301 差分用例，九段验收全绿。

剩余失败：`movzx reg,[mem]` 277 次、`add/sub reg,[mem]`、`lea reg,[mem]`
102 次、`cmp [mem],imm` 86 次、`pxor xmm,xmm` 75 次。
