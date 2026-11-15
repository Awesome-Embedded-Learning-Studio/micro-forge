# JIT 块链接 stage 3：jcc 受护链桩（2026-11-13）

stage 2（`517208a`）只链 jmp-imm → jmp-imm。stage 3 把 jcc 也变成可链
出口，并让所有 terminated 块都可作为链目标。分支 `feat/jit_support`。

## 机制

- **jit_chain_ 扩到 4 槽**：`[3]` 是出口快照——四个控制 helper（ret/
  call/jmp/jcc）写 `jit_next_rip_` 时同步写 `jit_chain_[3]`，host 侧可读。
- **55B 受护桩**（jcc 块尾，盖模板 ret）：
  `movabs r11,&chain; add [r11+8],count; cmp qword [r11+24],guard32;
  jne .ret; mov/cmp/jae（due 表面化）; dec [r11]; jnz rel32; rearm; ret`。
  guard 常量 = jcc 的 fallthrough rip（`cmp r/m64, imm32` 符号扩展，
  内核高半地址 0xffffffff80xxxxxx 恰好可表示）。helper 走了 fallthrough
  才链跳，taken 则正常 ret 回 C++——这正是"受护"的含义：链永远不用
  重算条件。
- **19B 记账尾**（ret/call 块）：`movabs; add [r11+8],count; ret`。它们
  总是回 C++，但可能被别的块的 hop 吃掉，只有自己的尾能替它记账。
- **try_link 放宽目标**：任何 sets_next（terminated）块都可作目标——
  它们都自带记账尾（45B/55B/19B 三档）。unterminated 目标仍然禁止
  （无记账、且被吃后无人推进 rip，会重执行）。

## 踩过的两个坑（都值得记）

1. **guard 段 `mov r11d, imm32` 毁掉 r11**（&chain 指针）——桩在 add 处
   SIGSEGV，r11 残留 guest 值 0x1015。改用 `cmp qword [r11+24], imm32`
   直接与内存比较，r11 全程不动，桩还从 61B 缩到 55B。
2. **add 记账必须在 guard 比较之前**：第一版 taken 路径（jne→ret）跳过
   add，NOCHAIN/CHAIN 双双少记（t_link5 icount 70 vs 162）。顺序换成
   movabs→add→cmp→jne 后两模式精确一致。

## 验证与性能

- `t_link5`（jmp→cmp/jae 循环，真正链到 jcc 块）/`t_link6`（jmp→jmp）：
  CHAIN == NOCHAIN == 解释器，icount 分毫不差。
- 内核 probe：`kind=1 rip=0xffffffff80201061 icount=28135989 console=1687
  coverage=99.9625%`，console 与解释器逐字节一致；330 差分 + 九段全绿。
- **性能持平**（CHAIN 58.7 vs NOCHAIN 59.1 Mips，同窗口）：内核里只发生
  268 次链接——jmp-imm 的常量目标多是冷门，而 jcc 的热出口（循环回边）
  是 taken 不是 fallthrough。真正的大头是"unterminated 块作为链目标"：
  直线码的 fallthrough 目标几乎全是 unterminated 块。那要求被吃块自己
  推进 rip（自续尾：写 `[3]` 快照 + C++ 侧读回），是下一批的工程量。
