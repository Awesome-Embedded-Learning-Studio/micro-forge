# 024 — taste 批3:t16_dataproc 再拆(special_bx + dataproc_reg)

> 2026-06-28。批2(notes 023)的收尾。批2 把 `execute_16bit` 拆成 13 族 handler,但 `t16_dataproc`(0b01000 臂)仍把两个不相关的族塞在一个 bit10 分支后:special-data/BX 与 data-processing-register。本批拆开,§1 单一职责。

## 改动

`t16_dataproc`(~140 行)→ 两个 handler:
- `t16_special_bx`(bit10=1):ADD/CMP/MOV high + BX/BLX
- `t16_dataproc_reg`(bit10=0):AND/EOR/shift-reg/ADC/SBC/ROR/TST/RSB/CMN/ORR/MUL/BIC/MVN

dispatcher 的 0b01000 臂改 ternary 分流:

```cpp
case 0b01000:
    return ((insn >> 10) & 1) ? t16_special_bx(insn) : t16_dataproc_reg(insn);
```

body 逐字搬;`t16_special_bx` 的 switch 原无 default(在 `t16_dataproc` 内靠 fall-through 到 dataproc_reg),独立 handler 加不可达 `return IllegalInstruction`(2-bit op 全覆盖 0-3)。

## 验证

- ctest **321/321 双构建全绿**(Debug + RelWithDebInfo)。
- bench `--baseline` PASS(0 regression);ratio 95-97% 是 WSL2 噪声(纯结构拆分、逐字搬,物理上不可能退化)。

## 价值

§1 单一职责:`t16_dataproc` 不再一个函数混"分支跳转族"与"数据处理寄存器族";每个 handler 一句话能说清。dataproc.cpp 的 handler 现在各自单一职责。

## 教训(重复,需记忆)

`Write`/`Edit` 路径**第二次**把 `cortex_m3`(下划线)写成 `cortex-m3`(连字符)——Edit 报 "File does not exist" 才发现。**铁律:本仓 cortex 目录是下划线 `cortex_m3`,所有工具调用路径用下划线。**
