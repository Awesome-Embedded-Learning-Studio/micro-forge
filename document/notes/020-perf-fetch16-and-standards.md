# 020 — perf 第二轮：fetch16 半字合并（大胜）+ 两个标准清理（perf-neutral）

> 2026-06-28。承接 notes 019（Phase 0 + clock/cycles 两优化）。本轮继续打实测排序后的下轮候选。

## 背景

notes 019 完成方法论 Phase 0 基建 + 头号两个热点（VirtualClock uint128、coordinator cycles 缓存），累计 gpio/uart/tim +22/+32/+26%。对抗验证 workflow 排出的下轮候选：fetch16 半字合并（#2，needs-care）、read_reg `value_or(0)`（#5）、eager-trace `name()` 虚调用。

## fetch16 两次 Byte → 一次 HalfWord（大胜）

**前置验证（needs-care 项）**：fetch fault 路径是否依赖 Width？
- `try_escalate_fault` 只按 `CPUError kind` 判升级，不吃 Width。
- `FaultRecord.access_width` 是**诊断元数据**，无控制流读它；唯一消费者是 `test_cortex_m3_faults.cpp:78` 断言 `Width::Word`（栈访问 fault，不是 fetch）。
- 指令 fetch 只命中 FlatMemory（flash/SRAM），`FlatMemory::read(addr, HalfWord)` 的字节移位循环与旧 `lo|(hi<<8)` 位等价。

结论：Byte→HalfWord 对所有已测/实际场景行为保持，仅 fetch fault 的诊断宽度元数据变化（无人消费）。

**实测**：计数器 bus_reads/instr gpio 2.88→**1.50**、uart/tim 2.00→**1.00**（fetch 现在正好 1 次读）。吞吐（vs 上一轮）gpio **+12.7%** / uart **+6.9%** / tim **+10.6%**。提交 `9664c48`。

## 两个标准清理：perf-neutral（重要方法论教训）

**read_reg `value_or(0)` → `unchecked()`**（`31c88ce`）：给 `Registers` 加 `unchecked()`（debug assert + release 直读数组），换掉 thumb16/thumb32 解码器 rr 助手。Debug 下 assert 激活——ctest 全绿即证明 rr() 索引恒合法。**公平 A/B（pinned-core，stash 对照）：within ~2%，不可从噪声分离** → perf-neutral。价值：移除 DIRECTIVES 禁的 `value_or(0)` + 加 Debug 安全网。

**Bus trace 参数 lazy 求值**（`33bdd96`）：trace_access 早返回，但参数（`value_or(0)` + 虚 `name()`）每读急切求值（9.49M name()/sec）。挪进 `if (trace_)`。**perf-neutral**。

**教训：调用次数 ≠ 代价。** gprof 里 `FlatMemory::name()` 9.49M 调用但 **~0% self-time** —— 单态虚调用被分支预测/去虚化，近乎免费。移除它无可测收益。**self-time 才是真信号，call-count 会误导**（这与 notes 019「VirtualClock 静态标低、实测 #1」互补：那次是 call-count 低但 self-time 高 = 贵；这次是 call-count 高但 self-time 低 = 便宜）。静态审计两个方向都错，实测是对账的唯一权威。

## 测量噪声的现实

本机（WSL2）bench 噪声大（uart min/max 可跨 15~24M）。三个大改动（uint128 软除、fetch16 减半总线读）远在噪声之上（+18~41%），清晰可信；小改动（去一个 bounds check、去一个单态虚调用）落入噪声，不可分辨。方法论 §4 防退化 guard 须配宽容差带；小改动的"提升"不可单独采信，要靠 self-time 而非 Δ 判断该不该做。

## 累计成果（vs 原始基线，RelWithDebInfo）

| 场景 | 基线 | 现在 | Δ |
|---|---|---|---|
| gpio_iotoggle | 14.27M | ~19M | **+33%** |
| uart_printf | 16.73M | ~23.5M | **+40%** |
| tim_timebase | 17.30M | ~23.8M | **+38%** |

ctest 321/321 双构建（Debug + RelWithDebInfo）全程全绿。

## 后续

剩余候选（workflow 评级 medium/low，且在本噪声地板下大概率 perf-neutral，性价比递减）：
- **weakptr-bus → raw observer**（#1，9 个解引用点 + API 改，blast radius 大；可能也 perf-neutral 若 WeakPtr 已被内联）。
- virtual-device FlatMemory 快路径、bitband 高位预检、find_region 缓存。

这些是否值得做，取决于是否接受"perf-neutral 但结构/标准清理"的价值，或需要更稳的测量环境（装 `libc6-dbg` 启用 callgrind、或物理机/pin CPU）来分辨小改动。
