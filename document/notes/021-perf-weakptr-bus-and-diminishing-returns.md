# 021 — perf 第三轮：weakptr-bus→裸指针（大胜）+ 收益递减拐点

> 2026-06-28。承接 notes 019/020。本轮做掉对抗验证排序里 blast-radius 最大的一个，并重新 profile 确认拐点。

## weakptr-bus → raw observer（实测大胜，+8-13%）

`CortexM3CPU::bus_` 原是 `WeakPtr<memory::Bus>`，改裸指针 `memory::Bus*`。**牵连比预想小**：8 个 `bus_->` 解引用点零改动（裸指针同样支持 `->` 和 `!` 判空），只改：成员类型 + 构造函数 + **4 个构造点**（生产 SoC 1 个 + 测试 3 个：common/interrupt_roundtrip/coordinator）+ 删未用的 `memory_bus()`。

注意 grep 陷阱：`test_coordinator.cpp` 用中间变量 `bus_ptr = bus_.GetWeak()` 再构造，`CortexM3CPU\(` 模式被模板 `>` 隔开没匹配，漏了第 4 个构造点（构建报错才发现）。教训：构造点搜索要查 `GetWeak` + 跨行，不能只查 `CortexM3CPU(`。

**生命周期**：Bus 由 Machine `unique_ptr` 持有，先于 CPU 构造、后于 CPU 析构，裸指针全程有效。对齐既有 `nvic_`/`scb_` 裸指针模式（方法论根因注：所有权是 clean `unique_ptr` 树，WeakPtr 破"循环引用"是误用）。

**为什么这个能测、#4/#5 不能**：WeakPtr 每次总线访问的 IsValid + 控制块解引用，优化器**无法消除**（控制块是独立内存，有别名）；不像单态虚调用（`name()`）会被去虚化。所以在最热路径（每次 fetch/load/store）上是真开销。A/B（pinned，min/max 带不重叠）：gpio **+13%**、uart **+8%**、tim **+8%**。提交 `aa97d6b`。

## 收益递减拐点（重新 profile）

4 个优化（VirtualClock/fetch16/cycles/weakptr-bus）后**重新跑 gprof**——旧排名已过时。当前分布：step() 16.7%（dispatcher 本身）> bitband_read 12.5% > check_and_handle_interrupt / VirtualClock::advance / execute_16bit 各 8.3% > 其余（find_region/FlatMemory::read/fetch16/cycles/consume_ticks/tick）都在 **4% 带**。

VirtualClock::advance 从 33% 降到 8.3%（uint128 修法实证生效），cycles() 从 5.6% 降到 4.17%（缓存生效）。**热点已发散**：top 函数才 16.7%，没有单一支配点。

**拐点判断**：剩余最大项是 step() 解码派发（要提速 = 解码器结构重构，属 taste 战役的 thumb16 拆/opcode 表/step 拆，非外科 perf 修改）和 bitband_read（bit-band 翻译数学 inherent）。外科式热路径修改的干净大胜已经吃光；再要增益要么做大重构，要么落入噪声地板（本机 uart min/max 跨几个 M）。

## 累计成果（vs 原始基线，RelWithDebInfo）

| 场景 | 基线 | 现在 | Δ |
|---|---|---|---|
| gpio_iotoggle | 14.27M | ~22M | **+54%** |
| uart_printf | 16.73M | ~26.2M | **+57%** |
| tim_timebase | 17.30M | ~26.2M | **+51%** |

ctest 321/321 双构建全程全绿。三场 perf 轮（notes 019/020/021）共 6 个改动：4 个大胜（VirtualClock uint128、cycles 缓存、fetch16 HalfWord、weakptr-bus 裸指针）+ 2 个标准清理（read_reg unchecked、Bus trace lazy，perf-neutral）。

## 后续方向

- **结构重构（taste 战役）**：step()/execute_16bit 现占 ~25% 合计，提速需解码器拆分 / opcode 表 dispatch / accessor 统一——这是已立项的 taste 维度工作，需 oracle 护栏，独立于 perf 外科修改。
- **测量环境**：分辨 <5% 的小改动需更稳环境（装 `libc6-dbg` 启 callgrind、物理机 pin CPU、或多次中位数 + 容差带 guard）。
- **防退化 guard**：bench 已是 advisory；可按方法论 §4 升格为软门（中位数 + 容差带）锁住这 +54-57%。
