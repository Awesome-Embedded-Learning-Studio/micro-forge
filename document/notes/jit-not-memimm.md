# not、ALU [mem],imm、imul [mem]（2026-11-12，9cc9c8f）

## 按权重榜往下吃

上一轮做出来的回退权重榜，这轮直接照着做。三项加起来九万多次回退：

| 形态 | 回退次数 | 做法 |
|---|---|---|
| `not rdx` | 66,304 | 复用三参数 ALU 模板，helper 只翻转槽 |
| `sub qword [mem],1` | 66,304 | 新模板 60 字节，读-算-写一体 |
| `imul rax,[mem]` | 26,448 | 读乘数后折成三操作数形状 |

## not 不需要新模板

`kAluRegHelper` 是 (slot, src, width) 三参数。not 只有一个操作数，那就把
slot 同时填进前两个洞，helper 忽略第二个。省一个 35 字节的模板，也省
一条 translate 分支的宽度门。

note：helper 用公开的 `gpr()` / `set_gpr()` 就够，所以写成自由函数，不占
类声明——600 行的账越来越紧。

## imul 的两操作数折三操作数

`imul rax,[rbp-0x50]` 的语义是 rax = rax × [mem]。`execute_imul23` 处理的
是三操作数形式 dst = src × imm。把 src 填成 dst、把从内存读出来的乘数
填进 imm 位，两边的语义就接上了，标志规则仍然只有一份。

## switch 改表驱动失败

jit_cpu.cpp 里 39 条 helper 的 `case` 占 117 行，想改成
`{id, address}` 静态表一次省掉六十行。但：

- 用 designated initializer 写，tidy 要求 `{.id=..., .address=..., }` 的
  末尾有逗号；
- 加上那个逗号，clang-format 立刻把每条展开成四行；
- 去掉逗号，tidy 又报 `readability-trailing-comma`。

这就是之前遇到过好几次的同一个死结（format 要短行、tidy 要尾逗号）。
最终回退成 switch，改成从别处压注释。

## 行数的两个办法

- cpu.hpp：只合并"连续两行都是 `///`、都不含 `@param`/`@brief`、合并后
  不超 98 列"的注释。上一版脚本漏了检查首行也是 `///`，把普通注释也并了，
  编出来 `Classification of the stop.enum class Kind` 这种东西，只能
  git checkout 重来。
- jit_cpu.cpp：删掉几句重复解释的注释（"半打补丁的模板不能成为块"这类）。

## 结果

99.09% → **99.75%**，双内核 console 逐字节一致。319 差分用例，九段验收
全绿。剩余主要回退只剩 `out dx,al`（25,757）与 `in al,dx`（12,035），
那两条要走设备模型。
