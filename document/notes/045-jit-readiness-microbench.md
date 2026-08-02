# 045 · JIT-readiness micro-bench + coordinator/NVIC 热路径优化

**日期**:2026-07-31
**动机**:为"要不要上 JIT/翻译块缓存"提供数据地基。之前对端到端成本
结构的判断(bus 是大头)是推断,没有实测拆解。建 Google Benchmark
micro-bench 把端到端成本逐层隔离,锁定 JIT 能/不能碰的部分,再针对性
优化。

## 基建:Google Benchmark 接入 + B1–B7

- `bench/bench_micro.cpp`(新)+ `bench/CMakeLists.txt` 用 `FetchContent`
  拉 `google/benchmark v1.9.0`(和 test/ 拉 GoogleTest 同模式),与现有
  `bench_sim.cpp`(手搓 chrono 的端到端 bench)并存。`MICRO_FORGE_BUILD_BENCH`
  门控,RelWithDebInfo 在 `build-rel/`。
- 七个 micro-bench,**增量叠加**精确分离成本:
  - **B1** `B.` 自循环:纯 dispatch 地板(fetch+decode+execute,零外设)
  - **B5** ALU 循环(ADDS+B):dataproc execute handler 边际
  - **B6** B.+NVIC/SCB:中断检查税(`has_pending_irq` 每步扫 8 word)
  - **B7** +coordinator:coordinator 协调税(WeakPtr deref + cycles + clock + tickable)
  - **B2/B4** Bus::read(1/4/8/16 region):find_region 扫描 + WeakPtr 税
  - **B3** SysTick::tick(1):P1 事件驱动改造的目标基线

## 锁定的端到端成本结构(B7 ≈ 端到端)

| 成本块 | 占端到端 | JIT 能砍? |
|--------|----------|-----------|
| 纯 dispatch(fetch+decode+execute) | ~44% | ✅ JIT 主场 |
| coordinator 协调(cpu_ WeakPtr deref + cycles + clock + tickable) | **~40%** | ❌ 在 cpu.step() 外层 |
| NVIC 中断检查(has_pending_irq,无 cache) | ~16% | ❌ 在 step 入口 |
| bus(find_region + WeakPtr + FlatMemory) | ~15-22% | 部分 |

**决定性结论**:JIT 碰不到的部分 = coordinator(40%)+ 中断检查(16%)
= **56%**。不先治这两块,JIT 在端到端只有边际效果。bus 不是大头(推翻
了最初判断)。

 dataproc execute handler 边际只有 ~2.5 ns(B5 锁定),也不是大头。

## F 优化(behavior-preserving,ctest 392 绿,含 QEMU oracle)

1. **coordinator `cpu_` WeakPtr → 裸 `CPU*`**([coordinator.hpp](../../include/sim/coordinator.hpp)):
   和 CPU→Bus、Region→Device 同类改造(CPU 与 coordinator 同寿命,无引用环)。
   5 处 `set_cpu` 调用方(soc + 2 test + bench)跟着改。**coordinator 税 14.2→2.4 ns(−83%)**。
2. **NVIC `has_pending_irq()` 复用 `highest_priority_pending_irq()` 的 cache**
   ([nvic.hpp](../../include/periph/nvic.hpp)):语义等价(0xFF ≡ 无 enabled+pending),
   nvic.cpp 的 write 已在每个 ISER/ICER/ISPR/ICPR/IPR 写后 `invalidate_cache()`。
   **中断检查税 5.5→1.6 ns(−71%)**。

## 结果

- **端到端 ips 翻倍**:gpio 21.5M→50.2M、uart/tim 25.5M→46M(taskset 标准条件)。
  micro-forge 跑到 ~50M ips,**逼近 STM32 72M 原生(差 1.4×)**。从最初的
  1.4M cycle/s 累计 ~35×。
- JIT 外的稀释从 2.27× 压到 1.33× → dispatch 占端到端从 44% 升到 **75%**,
  **JIT 的端到端收益空间大幅扩大**(之前担心的"被 bus 稀释"实为被
  coordinator+中断检查稀释,现已治)。
- `bench/baseline.txt` 更新到 50/37/39M(median×0.85,吸收 WSL 抖动)。

## 放弃项(数据证伪,记录省得再试)

- **(a) FlatMemory::read 逐字节循环 → memcpy**:实测倒退(B1 105M→87M)。
  原因:`width_bytes` 是运行时变量(Width 枚举),`memcpy(dst,src,n)` 无法
  内联成单 mov,退化成 runtime 调用。逐字节循环编译器已优化好。回退。
- **(c) find_region 二分**:端到端 perf_stats 显示 avg/call = **2.0**(flash
  region 在列表前部,fetch 命中第 1-2 个),二分对小表无收益。放弃。
- **(b) Region.device WeakPtr→裸指针 / (d) bitband 分流**:端到端 bus 只占
  15-22%,(b) 几十处接口变更换 ~4%、(d) ~5%,性价比低。**搁置**,等真要
  再榨 bus 那点余量时再做。

## 下一步

dispatch 现在占端到端 75%,JIT/翻译块缓存的主场打开。转 E:起草翻译块
缓存 propose(块粒度 / IR 形态 / trace invariant 怎么保 / 保真度怎么验),
核心翻译逻辑按 `complex-impl-self-written` 惯例——用户定谁写。
