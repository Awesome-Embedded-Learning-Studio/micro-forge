# micro-forge

An ARM Cortex-M3 (STM32F103) simulator written in modern C++23 — run and test embedded firmware without hardware.

基于 C++23 的 ARM Cortex-M3 (STM32F103) 模拟器 — 无需硬件即可运行和测试嵌入式固件。

[![CI Build & Test](https://github.com/Awesome-Embedded-Learning-Studio/micro-forge/actions/workflows/ci.yml/badge.svg)](https://github.com/Awesome-Embedded-Learning-Studio/micro-forge/actions/workflows/ci.yml)
[![Cross-Compile & E2E](https://github.com/Awesome-Embedded-Learning-Studio/micro-forge/actions/workflows/cross-compile.yml/badge.svg)](https://github.com/Awesome-Embedded-Learning-Studio/micro-forge/actions/workflows/cross-compile.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

## Features

- **Cortex-M3 CPU** — Full Thumb-16 and Thumb-32 instruction set, ARMv7-M exception handling, and bit-band alias regions (single-bit atomic RMW via the peripheral `0x42xxxxxx` and SRAM `0x22xxxxxx` word-alias windows)
- **STM32F103 SoC** — Memory map, clock tree, and peripheral register-level simulation
- **Peripheral Suite** — NVIC, SCB, SysTick, RCC, GPIO (A/B/C), USART1, TIM2, AFIO, FLASH
- **Firmware Loading** — ELF loader and raw binary loader
- **CLI & Diagnostics** — `micro-forge run` drives firmware with MMIO trace, memory dump, fault recording with context, and `--snapshot-json` JSON state export (CPU / fault / peripherals / recent MMIO)
- **Event Bus & Hooks** — Typed observer subsystem (`Signal<E>`, non-blocking `RingSink<E>`, `EventBus`) emitting `GpioEdge` / `UartByte` events with CPU-cycle timestamps — single-threaded and deterministic, drained offline rather than via a thread pool
- **Real-Firmware Proven** — Boots **real Keil/MDK-ARM STM32F103 HAL firmware** end-to-end: reset → `__main` scatter-load → `main` → `HAL_Init` → `SystemClock_Config` (PLL switch) → `MX_GPIO_Init` → `HAL_GPIO_WritePin(PA1)` → `while(1)` with SysTick IRQ
- **Well Tested** — 244 test cases across 19 test files with GoogleTest

## Quick Start

### Prerequisites

- GCC-14 or later (C++23 support required)
- CMake 3.25+
- For cross-compile examples: `arm-none-eabi` toolchain

### Build

```bash
git clone --recursive https://github.com/Awesome-Embedded-Learning-Studio/micro-forge.git
cd micro-forge
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

### Run Tests

```bash
cd build
ctest --output-on-failure -j$(nproc)
```

### Run Firmware

```bash
# Run a firmware image. `run` executes forever by default;
# Ctrl+C stops gracefully and prints a status report.
./build/micro-forge run examples/F103/MDK-ARM/F103/F103.axf
```

### Export State & Observe

```bash
# Export CPU / fault / peripheral / MMIO state as JSON on exit
./build/micro-forge run examples/F103/MDK-ARM/F103/F103.axf --snapshot-json state.json

# Trace MMIO accesses while running
./build/micro-forge run firmware.elf --trace-mmio

# Capture GPIO edges: subscribe via the event-bus hooks subsystem,
# see examples/hook_demo/ and include/hooks/ (Signal / RingSink / EventBus)
```

> Want to observe pin toggles live? `examples/hook_demo` subscribes to the
> event bus and captures PA1 toggles emitted by the GPIO peripheral.

## Project Structure

```
include/
  core/       Base types and interfaces (IPeripheral, types)
  cpu/        CPU framework (ICore, RegisterFile, ToyCore)
  memory/     Memory system (FlatMemory, Bus, Region, bit-band aliases)
  periph/     Peripheral abstractions (Device, Gpio, SerialPort, Timer)
  hooks/      Event bus (Signal, RingSink, EventBus, GpioEdge/UartByte)
  cli/        Command-line interface (snapshot, main)
  util/       WeakPtr lifecycle management
src/          Implementation files
test/         GoogleTest suite (244 tests)
examples/     Firmware examples (bare-metal + HAL + Keil/MDK + hook demos)
document/
  milestones/ Version roadmap (v0.1.0 → v1.0.0)
  notes/      Design notes by topic
scripts/      Utility scripts
```

## Examples

| Example | Description |
|---------|-------------|
| `hello_world` | Bare-metal UART output via register-level MMIO |
| `gpio_blink` | GPIO pin toggling with direct register access |
| `systick` | SysTick timer interrupt and tick counting |
| `hal_blink` | GPIO blink using STM32F1 HAL library |
| `hal_uart` | UART transmission using STM32F1 HAL |
| `F103` | Real Keil/MDK-ARM STM32F103 HAL GPIO firmware — the end-to-end regression carrier (`examples/F103/MDK-ARM/F103/F103.axf`) |
| `hook_demo` | Subscribes to the event bus to capture PA1 toggles live |

## Roadmap

See [document/milestones/](document/milestones/) for the full version roadmap.

Already landed: the **`micro-forge run` CLI** with `--snapshot-json` state export and MMIO tracing, the **Cortex-M3 correctness foundation** (interrupt preemption, MSP/PSP dual-stack, PRIGROUP, NVIC priority cache, bit-band aliases), the **Thumb-2 instruction-coverage fix** that lets real Keil/MDK-ARM HAL firmware boot end-to-end, and the **event-bus hooks subsystem**.

Key milestones ahead: full exception semantics tail-chaining, EXTI / Timer-IRQ / USART-RX-IRQ end-to-end paths, DMA / SPI / FLASH depth, extended HAL peripheral coverage, and a GUI debug dashboard.

## License

This project is licensed under the [MIT License](LICENSE).

## Contributing
geqianQWQ: Provide the suggestions of Compatible of Keil, and provide some examples.

---

## 中文说明

**micro-forge** 是一个用 C++23 编写的 ARM Cortex-M3 模拟器，目标芯片为 STM32F103（Cortex-M3 内核，72 MHz，128 KB Flash，20 KB SRAM）。

### 项目简介

micro-forge 在主机上模拟 STM32F103 的 CPU 指令执行、内存总线和外设寄存器，让你无需真实硬件就能运行和调试嵌入式固件。支持裸机编程和 STM32 HAL 库两种风格，并能跑通真实 Keil/MDK-ARM 生成的 HAL 固件。

### 核心能力

- **CPU 模拟**：完整 Thumb-16/Thumb-32 指令集，ARMv7-M 异常处理，Cortex-M3 位带别名区（外设 `0x42xxxxxx` / SRAM `0x22xxxxxx` 单 bit 原子 RMW）
- **外设模拟**：NVIC、SCB、SysTick、RCC、GPIO、USART、TIM2、AFIO、FLASH
- **固件加载**：支持 ELF 和原始二进制格式
- **CLI 与诊断**：`micro-forge run` 驱动固件，支持 MMIO 追踪、内存转储、故障记录、`--snapshot-json` 导出 CPU/外设/MMIO 状态
- **事件总线与 Hooks**：`Signal<E>` / `RingSink<E>` / `EventBus` 类型化观察者，发射带 cycle 时戳的 `GpioEdge`/`UartByte` 事件；单线程确定性，离线 drain，不走线程池
- **真实固件验证**：真实 Keil/MDK-ARM STM32F103 HAL 固件端到端跑通（reset → `__main` scatter-load → `main` → `HAL_Init` → `SystemClock_Config` 切 PLL → `MX_GPIO_Init` → `HAL_GPIO_WritePin(PA1)` → `while(1)`，SysTick 中断正常）
- **测试覆盖**：244 个 GoogleTest 用例，包含端到端固件测试

### 构建与运行

```bash
git clone --recursive https://github.com/Awesome-Embedded-Learning-Studio/micro-forge.git
cd micro-forge
cmake -B build
cmake --build build -j$(nproc)
cd build && ctest --output-on-failure -j$(nproc)

# 运行固件：默认无限执行，Ctrl+C 优雅退出并打印状态报告
./build/micro-forge run examples/F103/MDK-ARM/F103/F103.axf
# 导出状态为 JSON
./build/micro-forge run firmware.axf --snapshot-json state.json
# 观测 GPIO 翻转：通过事件总线订阅，见 examples/hook_demo 与 include/hooks/
```

### 项目结构

| 目录 | 说明 |
|------|------|
| `include/core/` | 基础类型和接口 |
| `include/cpu/` | CPU 核心框架 |
| `include/memory/` | 内存系统（FlatMemory、Bus、Region） |
| `include/periph/` | 外设抽象接口 |
| `src/` | 实现文件 |
| `test/` | 测试用例（GoogleTest） |
| `examples/` | 固件示例（裸机 + HAL） |
| `document/` | 设计文档和开发路线图 |
