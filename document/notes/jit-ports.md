# 端口 I/O 与最后几个大形态（2026-11-12，1ab11f5）

## in/out

权重榜上最后一个大头：`out dx,al`（25,757 次）与 `in al,dx`（12,035 次）。
解释器里走 `execute_ports(insn, rip_after)`，它读操作数形状、端口号来自
rdx 或立即数、设备副作用落在 `ports_` 上。

helper 的做法和 grp3 一样，重建形状：

```cpp
    const auto dx = Operand{.kind = kImplicit, .size = 2, .reg = Reg::kRdx};
    const auto acc = Operand{.kind = kImplicit, .size = w8, .reg = Reg::kRax};
    if (shape.mnemonic == Mnemonic::kIn) { ops[0] = acc; ops[1] = dx; }
    else { ops[0] = dx; ops[1] = acc; }
    (void)execute_ports(shape, 0);
```

**串口输出只有一份实现**，这是关键——JIT 与解释器的 console 逐字节一致
全靠这个。rip_after 传 0 是安全的：端口在寄存器里，两个操作数都不做
rip 相对寻址。

## div [mem]

`F7 71 14` 是 `div dword [rcx+0x14]`，两处各 1687 次。模板先算地址、把
vaddr 交给 helper，helper 读内存得到除数再走 `execute_grp3`。和寄存器
形式共用同一个执行器，商余与 #DE 的规则不会分叉。

## cmp [mem],imm

之前 `kAluMemImm` 只列了 add/sub/and/or/xor，漏了 cmp。加进去时要记得
**cmp 不写回内存**：helper 里加了 `writes` 标志，cmp 分支置 false，
store 只在 writes 时执行。

## 移位放宽到 1 字节

`shr cl,2`（1052 次）之前被"宽度只能 2/4/8"挡住。放宽后要求字节目标
**槽对齐**——AH/CH/DH/BH 落在父槽的第二个字节，helper 按整槽寻址会写
错地方，仍然回退解释器。cl 是 RCX 槽的低字节，对齐，可以走。

## 结果

99.75% → **99.94%**，双内核 console 逐字节一致。319 差分用例，九段验收
全绿。失败块 500 → 424，剩余回退 29,794 次，而且已经散开：最多的单项
1687 次，其余是启动路径上每个只跑一两次的指令。

下一步（都很小）：`movsbl ebx,sil`（1653）、`test [rsi+rax*8],r15`、
`lgdt`/`fxsave64`/`rdmsr`/`iret` 这些系统指令。
