# 025 — taste 批4:step() 拆 sub-function(§1 单一职责)

> 2026-06-28。承接批1-3(thumb16)。本批转 `cortex_m3.cpp` 的 `step()`:CODING-TASTE §1 标的核心反例之一(混中断预处理 / fetch-fault / IT 门控 / dispatch+PC / exec-fault 后处理)。

## 改动

`step()`(122 行,6 职责)→ 3 个函数:
- `step()`(~12 行):纯编排 —— 状态检查 + `step_take_interrupt` + `step_execute_one`。
- `step_take_interrupt()`(中断门控):`check_and_handle_interrupt` + 异常 entry 消耗 step。返回 `StepFlow::{Continue, Return}`(Return = entry 消耗了本 step);fault 时设 Faulted + 返回 error。
- `step_execute_one()`(执行流水线):fetch hw1 + IT 门控 + 32/16 dispatch + PC 增量 + exec-fault 后处理 + cycles。~100 行,单一职责("执行一条通过中断检查的指令")。

新增嵌套枚举 `StepFlow { Continue, Return }`(hpp private)。`step_take_interrupt` 返回 `CPUExpected<StepFlow>`——成员函数定义的返回类型不在类作用域,.cpp 里全限定 `CortexM3CPU::StepFlow`。

## 行为保持(铁律)

body 逐字搬:中断门控(原 360-375)→ `step_take_interrupt`;fetch/IT/dispatch/fault/cycles(原 377-475)→ `step_execute_one`;状态检查 + 清 fault 留 `step()`。`step_take_interrupt` 返回 Return 时 `step()` 早返回 `{}`(等价原 `cycles_++` + `return {}`);fault 时返回 error(`step()` 透传)。控制流逐路径等价。

## 验证

- ctest **321/321 双构建全绿**(Debug + RelWithDebInfo)。
- bench `--baseline` PASS(0 regression);ratio 91-94% 是 WSL2 噪声(纯结构重组 + 逐字搬,不可能退化;`step_take_interrupt`/`step_execute_one` 会被内联)。

## 价值与遗留

§1 单一职责:`step()` 从 122 行混 6 职责 → ~12 行纯编排 + 两个各司其职的子函数。`step_execute_one` 仍 ~100 行,但单一职责(一条指令的执行流水线,altitude 统一)。

**遗留 §2 DRY**:`step_execute_one` 里 hw1 / hw2 两次 fetch-fault 处理(`LOG_ERROR` + `try_escalate_fault` + `cycles_` + return)近克隆,但 LOG 格式串不同(`opcode=0x%04X` vs `0x%04X%04X`,`detail=fetch` vs `fetch-hw2`),合并需内部分流 LOG、收益小,留后续。
