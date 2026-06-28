# 026 — taste 批5:execute_32bit 族 dispatch 表驱动(§4 数据驱动 dispatch)

> 2026-06-28。CODING-TASTE §4 核心反例:execute_32bit 的掩码梯优先级只在注释里(SSAT 必须在 dataproc-imm 前、LDREX/TBB 必须在 STRD 前——改一级得通读整梯)。

## 改动

把 execute_32bit 两段连续的族 dispatch(SSAT→addsub→dataproc_imm→loadstore_single;dataproc_reg→shift_reg→misc_reverse→tbb_tbh→ldrex_strex→strd_ldrd→stm_ldm)从 if 链改成**有序表**:
- 11 个纯 hw1/hw2 matcher 函数(anonymous namespace),逐字抄原 if 条件(含 `||`/`!=`)。
- `T32Dispatch { bool (*match)(uint16_t,uint16_t); CPUExpected<void> (CortexM3CPU::*handler)(uint16_t,uint16_t); }`。
- 两张 `static const` 局部表(execute_32bit 内)——成员函数内可取 private handler 指针。
- dispatch = 首匹配遍历。

**顺序在表里显式**(数组顺序 = 匹配优先级):SSAT 在 dataproc_imm 前、LDREX/TBB 在 STRD 前,一目了然;每个 matcher 可独立单测。

**保留 if 的**:内联项(BL/B.W/MOVW/MOVT/DMB/MRS/MSR/BFI/SBFX)——匹配独立 hw1、顺序不敏感;UDIV/MLA/SMULL(原 307-381)——夹两表中间,内联保留,顺序靠"表1 → 内联块 → 表2"维持。

## 行为保持(铁律)

matcher 逐字抄原 if 条件;表顺序 = 原 if 链顺序;handler 全是已有 `t32_*`,body 零改。逐路径等价。

## 验证

- ctest **321/321 双构建全绿**(Debug + RelWithDebInfo)。
- bench `--baseline` PASS(0 regression);ratio 91-94% 是 WSL2 噪声。**函数指针 matcher 未显著退化 32bit dispatch**(gpio/uart/tim 固件在 tolerance 内)——matcher 极小,-O2 下表遍历开销可忽略。

## 价值与遗留

§4:两段族 dispatch 的优先级从"注释里的物理顺序"变成"表里的显式顺序",改一级只需看表。matcher 可独立单测(§5 可测试性的附带收益)。

**遗留**:内联项(BL/MOVW/UDIV/...)仍 if,§4 为部分治理(族 dispatch 表化、内联未);UDIV/MLA/SMULL 因夹两表间未表化。彻底表驱动需把内联也抽 handler(工程大、边际收益,留后续)。
