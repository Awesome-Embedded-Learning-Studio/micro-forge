# test/cmp 的 index 形式（2026-11-13，d6a1499）

## 模板

`kAluMemIdxSlot`（71 字节）把 `kLoadIdxSlot` 那套 SIB 地址算术搬过来：

```
push rdi ; mov rsi,[rdi+base32] ; mov r8,[rdi+idx32] ; shl r8,shift8 ;
add rsi,r8 ; add rsi,disp32 ; mov edx,src32 ; mov ecx,mnem32 ;
mov r8d,width32 ; mov rdi,cpu64 ; movabs r11,helper64 ; call r11 ; pop rdi
```

后面接四个参数：源（test 是寄存器槽、cmp 是立即数）、助记符、宽度。
两种指令都不写回内存，所以一个 helper 够用——test 走
`alu_logic(mem & reg)`，cmp 走 `alu_sub(mem, imm)`。

## 收窄条件救了一次内核

第一版笼统地允许 test/cmp 的所有源形式。跑内核直接崩：输出到 49 字节就
停住，coverage 掉到 99.93%。

原因：内核里有 `test [mem], imm`，而我的 helper 按"源是寄存器槽"去解释
立即数——把 0x1F 这种小数字当槽偏移，读到的是无关寄存器，标志全错，内核
在字符串处理里跑飞。

修法是条件按指令分开：

```cpp
((mnemonic == kTest && ops[1].kind == kReg) ||
 (mnemonic == kCmp && ops[1].kind == kImm))
```

**差分用例先过、内核却崩**，这种时候要立刻怀疑"用例没覆盖的形态"。我加
的那条用例正好是 test 的寄存器源，恰好错过了内存源这条路径。

## 用例的 REX 位

写用例时把 `test [r12+rcx],rax` 编成了 `4C 85 04 0C`——`4C` 只有 W 和 R
位，没有 B，于是 SIB 的 base=100 解释成 rsp 而不是 r12，读到 0xF000 的
零页，标志成了 ZF+PF。正确编码是 `49 85 04 0C`（B 位表示扩展 base）。

REX 的四个位（W/R/X/B）每次都要对一遍，尤其是 base 和 index 都在 SIB 里
的时候。

## 结果

99.96% → **99.96%**（1627 次回退被吃掉，百分比只动了 0.005），双内核
console 逐字节一致。320 差分用例，九段验收全绿。失败块 408 → 406。
