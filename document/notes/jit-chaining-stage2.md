# JIT 块链接 stage 2：jmp-imm 直接链跳（2026-11-13）

stage 1（`8d97558`）已把块退出收进 `run_jit_block` 的帧内循环。stage 2
在此基础上让 jmp-imm 块尾的 `jnz rel32` 直接跳进目标块，跳过一次 C++
分派。分支 `feat/jit_support`。

## 最终机制

- **45B 可重写链出口**（盖在模板 ret 上）：`movabs r11,&jit_chain_[0]` /
  `add [r11+8],count`（自记账）/ `mov rax,[r11+8]; cmp rax,[r11+16]; jae .ret`
  （设备到期表面化）/ `dec [r11]`（链预算）/ `jnz rel32`（未链时为 0，
  落到 re-arm 64 + ret）。构造与 `try_link` 都在 `jit_chain.cpp`。
- **懒链接**：块首次经 C++ 退出时 `try_link` 把 rel32 patch 成目标块入口
  （页 `unseal/seal` 翻转，MAP_32BIT 保证 rel32 在界内）。
- **只有 jmp-imm 块可作链目标**：链跳吃掉的块不经过 C++，只有它自己的
  出口序列能替它记账。jcc/ret/call 块没有（也不需要）自记账尾——它们
  永远不会被吃。这条约束是本批最重要的正确性决定（见下）。

## 排障记录：icount 少记引发的内核分叉

第一版把 45B 序列加到**所有** terminated 块。内核在 icount 3815781
（memmem 循环内 0x8022b310）guest RDI 比基线多 0x40——块序、RAM、此前
所有寄存器采样全部一致。定位链：

1. **MEMHASH**（probe 新增）：首个 RAM 写分歧 ~icount 3820200-3821000。
2. **REGWIN**（probe 新增，逐指令寄存器窗）：首个 guest 态分歧就是那次
   多跑一轮外层循环——同 icount 下链版"跑得更远"。
3. **JIT_TRACE 块对齐**：块调用序到 3.816M 仍完全一致 → 差异在块内部
   效果，不在控制流序。
4. **/tmp/t_link5.cpp**（32 轮 jmp→cmp/jae 循环）一锤定音：链版 icount
   102 vs 基线 162，恰好少 30 轮 × 2 条 = 被 `jnz` 链跳吃掉的 cmp+jae
   无人记账。

icount 偏移顺着 rdtsc（TSC=icount+jit_chain_[1]）和设备 deadline 表面化
时机被放大成内核行为分叉。修复即上面的"只链 jmp-imm 目标"约束；
`t_link6.cpp`（jmp→jmp→dec/jnz）验证链跳发生且记账分毫不差。

## 性能

- 早期版本回退 ~30%，perf 定位到 **run_jit_block 循环里每次退出调
  `getenv("MICRO_FORGE_JIT_CHAINDBG")`（占 12.5%）**——改为函数级
  `static const bool` 缓存（env 在差分测试里靠 fork 前设置，进程内不变）。
- 与 stage 1（`/tmp/wt-s1` 交错对拍）持平：~73-83 Mips（多 dsh 噪声主导；
  同窗口内两版差异 <3%）。链收益暂小：目标限 jmp→jmp，真正的收益在
  stage 3 放开 jcc/call 目标后。
- 探针正确性锚：`kind=1 rip=0xffffffff80201061 icount=28135989
  console=1687 blocks=9910 failed=354 coverage=99.9625%`，console 与解释
  器逐字节一致；330 差分 + 九段验收全绿。

## 去留

- 保留：`MICRO_FORGE_JIT_NOCHAIN`（A/B 开关）、`JIT_CHAINDBG/2`（已缓存，
  CHAINDBG2 为逐次打印）、`JIT_LINKSTATS`。probe 的 MEMHASH/REGWIN 是
  一次性排障件，env 门控、默认零开销，暂留。
- stage 3 方向：jcc/ret/call 的受护链桩——届时它们也需要自记账出口，
  本批删掉的 19B 纯记账尾会以受护形式回来。
