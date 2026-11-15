# 三操作数 imul（2026-11-12，8fbfe24）

## 同一个重建手法

一操作数 grp3 那轮验证了"构造最小指令形状交给解释器"可行，这轮把它用到
三操作数 imul：`execute_imul23` 要读 ops[0]（目标）、ops[1]（源）、ops[2]
（立即数）和 opsize，所以形状里要填真实寄存器编号，不能只填助记符。

```cpp
    arch::x86_64::DecodedInsn shape{};
    shape.opsize = w8;
    shape.ops[0] = {.kind = kReg, .size = w8, .reg = dreg};
    shape.ops[1] = {.kind = kReg, .size = w8, .reg = sreg};
    shape.ops[2] = {.kind = kImm, .size = w8, .disp = sdisp};
    (void)execute_imul23(shape, 0);
```

`rip_after` 传 0 是安全的：三个操作数都是寄存器与立即数，没有 rip 相对
寻址，`read_operand` 不会用到它。

## 两个构建期的错

1. `kImul23Slot` 声明了 47 字节，实际编码 51。五个参数（目标槽、源槽、
   imm64、宽度）加上两条重定位，光指令就占满了 51。**先按字节累加算长度，
   再写数组**，这是第 N 次栽在这上面。
2. `Operand` 的 designated initializer 顺序必须跟声明一致：`kind`、
   `size`、`reg`。我按 `kind`、`reg`、`size` 写，编译器直接拒。

## 结果

老内核 97.57% → **97.58%**，新内核 97.57%，双内核 console 逐字节一致。
315 差分用例，九段验收全绿。失败块 537 → 518。

## 边际效益的观察

从 97.4% 往上，每批改动只有 0.01-0.1 个百分点。实测每个失败块平均被执行
一千三百来次，而失败块的指令数平均两三条——也就是说 518 个块加起来正好
是那 2.4% 的缺口。要在一年内摸到 99%，还得再吃掉两百多个块，而且得挑
执行权重高的那些，不是编译失败次数多的。
