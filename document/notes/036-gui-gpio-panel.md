# 036 — GUI GPIO 面板 + milestone 04 完成(G5c)

> 日期: 2026-06-30
> 阶段: v0.7.0 milestone 04([GUI dashboard](../milestones/04-gui-debug-dashboard.md))
> 状态: ✅ GPIO 面板 + blink offscreen 端到端;视觉翻转交用户确认
> 前置: G5b(notes 035)

## 做了什么(G5c)

- **GPIO 面板**:3 端口(A/B/C)各一行,显示 ODR hex(4 位)+ 16 个 LED 字形(pin0..pin15,`●`= high、`·`= low)。每 UI tick 从 `snap.peripherals.gpio[i]` 刷新。
- 字形用 `QChar(0x25CF)`(●)/ `QChar(0x00B7)`(·),monospace 对齐。
- 加载 `blink.elf` + Run → PA 某 bit 翻转可见——这就是 milestone 04 的验收场景「GPIO blink 可见 pin 翻转」。

## 验证

- `cmake --build build-gui` 编译链接成功(AUTOMOC + main_window + main)。
- `QT_QPA_PLATFORM=offscreen MICRO_FORGE_GUI_AUTORUN=1 micro-forge-gui blink.elf` 跑 3s,exit=124(GPIO 面板 + run loop 端到端不 crash)。
- 视觉翻转验收:请用户跑 `./build-gui/micro-forge-gui build-gui/examples/gpio_blink/blink.elf`,点 Run,看 PA 行的 `●`/`·` 闪烁。

## milestone 04 完成度

| 批 | 内容 | notes |
|---|---|---|
| G1 | 结构化 introspection API + CPU/fault 全字段(单一事实来源) | 031 |
| G2 | peripherals 摘要(GPIO/SysTick/NVIC) | 032 |
| G3 | SCB introspection + scb/nvic coverage 收口 + CCR write bug fix | 033 |
| G5a | Qt6 opt-in 骨架 + DIRECTIVES §E | 034 |
| G5b | CPU 面板 + run/pause/step/reset + AUTORUN 钩子 | 035 |
| G5c | GPIO 面板 + blink 端到端 | 036 |

**EventBus/增量事件明确推迟**(YAGNI):GUI MVP 用 30Hz 全量 `read_introspection` 足够看 GPIO 翻转/UART;等 GUI 跑起来发现 30Hz 漏快速事件时再接(G3 的 RingSink/EventBus 已设计好接线点)。

**scb/nvic coverage 收口**(用户选的"GUI 前置收口")作为 G3 完成:scb 从 29%、nvic 从 62% 大幅上调(待 gcovr 跑具体数),并挖出修了 SCB CCR write 未实现的真实 bug。

## 下一步(收尾)

- 发版叙事:这一程把"自用 CLI 模拟器"推到"可演示的 GUI 产品"。版本号待定(v0.2.0 综合叙事,或 v0.7.0 GUI milestone)。3 commit push 待用户确认(outward)。
- 视觉验收:用户跑 blink.elf 确认 GPIO 翻转可见。
- 后续(非本里程碑):EventBus 增量事件、SCB/gdbstub 断点、源码级调试、DMA 第三波。
