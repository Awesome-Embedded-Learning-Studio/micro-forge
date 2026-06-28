# 027 — taste 批6:t32 dataproc imm+reg 合并(§2 DRY)+ inline 教训

> 2026-06-28。CODING-TASTE §2 反例:`t32_dataproc_imm` 与 `t32_dataproc_reg` 近克隆——switch op(0/1/2/3/4/8/10/11/13/14)+ 标志更新(ADD/ADC/SBC/SUB/RSB 特殊 + update_nz)+ Rd=15 检查**逐字相同**,只差第二操作数(Thumb2ExpandImm 的 imm32 vs 移位寄存器的 shifted)。

## 改动

抽 `t32_dataproc_apply(op, s_bit, rn, rd, rn_val, operand_b)` 统一 switch+标志+Rd 检查(~50 行)。`t32_dataproc_imm` 只解码 imm32 后 `return apply(op2, ..., imm32)`;`t32_dataproc_reg` 只算 shifted 后 `return apply(op, ..., shifted)`(shift 计算保留)。逻辑逐字来自两 form(对比确认 case 0-14 + 标志全等)。

## ⚠️ perf 教训:out-of-line 退化 ~10-13%,改 inline 恢复

第一版 apply 是 **out-of-line**(定义在 .cpp)。ctest 321/321 双绿(行为保持),但 bench **REGRESSION**:公平 A/B(git stash 回退批6 → bench 批5 → pop),同 session 对比——

| 场景 | 批5(stash) | 批6 out-of-line | 批6 inline |
|---|---|---|---|
| gpio | 92.8% | **83.4%** ❌ | 91.5% ✅ |
| uart | 92.6% | **79.9%** ❌ | 91.7% ✅ |
| tim | 90.0% | 88.2% | 94.3% |

out-of-line apply(gpio/uart 退化 10-13%,tim 几乎不退化——tim 用 dataproc 少)。根因:**out-of-line 函数调用无法内联**,dataproc 密集路径每条指令多一次调用 + switch 不能跨函数优化。改 **hpp 类内 inline 定义**(调用点可见→编译器折叠进 imm/reg,等价原内联 switch)后 perf 恢复。

**方法论教训**(补 PERF-METHODOLOGY):**重构抽公共函数要警惕 out-of-line 退化**——热路径上的 helper 若不 inline,函数调用本身是真开销(尤其中小 helper 被高频调用)。修复:类内 inline(LTO 也能,但项目没开)。A/B 是唯一可靠判据(ctest 绿 ≠ perf 不退化)。

## 验证(inline 版)

- ctest **321/321 双构建全绿**(Debug + RelWithDebInfo)。
- bench `--baseline` PASS(0 regression);ratio 91-94% 是 WSL2 噪声,inline 后与批5 持平。

## 价值

§2 DRY:消除 imm/reg ~50 行逐字重复,op 表/标志/Rd 逻辑单一归属(apply)。代价:apply 实现暴露在 hpp(private inline,C++ 常见性能模式)。
