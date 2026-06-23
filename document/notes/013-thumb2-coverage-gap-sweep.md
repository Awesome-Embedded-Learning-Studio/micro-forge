# 013 — Thumb-2 §5 测试覆盖缺口 sweep(T4)

> Thumb-2 全覆盖里程碑 · T4(收尾)。为已实现但缺针对性单测的指令补回归,锁定正确性。纯补测、零 src 改动 —— 14 新单测全部一次通过,无 bug 发现(与 §2/§3 已修 + §4 已门禁一致)。ctest **286/286 绿**(272 + 14)。

## 背景

matrix §5 列出已实现指令的测试缺口。前置 §2 静默错(T1c)、§3 缺失(T2)、§4 作用域门禁(T3)已清,本批把覆盖「钉死」,防后续 dispatch 改动悄悄退化为旧 bug。

## 补测范围(14 新单测)

### LDRD / STRD 全寻址模式(`test_cortex_m3_loadstore.cpp`,新文件)
P/U/W/L 矩阵全覆盖,断言**具体值 + writeback**(非 roundtrip —— roundtrip 用同地址算会掩盖 P/U/W 或 rt/rt2 字段错,正是 T1c #9/#10 那类 bug 的温床):
- LDRD:imm offset / pre-index(+WB)/ post-index(+WB)/ 负偏移 pre-index(U=0)。
- STRD:imm offset / post-index(+WB)。

### Load/Store single .W imm8 寻址模式(op=B/9/F/D)
hw2[11:8] op 模式:post+(B)、post-(9)、pre+(F)、pre-(D),全断言 load/store 值 + writeback,覆盖 `t32_loadstore_single` 的 imm8 分支。

### Data-proc shifted-reg flag sweep(`test_cortex_m3_advanced.cpp`)
覆盖 T1a 修过的 shifter-carry→C 之后的**算术 flag 更新**路径(ADD 进位/溢出、SUB 借位、ROR operand 计算),flag 经 `MRS R0,APSR` 读(N=31/Z=30/C=29/V=28)。lsl/lsr/ror operand 各一。

## 验证

- `ctest` 全量 **286/286 绿**,固件 E2E(3 AC6 + gcc hal_uart)/ CLI / 中断抢占无回归。
- 全部编码 `arm-none-eabi-as` + `objdump -d` 权威确认字段位。

## 设计点

- **断言具体值非 roundtrip**:LDRD/STRD/load-store 全部断言独立计算的期望地址 + 值;写回寄存器单独断言。若 handler 算错地址/字段,测试直接红而非自洽通过。
- **flag 经 MRS 读**:fixture 不暴露 xpsr_,沿用 basic.cpp 的 `MRS R0,APSR`(0xF3EF 0x8000)读 flags 到 r0 再查位。

## 成果

Thumb-2 全覆盖里程碑核心完成:T0 拆分 → T1 静默错 11/11 → T2 M3 缺失指令 → T3 作用域 clean-fault 门禁(含修 misc_reverse 误中)→ T4 测试缺口 sweep。模拟器从「够跑自写裸机」提升到「ARMv7-M base 指令集覆盖 + clean-fault 边界锁定 + 回归守门」。余 T5a/b(CMSIS-DSP / CubeF1 活体语料,可选验收)非阻塞。
