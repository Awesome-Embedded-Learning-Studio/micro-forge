# jmp/jcc JIT lowering：块链接完成（2026-11-11，ffcfafc）

## 机制

- `kJmpImmHelper`（36B）：rsi=target（patched imm64），helper jit_jmp
  写 jit_next_rip_。
- `kJccHelper`（51B）：rsi=taken、rdx=fall、ecx=cc（三个编译期 patch），
  helper jit_jcc 复用解释器 condition() 求值后二选一写退出目标。
  jcc 是纯标志运算、无访存 → **没有故障路径**，比 ret/call 简单得多。
- compile_block 的块终止集合扩成 switch 判定的 ret/call/jmp/jcc，
  全部自带宿主 ret 结尾（budget-1）。
- jit_cpu.cpp 因此超 600 行，控制流 helper+thunk 拆到新文件
  jit_control.cpp（69 行）。

## 为什么收益爆炸

jcc 在内核指令流里无处不在，此前每条 jcc 都截断块 → 块平均长度
被压得很短，jcc 本身 + 后续指令全部落回解释器。两条出口打通后：

- 覆盖率 80.54% → **97.91%**（老内核，+17.4pp！），新内核 97.90%。
- Mips 77 → ~103-145（块连贯，解释器往返大幅减少）。
- failed 块 3044 → 1644（jcc 不再导致编译失败）。

## 坑

1. clang-format 与 tidy-trailing-comma 会为一个超长 Patch 行打架：
   折行就丢尾逗号、加逗号就被折回去。解法=提出局部变量缩短行
   （顺带触发 modernize-use-auto：cast 初始化要用 const auto）。
2. 差分用例：kFlags=0x8D5 的 ZF=1，je taken / jne fall 各一例即可
   覆盖两分支；jmp 用 rel8。

## 结果

292 差分用例、481 项 GoogleTest、九段验收全绿，console 逐字节一致。
剩余 ~2.1%：push [mem]/call [mem]/jmp reg/movx 带 index/ret imm16 等
边角形态。目标 ~100% 已在射程内。
