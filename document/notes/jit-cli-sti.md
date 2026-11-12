# cli/sti 复用端口模板（2026-11-13，69d0100）

## 一个 helper 两种语义

`cli`/`sti` 只翻 CPU 的标志位，没有操作数、也没有端口。但它们和 `in`/`out`
有一个共同点：都只需要"助记符"这一个编译期常量。

所以模板直接复用 `kPortsSlot`（助记符洞 + 宽度洞），helper 在 `jit_ports`
里按助记符分派：

```cpp
    if (shape.mnemonic == Mnemonic::kCli || shape.mnemonic == Mnemonic::kSti) {
        (void)execute_system(shape, 0); // flag-only: the STI shadow lives there
        return;
    }
```

一个 helper 承担两种语义确实不漂亮，但它省掉了独立枚举、类声明、thunk、
dispatch case 四样东西——在 600 行的账上，这笔交易划算。**STI 的影子
规则（下一条指令执行后才重新允许中断）因此只有 execute_system 那一份**，
这一点比命名清晰更重要。

## 结果

99.95% → **99.96%**，双内核 console 逐字节一致。319 差分用例，九段验收
全绿。失败块 413 → 408。

## 剩下的路

回退 14,594 次，构成如下：

| 形态 | 次数 | 成本 |
|---|---|---|
| `test [rsi+rax*8],r15` | 1342 | SIB + 反向操作数，要新模板 |
| `cmp byte [rdi+rax],0` | 285 | 同上 |
| `movups/movdqu [base+index]` | 768 | 现有 SSE 模板要加 SIB |
| `lgdt`/`fxsave64`/`rdmsr`/`iret` | 各十几 | 要建模架构状态 |

每一项都能做，但都在 0.001 到 0.005 个百分点之间，且需要新模板或新的
架构状态建模。**99.96% 已经是"几乎 100%"**——剩下的是启动路径上每条
只跑一两次的指令。
