# rip-relative 内存形式（2026-11-12，ea95a75）

## 为什么这批便宜

rip-relative 的有效地址 = 下一条指令地址 + disp，两者都是编译期已知量。所以
模板里不需要从寄存器读 base、也不需要 add 位移，一条 `movabs rdx,imm64` 就
把地址放好了——比 base+disp 那套 14 字节还短。

三种形态：

| 指令 | 模板 | helper |
|---|---|---|
| add/adc/sub/sbb/and/or/xor reg,[rip+disp] | kAluMemRipSlot 51B | jit_alu_mem（复用） |
| movzx reg,[rip+disp] | kLoadRipSlot 48B | jit_load（复用） |
| lea reg,[rip+disp] | kMovAbsSlot 17B（复用纯宿主） | 无 |

lea 这条最省：结果本身就是那个常量，`movabs rax,imm64 ; mov [rdi+dst],rax`
就够了，跟前面的 movabs 落地用的是同一个模板。

**两个新模板，零新 helper**。helper 的 ABI 里地址本来就是普通的 uint64，
编译期算好的常量和运行时算出来的地址没有区别。

## 行数又撑爆两个文件

translate.cpp 到 604、测试用例表到 613。处理：

- lea 的 rip 分支挪进 `translate_alu_extra`，顺带给它加一个 `rip` 参数
  （默认 0，只有 lea 用得上）。
- 差分用例拆出 `tests/jit_diff_cases_extra.cpp`，pxor、移位、rip 三类都在
  新文件里，`alu_cases()` 末尾调用 `extra_cases()` 合并。tests/CMakeLists.txt
  的 add_executable 要跟着加文件名。

## 用例的位移算错一次

`movzx eax,byte [rip+disp]` 的编码是 `0F B6 05 <disp32>`，**7 字节**，不是
6 字节（0F B6 两字节 + modrm + 4 字节位移）。按 6 算出来的 disp 让读数偏了
一个字节，0x10 变成 0x32。写 rip-relative 用例时先数清指令长度。

## 结果

老内核 97.37% → **97.39%**，新内核 97.38%，双内核 console 逐字节一致。
310 差分用例，九段验收全绿。

失败构成已经进入长尾：lgdt 34、fxsave64 16、rdmsr 15、mov byte[mem],imm 15，
其余都低于 15，合计 659 个块。再往上要逐个吃小众指令了。
