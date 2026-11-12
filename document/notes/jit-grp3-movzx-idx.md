# 一操作数 grp3 与 movzx 的索引形式（2026-11-12，c8261b5）

## 用"重建指令形状"复用解释器

一操作数 mul/imul/div/idiv 的解释器实现是 `execute_grp3(insn, src)`，它只
读 `insn.mnemonic` 和 `insn.opsize` 两个字段。所以 JIT helper 里可以构造
一个零初始化的 `DecodedInsn`，填上这两个字段，直接调用：

```cpp
    arch::x86_64::DecodedInsn shape{};
    shape.mnemonic = static_cast<Mnemonic>(mn);
    shape.opsize = static_cast<std::uint8_t>(width);
    (void)execute_grp3(shape, divisor);
```

rdx:rax 的拆分、CF/OF 的更新、除零与商溢出的 #DE latch，全部落在解释器
那一份实现里，JIT 侧没有第二份除法代码可以漂移。

这个手法的前提是**先确认被复用函数到底读了指令的哪几个字段**。读完发现
只读两个，就值得；如果它还要读操作数形状和 rip_after（比如三操作数
imul 的 `execute_imul23`），重建成本就上去了，得单独权衡。

## movzx 的索引形式

与 base-only 形式同一个技巧：`kLoadIdxSlot` 的原始字节（不经 `load_code<>`
的宽度替换）结尾也是整槽写 `mov [rdi+dst32],rax`，配合 `jit_load` 的零扩展
返回值，正好是 movzx 语义。

洞偏移：base@5、index@13、shift@20、disp@27、width@42、dst@64，
call@48、cpu@33。

## 行数维护

cpu.hpp 又超两行。这次省的地方是 `ExitStatus::detail` 的两行注释（合成
一行）和 `request_interrupt` 的文档段（收窄到三行）。

**Doxygen 要求每个函数有独立的 @brief 块**，试过把两个同族 helper 的注释
合并成一条，lint 立刻报"Missing Doxygen block"。想省行只能从注释的行数
上抠，不能把两条 doc 并成一条。

## 结果

老内核 97.46% → **97.57%**，新内核 97.56%，双内核 console 逐字节一致。
314 差分用例，九段验收全绿。失败块 556 → 537。

div 用例的做法：先 `xor edx,edx` 把高半清零，避免商溢出触发 #DE，
期望值是 `kLoaded / rbx` 的商 0xF，余数靠寄存器对拍验证。
