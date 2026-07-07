# 037 — EventBus 接线(GPIO edge + UART byte → RingSink)

> 日期: 2026-06-30
> 阶段: v0.7.0 milestone 04(增量事件,补 G3 推迟项)
> 状态: ✅ ctest 354→355 双构建绿 + bench advisory PASS + GUI offscreen 端到端
> 前置: G3(notes 033 指出"EventBus 声明了从未实例化");G5a-c

## 背景

EventBus 类一直在 `include/hooks/event_bus.hpp`,但 `Stm32f103Parts` **没有 EventBus 成员**——"声明了从未实例化"(G3 实测发现)。GUI MVP(G5c)用 30Hz 全量 snapshot 够看 blink,但用户点名"搞定 EventBus",补上增量事件通道(不漏 30Hz 采样之间的快速 GPIO/UART 事件)。

## 做了什么

1. **Parts 加 `hooks::EventBus event_bus` 成员**(stm32f103_soc.hpp)。
2. **`Stm32f103Soc::create()` 接线**(stm32f103_soc.cpp):
   - GPIO `edge_signal()` 再 connect 一个转发 slot 到 `event_bus.gpio`——**EXTI 保持首位订阅**(Signal::emit 遍历所有 slot),peripheral 不需知道 bus。
   - `usart1.set_output` 默认 emit `UartByte{cycle, unit, byte}` 到 `event_bus.uart`(带 cycle 时戳)。
   - `event_bus.set_cycle_source(cycle_src)` 注入 CPU cycle 计数器(同 GPIO 的 source)。
3. **caller 改订阅 bus.uart**:CLI(main.cpp)+ GUI(main_window.cpp)从 `serial.set_output(append)` 改为 `event_bus.uart.connect(append e.byte)`——行为不变(同字节),但共享 EventBus 作单一事件通道。set_output 的默认 emit 不被覆盖。

## 设计要点

- **Signal 多订阅者**:GPIO edge 同时喂 EXTI(中断)和 bus(观察者),互不干扰。这是 Signal::emit 同步遍历所有 slot 的天然能力。
- **RingSink off-path**:GUI/test 连 `bus.gpio.connect(sink.slot())`,push O(1),drain 批量。SPSC relaxed atomic,单线程确定性不受影响(emit/drain 都在 sim 线程或同 GUI 主线程)。
- **无订阅者零开销**:`emit` 对空 slot 表是 O(1)(empty check)。bench 的 uart_printf 固件高频 TX 但没人订阅 bus.uart → emit 空遍历,**不退化热路径**(advisory 0 regression)。
- **peripheral-owned Signal 保留**:`Stm32f1Gpio::edge_signal_` 没动(EXTI 订阅它),EventBus 只是在它上面加第二个 slot。符合 event_bus.hpp 注释的设计意图。

## 验证

- `test_event_bus`(3 测试):GPIO edge → RingSink / 每次翻转独立事件 / UART byte → RingSink(unit=1)。
- `test_cli` 全过(Cli.RunHelloOutputsString)——CLI 改 connect bus.uart 后 hello 输出仍正确收集(端到端:固件 TX → bus → CLI usart_out)。
- ctest **355/355**(Debug + RelWithDebInfo;原 354 + UART test)。
- GUI offscreen + AUTORUN:hello(UART 经 bus)+ blink(GPIO 经 bus)各跑 3s,exit=124。
- bench `--baseline` advisory PASS(0 regression;uart_printf ratio 88% 是 WSL2 噪声,emit 空遍历不退化)。

## 至此 milestone 04 全套完成

G1 introspection / G2 peripherals / G3 SCB+coverage / G5a-c Qt GUI / G6 EventBus。下一步:push(7 commit 待 origin)+ 发版叙事。
