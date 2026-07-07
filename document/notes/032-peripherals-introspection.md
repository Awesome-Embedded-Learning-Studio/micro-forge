# 032 — peripherals 摘要扩展(GPIO/SysTick/NVIC 进 introspection,G2)

> 日期: 2026-06-30
> 阶段: v0.7.0 GUI dashboard 前置([milestone 04](../milestones/04-gui-debug-dashboard.md))
> 状态: ✅ ctest 327→328 双构建绿 + bench advisory PASS
> 前置: G1(notes 031)

## 背景

G1 建了 introspection 骨架,但 `PeripheralsSnapshot` 只有 usart_output。04 GUI 外设面板要 GPIO pin 状态 / SysTick / NVIC 摘要。实测:`Stm32f1Gpio::odr_` 私有(有 `get_pin` 但整端口读取无 getter);`SysTickPeripheral::ctrl_/load_/val_` 全私有、无 getter;NVIC 有完整 query API(`is_enabled`/`has_pending_irq`/`highest_pending_irq`)但无数 enabled 的 helper。

## 做了什么(G2)

1. **3 个 inline const getter**(纯只读):
   - `Stm32f1Gpio::odr()` — 整端口输出电平(GUI 比 16 次 get_pin 直接)。
   - `SysTickPeripheral::ctrl()/load()/val()` — COUNTFLAG 在 ctrl bit16。
   - `NvicPeripheral::enabled_count()` — popcount(ISER),比遍历 is_enabled(240 次)高效。
2. **PeripheralsSnapshot 扩展**:`GpioPortSnapshot{port,odr}[3]`(A/B/C)+ `SysTickSnapshot{ctrl,load,val}` + `NvicSnapshot{has_pending,highest_pending_irq,enabled_count}`。
3. **read_introspection 填充**:从 `soc.parts()` 读 gpioa/b/c/systick/nvic。**peripherals 填在 cm3 IsValid 早返回之前**(parts 总存在,不依赖 CPU 装好)。
4. **snapshot.cpp 序列化**:peripherals 区从 `{usart_output}` 扩为 `{usart_output, gpio[], systick{}, nvic{}}`。usart_output 仍是首 key → JSON 超集,test_cli 兼容。
5. **test_introspection 加 FreshSocPeripheralsAreQuiescent**:断言 FreshSoC 的 gpio 全 0 / systick 全 0 / nvic has_pending=false / enabled_count=0 / highest=0xFF。

## 设计要点

- **纯只读 behavior-preserving**:和 G1 同性质。getter 只在 read_introspection 调(每 UI tick 一次),不进 sim 热路径(`set_pin`/`tick`/`highest_priority_pending_irq` 机器码不变)。bench advisory 0 regression。
- **GPIO 用 odr 而非 get_pin**:odr 是固件写的输出电平,GUI 看 pin 翻转(GPIO blink 验收)正好用 odr;一次读 vs 16 次 get_pin。
- **NVIC enabled_count 用 `__builtin_popcount`**:与 nvic.hpp 现有 `__builtin_ctz` 同类内建,一致。

## 验证

- ctest **328/328**(Debug + RelWithDebInfo;原 327 + FreshSocPeripheralsAreQuiescent)。
- bench `--baseline` advisory PASS(0 regression;ratio 90–92% WSL2 噪声)。
- test_cli JSON 断言全过(peripherals 超集,usart_output 首位不变)。

## 下一步

- **G3**:EventBus 接线(Stm32f103Parts 加 EventBus 成员;GPIO `edge_signal_` + USART output → EventBus.gpio/uart;`set_cycle_source` 注入)。涉及 SoC 改 + emit 点 + 热路径 perf 考量(`on_odr_changed` 在 GPIO write 路径,emit 要轻)。
- **G4**:GuiSession 控制封装(step/pause/reset)+ scb/nvic coverage 收口(29%/62%)+ SCB getter(ICSR/VTOR)。
- **G5**:Qt6 GUI MVP。
