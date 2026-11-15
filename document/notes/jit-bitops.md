# movsx、neg、位操作（2026-11-12，bbfd6f4）

## bts 不是 bt

热点 `48 0F BA EF 1F` 我第一眼读成 `bt rdi,0x1f`，写了半天没命中。真相是
modrm 的 reg 字段 = 5，而 group 8 的扩展位是 /4=bt、**/5=bts**、/6=btr、
/7=btc。这条是 `bts rdi,0x1f`——位测试**并置位**。

解码器给出的 mnemonic 也是 kBts（37），不是 kBt（36）。用 mini 打印
`mnemonic_name()` 一眼看穿，比对着字节猜快得多。

实现上用 `jit_bit_op(slot, index, width, op)` 一个函数覆盖四种：CF 都按
原位置设置，写回按 op 走（0 不写、1 置位、2 清位、3 取反）。四个 thunk
共用这份逻辑，没有第二份位运算。

## movsx 复用宽度模板

`kMovSlotW` 的四个洞本来是"值、源宽、目标槽、零扩展开关"。movsx 需要
的是"值、源宽、目标槽、**目标宽**"，语义不同但字节布局一样，于是直接
复用模板、换 helper：按源宽符号扩展，目标 32 位时再截断（零扩展语义）。

## neg

`jit_neg` 复用三参数模板（slot 填两次），标志走 `alu_sub(0, value)`，
和解释器同一份。

## add 的累加器形式

`48 05 imm32`（add rax,imm32）被那道 add reg,imm 分支漏掉——它只认
kReg，不认 kImplicit。这已经是 kImplicit 第二次在这条路上绊人（上一次
是 cmp），**看到"目标必须是 kReg"的分支就该想到累加器短形式**。

## 结构：helper 地址 switch 搬家

jit_cpu.cpp 因为 helper switch 涨到 615 行。上次试过改成静态表，撞上
clang-format 与尾逗号的死结。这次换个做法：**把整个 switch 搬到
jit_helpers.cpp**，每条 case 用 `return address;`（比原来的
`helper = ...; break;` 少一行），函数返回 0 表示没匹配。

jit_cpu.cpp 从 615 行回到 476 行，再也不必为加一个 helper 发愁。

## 结果

99.94% → **99.95%**，双内核 console 逐字节一致。319 差分用例，九段验收
全绿。失败块 424 → 413，剩余回退 15,645 次——已经全是启动路径上每条只
跑一两次的指令。
