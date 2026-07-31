# 046 · WFI fast-forward(业界标准 hypersimulation)+ 端到端实证

**日期**:2026-08-01
**动机**:JIT 之前,先按业界标准(QEMU `sleep=no` / Simics hypersimulation)
做 WFI fast-forward(memory `emu_busy_wait_research` P2.a)。调研结论:
**业界只快进 WFI(真睡),不识别 busy-wait**(HAL_Delay 轮询)。

## 业界标准路(决定性)

跑了一遍 deep-research(各家):QEMU icount、Simics hypersimulation、
Renode、gem5、unicorn——**没有一个识别 busy-wait 模式**。标准 fast-forward
= WFI/sleep 段瞬移到下个 IRQ + 虚拟钟(icount)脱墙钟。HAL_Delay(busy-wait)
在它们里都逐指令跑。

micro-forge 最初想 beyond 业界(识别 HAL_Delay,候选 ① 函数级 stub /
② loop 点 / ③ 推进到退出),实测全部碰壁:
- **② fall-through 死循环**:fast-forward 触发 SysTick handler,handler 中断
  HAL_Delay 函数体,返回入口,体里的"检查 uwTick 是否到 delay"永远不执行 →
  HAL_Delay 永不返回。矛盾:fast-forward 要 handler 跑(uwTick++),但 handler
  拽走 cpu 让 HAL_Delay 体不跑。
- **③ PC 局部性启发式误判**:把 `HAL_RCC_OscConfig`(启动时小循环)也当
  busy-wait 快进 → 跳过 clock setup → 跑飞。

结论:**跟随业界,只做 WFI**(c/d 放弃,beyond 业界不可行)。

## P2.a 实现(ARMv7-M §B5.2.2 语义)

- **cpu WFI**([cortex_m3_thumb16_loadstore.cpp](../../src/arch/arm/cortex_m3/cortex_m3_thumb16_loadstore.cpp)):
  `WFI`(0xBF30)设 `sleeping_`(Cortex-M3 确定睡,非 hint)。`step()` sleeping
  且无 exception → 挂起 fetch(烧 cycle 不取指)。`step_take_interrupt` 进
  handler 时清 `sleeping_`(唤醒)。
- **coordinator fast-forward**([coordinator.cpp](../../src/sim/coordinator.cpp)):
  `is_cpu_sleeping_()`(= cpu->is_sleeping)→ `skip = min(tickable.cycles_until_next_event)`
  → `advance_cycles(skip)` + `clock.advance` + tickable.tick(触发 timer pending)
  → fall-through 让 cpu 进 handler 醒。lockstep 靠 `advance_cycles` 保住。
- **基础设施**(通用,WFI 用):
  - `CPU::advance_cycles(n)` / `is_sleeping()`(virtual,Cortex-M3 override)
  - `Device::cycles_until_next_event()`(SysTick override → next COUNTFLAG reload)
  - `SysTick::cycles_until_next_tick()`(P1 事件化查询)
  - `Stm32f103Soc::set_fast_forward_enabled`(转发 coordinator)

**关键**:WFI 后 cpu **真睡(不取指)**,fast-forward 直接推进时间到 IRQ——
没有 HAL_Delay 那种"handler 中断函数体"的矛盾。业界机制干净。

## 端到端实证(wfi_blink 固件)

[wfi_blink](../../examples/wfi_blink/firmware/main.c):PC13 LED + `__WFI()` 等
SysTick 500 tick(不是 HAL_Delay busy-wait)。runner(`MF_FAST_FORWARD` env):

| 模式 | 8M step PC13 toggle |
|------|---------------------|
| 关 fast-forward(WFI 逐 cycle) | **3** |
| 开 fast-forward(WFI 快进) | **939**(~313×) |

对比 **tamcpp_led(HAL_Delay busy-wait)**:同样 `MF_FAST_FORWARD=1` →
fast-forward **不触发**(cpu 没 WFI)→ LED 仍慢。**实证业界标准边界**:
WFI 固件实时,busy-wait 逐指令(QEMU/Simics 同)。

GUI:`MICRO_FORGE_FAST_FORWARD=1 ./micro-forge-gui wfi_blink.elf` → PC13 LED
实时闪(Session rebuild 读 env,转发 SoC)。

## 测试

- `WfiSetsSleepingAndSuspendsFetch`(cpu:WFI 设 sleeping + 挂起 fetch)
- `FastForwardSkipsWfiSleepToTimerEvent`(coordinator:sleeping + armed timer →
  跳 ~1000 cycle,虚拟时间推进)
- ctest 374 绿(含 2 新 WFI test)

## 残余 / 下一步

- P0 虚拟钟(icount)基本已有(VirtualClock cycle 派生);GUI pacing(墙钟
  同步)未做——fast-forward 开时虚拟时间可能快于墙钟刷新,LED 偏快闪。
- JIT(E):dispatch 现占端到端 75%(F 已治 coordinator+NVIC 56%),JIT 主场
  打开。WFI fast-forward 不与 JIT 冲突(正交:WFI 砍 sleep,JIT 砍 dispatch)。
- GUI 永久 fast-forward 勾选(替代 env)、beyond 业界 HAL_Delay 处理仍 open
  (但实证难,低优先)。
