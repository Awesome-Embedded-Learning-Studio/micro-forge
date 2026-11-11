# call reg64/rel32 JIT lowering（2026-11-11，c9d3441）

## 机制

复用 ret 批次的块退出状态（jit_next_rip_/jit_next_set_）：

- `kCallRegHelper` / `kCallImmHelper`（各 56B）：ABI rdi=cpu、rsi=target
  （reg 模板从 [rdi+reg32] 读，imm 模板 movabs patched）、rdx=call 自身
  rip、rcx=返回地址。helper `jit_call` 复用解释器 push_value 压返回址，
  成功则退出目标=target，压栈故障则 suspend 一步回解释器重执行。
- compile_block 在 call 处截断块（模板自带宿主 ret，budget-1），与 ret
  同款。call [mem]（FF /2 mem）仍留解释器，下批做。

## 栈对齐坑（本轮唯一真 bug）

imm 模板最初为省字节去掉 push/pop rdi：进入 JIT 页时 rsp%16==8，call
r11 后 helper 里 rsp%16==0，违反 SysV——**GCC 在 helper 深处用 movdqa
访问 16 字节对齐的 spill 槽直接 SIGSEGV**（差分 child status=11，gdb
看到崩在 RamRegion::write_u64 的 movdqa 而非 JIT 页本身，极具迷惑性）。
教训：**任何 helper-call 模板在 call r11 前必须保证 rsp%16==0**——
有 [rdi+…] 内存读的模板天然带 push rdi；纯 imm64 模板也要补。

其他小坑：单用例 clang-tidy 尾逗号（readability-trailing-comma）会挂
tidy 门；jit_cpu.cpp 超 600 行时优先压 helper 体的 if/else 分支为
三元式（ret/call 各省 3-4 行）。

## 结果

老内核 78.63% → 80.54%，新内核 → 80.53%（ret+call 两轮合计 +1.9pp）。
289 差分用例，477 项 GoogleTest，九段验收全绿，console 逐字节一致。
块链接三件套只剩 jcc（一个块两条出口，下批硬骨头）。
