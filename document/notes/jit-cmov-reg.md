# JIT cmovcc reg,reg 落地收工笔记（2026-11-11，`7b08b1e`）

## 结果

- 老内核 b58e271 覆盖 77.40% → **77.4134%**；新内核 a234e96 → **77.3847%**
- 双内核 console 输出与解释器逐字节一致；452 项测试、九段验收门禁全绿
- 新增 16 个 cmovcc 差分用例（o/ne/b/ae/l/ge/g/le × w64/w32）

## 模板（kCmovRegHelper，49 字节）

```
57                      push rdi                  ; 保住 regs 基址
BE <dst32>              mov esi,slot              ; 第2参
48 8B 94 27 <src32>     mov rdx,[rdi+src]         ; 读源槽（rdi 仍是 regs 基址）
B9 <cc32>               mov ecx,cc                ; 第4参
41 B8 <width32>         mov r8d,width             ; 第5参（REX 前缀，共 6 字节！）
48 BF <cpu64>           movabs rdi,cpu
49 BB <helper64>        movabs r11,helper
41 FF D3                call r11
5F                      pop rdi
```

helper：`Cpu::jit_cmov(slot, src_val, cc, width)`——条件假直接 return（保留原值），
条件真 `merge_width(dst, src & mask, width)`。32 位写零扩展语义由 merge_width 承担。

## 本轮踩坑（按耗时排序）

1. **helper 参数序接反**（最大坑）：SysV 第 4 参=rcx、第 5 参=r8。最初模板把 cc 放
   r8d、width 放 ecx，于是 helper 收到 cc=width、width=cc 码。症状极具迷惑性：rcx 变成
   `0xFEDCBADD76772211` 这种"按错误宽度合并"的杂交值，单看像内存污染。定位方法：
   在 harness execute_case 里临时打印 gpr 上下文 + exit kind，发现解释器子进程其实是
   过的、坏的是 JIT 侧；再用 /tmp/t_mini.cpp（fork-free、开 MICRO_FORGE_JIT=1）复现，
   用 t_dump 模拟 patch 后 dump 字节确认编码无误，最后唯一变量只剩参数寄存器。
2. **41 B8 是 6 字节**：mov r8d,imm32 带 REX，别按 5 字节排洞。
3. **上轮 tidy 并非真绿**：jit_diff_cases.cpp 头有孤儿 NOLINTBEGIN（无 END），66 条
   告警一直被掩盖。本轮清零（designated 初始化、尾逗号、signed 位运算、嵌套条件），
   并因 600 行预算拆出 jit_diff_cases_alu.cpp。教训：**验收日志要逐段看 verdict 原文**。
4. cpu.hpp 超 600：四个宽度工具（sign_bit/width_mask/shift_count_mask/sign_extend）从
   Cpu static 成员移到 internal.hpp 的 `width_util` 命名空间（调用方全在 x86_64 目录）。
5. clang-tidy --fix 对 readability-avoid-nested-conditional-operator 无 fix；NOLINT 注释
   会被 clang-format 折到内层条件行上而失效——嵌套条件直接改写 if/else 或 IIFE 最省事。

## 下一步候选（按残留占比）

- bt/setb ~3.1%：位测试 + 条件写，helper 模式可直接抄 cmov
- movups ~2.8%：SSE 128 位搬移
- push/pop ~6.2%：helper 里走 rsp_ 读写内存
- call ~3.9% / ret ~3.8%：需块内控制流，接近 jcc-in-block 的前置工程
- jcc-in-block ~61%：最大项，块退出设计，属"疑难"档（可考虑 codex 协作）
