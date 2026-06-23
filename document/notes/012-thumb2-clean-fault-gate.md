# 012 — Thumb-2 §4 作用域外 clean-fault 门禁(T3)

> Thumb-2 全覆盖里程碑 · T3。把「ARMv7E-M DSP 指令在 Cortex-M3 上应 UndefinedInstruction」从 ad-hoc verify 固化成 ctest 回归。过程中发现并修复 `t32_misc_reverse` 的静默误中 bug。ctest **272/272 绿**(271 + 1 新测试)。

## 背景

matrix §4 列出 M3 作用域外指令(ARMv7E-M DSP 扩展,M4/M7 才有):QADD/QSUB/QDADD/QDSUB、PKHBT/PKHTB、SEL、SXTAH/UXTAH/SXTB16/UXTB16、UMAAL、SMLAD/SMLSD/SMLALD、USAD8/USADA8。audit 阶段曾 verify 它们 clean-fault,但未固化成回归测试 —— dispatch 表后续任何改动都可能让它们悄悄误中某个 handler。本批补回归 + 顺手修发现的真 bug。

## 编码数据源(不靠记忆,以 objdump 为权威)

`arm-none-eabi-as -mcpu=cortex-m3` 拒编这些指令(证实 M3 无 DSP);改用 `-mcpu=cortex-m4` 汇编 + `objdump -d` 提取 18 个权威 hw1/hw2 编码。注意 M3 的 SXTH/SXTB/UXTH/UXTB 是 **16-bit `0xB2xx`**,而 `0xFA0x` 空间的 SXTAH/SXTB16 是 DSP —— 助记符相近但编码空间不同。

## 发现:misc_reverse 静默误中(9 条)

dispatch mask 21 `t32_misc_reverse` 条件 `(hw1&0xFF00)==0xFA00 && (hw2&0x00F0)!=0` 只用 op2(hw2[7:4])区分 REV/REV16/RBIT/REVSH/CLZ,**完全没校验 op1(hw1[7:4])**。objdump 权威:合法 reverse/CLZ 族 op1 只能是 `9`(rev/rev16/revsh/rbit)或 `B`(clz):

| 指令 | 编码 | op1 | op2 |
|------|------|-----|-----|
| rev.w | fa91 f081 | 9 | 8 |
| rev16.w | fa91 f091 | 9 | 9 |
| rbit | fa91 f0a1 | 9 | A |
| revsh.w | fa91 f0b1 | 9 | B |
| clz | fab1 f081 | B | 8 |

但 `case 0x8 REV.W` 接受任意 op1,导致 op1∈{0,1,2,3,8,A} 的 DSP 编码被静默当 reverse 族执行:

- SXTAH(`0xFA0x`)、UXTAH(`0xFA1x`)、SXTB16(`0xFA2x`)、UXTB16(`0xFA3x`)→ REV.W
- QADD(`0xFA8x`)→ REV.W;QSUB(op2=A)→ RBIT;QDADD(op2=9)→ REV16;QDSUB(op2=B)→ REVSH
- SEL(`0xFAAx`)→ REV.W

## 修法

`t32_misc_reverse` 入口(CLZ 分支前)加 op1 门禁:CLZ 特判 `op1==B & op2==8`;否则要求 `op1==9`,否则 `IllegalInstruction`。一处改动,不动 dispatch 顺序(仍 load-bearing)。选 handler 内门禁而非收紧 mask —— 合法 reverse 族 op1 分散(9 与 B),mask 难精确表达,门禁更稳。

## 本就 clean-fault 的 9 条(无需改代码)

- PKHBT/PKHTB(`0xEAC1`)虽命中 dataproc_reg(mask 19),但 op=6 → handler 内 default fault。
- UMAAL/SMLAD/SMLSD/SMLALD/SMLALDX/USAD8/USADA8 编码不撞任何前置 mask → 末尾兜底 IllegalInstruction。

## 验证

- `test_cortex_m3_faults.cpp` 新增 `V7emDspInstructionsCleanFault`:18 编码,每条 `load_program({hw1,hw2})` 后 assert `step()` 返回 `IllegalInstruction` 且 `State::Faulted`。
- `ctest` 全量 **272/272 绿**(reverse 族、固件 E2E 3 + gcc hal_uart、CLI、中断抢占均无回归)。

## 陷阱

- **op1 vs op2**:`0xFA00` 空间指令同时由 hw1[7:4](op1)和 hw2[7:4](op2)区分;合法 reverse 族 op1∈{9,B},只查 op2 会吞 DSP 编码。
- **dispatch 顺序仍 load-bearing**:misc_reverse(op2≠0)排在 shift_reg(op2==0)之后,两者共占 `0xFA00`;门禁加在 handler 内,避免动 mask 顺序。
- **SXTH vs SXTAH**:M3 extend 是 16-bit `0xB2xx`,`.W` 形式 `0xFA0x` 是 DSP —— 不要因助记符相近就假定同编码。

## 成果

§4 作用域门禁锁定:18 个 ARMv7E-M DSP 编码在 Cortex-M3 上全部 clean-fault,9 个静默误中 bug 已修 + 配回归。dispatch 表对 DSP 编码的误吞风险从此有回归守门。Thumb-2 全覆盖里程碑剩 §5 测试缺口 sweep(T4)。
