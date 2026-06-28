# 019 — perf 战役 Phase 0：测量基建 + 两个 clock/coordinator 热点优化

> 2026-06-28。perf 维度第一场实战（PERF-METHODOLOGY.md §3 Phase 0 + §1 闭环）。
> 标尺已立（PERF-METHODOLOGY / CODING-TASTE / COVERAGE 三方法论），本篇是 perf 维度的首轮落地。

## 背景

perf 战役的方法论（PERF-METHODOLOGY.md）此前已立，并附**静态** perf-abstraction-audit（附录 A，9 个靶子带量级估计）。但方法论 §0 铁律是「测量驱动，不靠直觉」——静态估计未经实测。Phase 0（§3）明列基建前置：构建类型 / bench 目标 / `MF_PERF_STATS` 计数器 / 场景库。**这些此前都不存在**（实测 `build/CMakeCache.txt` = Debug/-g/-O0，无 bench target，无计数器）。

## 目标

1. 立 Phase 0 测量基建（独立 build-rel，不污染 Debug test 构建）。
2. 跑真 profile，用实测数据**验证/重排**附录 A 的静态靶子。
3. 按方法论 §1 四步闭环优化头号靶子，**行为保持**（cycle-accuracy 不许串 wall-clock）。

## 设计

### Phase 0 基建
- **build-rel**（RelWithDebInfo）：bench 只在优化构建下取，**绝不 bench Debug**（§3 第一要纠正的点）。与 `build/`（Debug test）隔离。
- **`MF_PERF_STATS` 计数器**（`include/util/perf_stats.hpp`）：编译期门控。关时宏展开为 `((void)0)`/`((void)(x))`，零开销且不触发 `-Wunused-but-set-variable`（关键：off 分支必须 void-cast 掉 find_region 的迭代计数）。开时记 bus_reads/writes、fetches、find_region 扫描长度、16/32-bit 指令混合。
- **bench 目标**（`bench/bench_sim.cpp`）：载 armcc/AC6 语料，warmup 到稳态后计时固定步数，报 insn/sec 中位数。场景组合（§2C）：gpio_iotoggle（紧循环）/ uart_printf（IO 密集）/ tim_timebase（定时器驱动）。`MICRO_FORGE_BUILD_BENCH` 选项门控，默认 OFF 不污染常规构建。

### Profile 工具的现实
- `perf` 未装；**callgrind/memcheck 不可用**（本机 `ld-linux-x86-64.so.2` 被 strip，valgrind 无法建立必需的函数重定向，需 `libc6-dbg`，不擅自动）。
- 实际用 **gprof**（-O2 -pg，编译期插桩无运行时符号依赖）+ **MF_PERF_STATS 计数器**。计数器给「每指令做了多少工作」，gprof 给「时间花在哪个函数」。两者互补，足够驱动决策。

### 优化（4 步闭环 ×2）

详见下「决策」。两个优化都在 sim 时钟/协调层，纯算术、trivially behavior-preserving。

## 决策

**实测重排附录 A**（静态估计被实测纠正的最显著一例）：

| 靶子 | 静态估计 | 实测 | 处理 |
|---|---|---|---|
| VirtualClock `__uint128` | **#9 标「低」**「≤1x/指令」 | **gprof #1，33% runtime** | ✅ 本轮修复 |
| coordinator `cycles()` ×2/步 | #6 中 | 5.6%，6.6M 调用（确为 2×/步） | ✅ 本轮修复 |
| fetch16 2×Byte | #2 高 | 计数器坐实：2.0 bus_reads/instr（全 16 位固件）= 正好两次 fetch 字节 | 下轮候选 |
| write_reg 双查 | #7 中 | gprof <0.1%，**不热**，优化器会吃掉 | ❌ reject |
| `FlatMemory::name()` 每读虚调用 | 附录漏列 | 9.49M 调用（trace 关仍急切求值） | 下轮候选 |

对抗验证由 perf-review workflow fan-out（10 靶子各一 skeptic，读真实代码 + 实测数据）。结论：**先做 VirtualClock（#1，trivially-preserving，最高 gain/risk）+ cycles 缓存**，合计 ~38% runtime。

**VirtualClock::advance `__uint128_t`→`uint64_t`**：x86-64 无 128 位硬件除法，`__uint128` 除法走 `__udivti3` 软除（~50 周期）×每步 2 次 ×多时钟域 = 登顶。但 `advance()` 唯一调用方是 `coordinator.cpp:40`，传入 delta = 单步 cycle 数（1-3）；`delta × 72MHz < 2³²`，`delta×1e9+residual < 2³³`，**uint64 绰绰有余**，结果与 `__uint128` 位等价。加 `INVARIANT` 注释锁定该前提。

**coordinator `cycles()` 缓存**：`step()` 此前每步调用虚 `cycles()` **两次**（step 前后取 delta），仅读一个 uint64 成员。`last_cycles_` 成员早已声明却未用——用它缓存上一步的 curr，砍掉一次虚调用（2×→1×/步）。

## 陷阱

1. **「tim_timebase -O2 fault」虚惊一场**：build-rel bench 报 tim_timebase DataAccessFault，疑似 -O0/-O2 行为发散（行为保持铁律违反）。深查发现：Debug bench **也打同一条 fault**——是 tim_timebase 正常 boot 的**可恢复 fault**（NVIC 优先级配置时一个越界 IRQ，`try_escalate_fault` 恢复后继续）。固件测试只断言「最终非 Faulted」，故全绿。教训：对比 build-type 行为时，**测试（只查终态）≠ bench（打详细 fault 日志）**，要苹果比苹果。

2. **bench subset 过滤器参数写反**：`subset.find(s.stem)` 是在短 subset 里找长 stem，恒失败 → 全部场景被 skip → callgrind/gprof「空跑」（只打 warmup 行就 exit 0，总 Ir 恒为启动开销）。应为 `stem.find(subset)`。此 bug 让我一度误判 callgrind/gprof 在本机完全不可用。

3. **`MF_PERF_STATS` off 分支的 `-Wunused-but-set-variable`**：find_region 的 `iters` 计数在宏关闭时被赋值却「未用」→ -Werror 炸生产构建。off 宏必须 `((void)(iters))` 引用它才算已用。

4. **工作区有大量预存 WIP**：会话开始时 ~37 文件未提交（用户「忘记提了」）。提交前必须分清我的 perf 改动 vs 既有 WIP，分两次提交（WIP 包 + perf 包），避免织在一起。

## 验证

- **bench Δ（RelWithDebInfo，中位数 insn/sec）**：
  - gpio_iotoggle：14.27M → 17.42M（**+22%**）
  - uart_printf：16.73M → 22.12M（**+32%**）
  - tim_timebase：17.30M → 21.79M（**+26%**）
- **ctest 321/321 全绿**（Debug `build/` + RelWithDebInfo `build-rel/` 双构建类型）——行为保持。
- **`__udivti3`/`__umodti3` 已从 bench 二进制消失**（`nm` 验证）——软除确实消除。

提交：`99c6b6b`（perf Phase 0 + 两优化）；WIP 包 `4bb7a96`。

## 后续

- **下轮候选**（对抗验证已确认，按 gain/risk 排）：fetch16 2×Byte→HalfWord（#2，needs-care：fault 宽度 Byte→HalfWord，需审 `record_bus_fault` 消费方）、read_reg `value_or(0)`（#5，顺带清 DIRECTIVES 禁的 banned-pattern）、eager-trace-args（`name()` 9.49M 虚调用，顺带清 bus.cpp:69 的 `value_or(0)`）、weakptr-bus→raw observer（#1，9 个解引用点，blast radius 大放后）。
- **防退化 guard 政策**（§4）：bench 已是 advisory 基建；阈值带 + 中位数 + 锚定 insn/sec，软门先行，pin 死工具链后再考虑硬门。
- **profile 工具债**：本机缺 `libc6-dbg` 致 callgrind/memcheck 不可用；装上后可补函数级调用图（gprof 在 -O2 内联下粒度有限）。
