# 014 — Timer UIF → NVIC → handler 端到端(C2a/b)

> 06 第二波 C2(外设中断端到端)。打通外设→NVIC 的 IRQ 注入通道(原 `raise_irq` 空壳),Timer UIF 经 edge 回调 + UIE 使能触发,coordinator 驱动 tick → handler 往返 E2E 验证。ctest **289/289 绿**(286 + 3 新)。下一步 C2c 抢占/嵌套验证。

## 背景

06 第二波:外设中断路径紧跟第一波 A(抢占)做端到端验证 ——「中断在真实外设上跑通是抢占正确性的最佳验证手段」。Timer 已半成(UIF 产生、PSC/ARR/CNT 与 VirtualClock 联动),缺 UIF→NVIC→handler 端到端。

## 核心发现:raise_irq 是空壳

`CortexM3CPU::raise_irq` 原为 no-op(`return {};`)—— **整个外设→NVIC 通道从未接通**。SysTick 靠独立的 `sys_tick_irq()`(直设 `pending_sys_tick_`,系统异常 15)绕过它。所以 C2 不只是「Timer 没调 raise」,而是**第一个打通这条公共通道**的里程碑(USART/EXTI 以后都走它)。

## 实现

1. **`raise_irq`**([cortex_m3.cpp](src/arch/arm/cortex_m3/cortex_m3.cpp)):`if (nvic_) nvic_->set_pending(irq);`。一行接通 NVIC ISPR。
2. **Timer UIE edge 回调**([stm32f1_timer.hpp](include/chips/stm32f1/stm32f1_timer.hpp) / [.cpp](src/chips/stm32f1/stm32f1_timer.cpp)):仿 SysTick `set_irq_callback(std::function<void()>)`。`tick()` 在 UIF **0→1 edge 且 `DIER.UIE`(bit0)使能**时调回调一次。
3. **`kTim2Irqn=28`**([interrupt_config.hpp](include/chips/stm32f1/interrupt_config.hpp)):`inline constexpr intr::intr_n_t kTim2Irqn = 28`(vector index 16+28=44)。
4. **SoC 接线**([stm32f103_soc.cpp](src/chips/stm32f1/stm32f103_soc.cpp)):`tim2.set_irq_callback([cm3_weak]{ (void)cm3_weak->raise_irq(kTim2Irqn); })`,仿 SysTick 接线(WeakPtr + IsValid 守卫)。

## 验证(3 新单测)

- **单元**([test_stm32f1_periph.cpp](test/test_stm32f1_periph.cpp)):
  - `UifEdgeWithUieTriggersIrqCallback`:edge+UIE → 回调一次;UIF 未清时再溢出不重 fire(edge);清 UIF 后重新 fire。
  - `UifWithoutUieDoesNotFireCallback`:UIF 置位但 UIE 未使能 → 不回调(只状态,无 IRQ)。
- **E2E**([test_interrupt_roundtrip.cpp](test/test_interrupt_roundtrip.cpp) `TimerUifRoundtrip`):fixture 加 TIM2(0x40000000)+ NVIC enable bit28 + prio 0xE0 + vector[44];配 PSC=0/ARR=5/UIE/CEN;coordinator(Apb1)驱动 tick → UIF → raise_irq(28) → handler 进入 → BX LR 返回。断言 entered + returned。
- `ctest` 全量 **289/289 绿**,固件 E2E/CLI/中断抢占无回归。

## 陷阱

- **`raise_irq` nodiscard**:返回 `expected<void>`,丢弃触发 `-Wunused-value`(-Werror)。调用点(SoC 回调、test)须 `(void)` 包裹。SysTick 的 `sys_tick_irq` 返回 void 无此问题 —— 外设 IRQ 通道首次遇到。
- **edge vs level**:选 edge(UIF 0→1 raise 一次)。NVIC `set_pending` 幂等,但 edge 语义干净(避免每 tick 重复调回调);固件不清 UIF 导致持续 pending 是固件行为非 bug。
- **Apb1 时钟域**:Timer 是 tickable on Apb1,coordinator 每 step 按 Apb1 频率给 cycle。ARR=5/PSC=0 在数 step 内触发(SysTick Sysclk 类比)。
- **IPR 偏移**:TIM2=IRQ28,优先级寄存器 `0xE000E400+28`(=0xE000E41C,word 对齐,byte0=IRQ28 prio);ISER0 bit28 enable;vector index 16+28=44。

## 成果 / 下一步

`raise_irq` 公共通道打通 + Timer UIF 端到端往返验证。**C2c**(下批):抢占/嵌套场景(高优先级 Timer IRQ 抢占 Thread / 嵌套),检验第一波 A 的 `active_priorities_` 栈 —— 这是 C2 的核心价值。之后 C1 EXTI / C3 USART RX-IRQ 复用同通道。
