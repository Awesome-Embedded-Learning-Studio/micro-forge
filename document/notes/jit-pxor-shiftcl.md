# pxor 与 shift reg,cl 落地（2026-11-12，7f862d4）

## pxor 不需要新成员

xmm 的读写接口 `xmm()` / `set_xmm()` 在 public 段，所以 helper 不必是
`Cpu` 的成员函数——写成 `jit_alu.cpp` 匿名 namespace 里的自由函数即可，
`extern "C"` 签名直接对上模板。少一个类声明，在 600 行上限下很值钱。

模板 35 字节：push rdi、两个 xmm 字节槽分别进 esi/edx、cpu64、helper64、
call、pop。helper 读两个 16 字节寄存器、异或、写回。

## shift reg,cl 零新 helper

模板 48 字节，跟移位立即数版几乎一样，只是第二个洞填的是 **cl 的槽偏移**，
模板运行时 `mov rdx,[rdi+clsl32]` 把 cl 的值读出来当计数。现成的 `jit_shift`
本来就按宽度掩码计数，所以 cl 的 0x81 会被掩成 0x01，与解释器一致。

## 分派位置踩了一次

新增的 `translate_alu_extra()` 第一版挂在 `translate_alu` 调用旁边，结果
pxor 永远走不到：那之前有一道"目标操作数必须是通用寄存器"的守卫，
`operand_slot` 对 xmm 直接返回空，函数提前返回 nullopt。

改到守卫之前就通了。**这道守卫的位置记下来**：任何以 xmm 为目标的形态
都必须排在它前面。

## 失败形态的读法

给 `MICRO_FORGE_JIT_FAILSTATS` 加上指令地址后，直接把内核字节dump 出来
对照，三种剩余形态一目了然：

- `48 6B D2 58` → 三操作数 imul，带立即数
- `48 2B 05 87 DA 04 00` → sub 的 rip-relative 内存操作数
- `44 0F B6 04 08` → movzx 的 SIB index 形式

rip-relative 的地址是"下一条指令地址 + disp"，编译期就能算出来，模板只
需要 `movabs rdx,imm64`，比 base+disp 的 14 字节还短两字节。下一批先吃它。

## 结果

老内核 97.12% → **97.37%**，新内核 97.36%，双内核 console 逐字节一致。
307 差分用例，九段验收全绿。Mips 约 80。
