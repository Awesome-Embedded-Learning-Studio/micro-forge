[简体中文](README.md) | [English](README.en.md)

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

## Acknowledgements

Thanks to everyone who has shaped micro-forge with ideas, fixes, and feedback:

- **geqianQWQ** — Keil compatibility suggestions and example firmware.
- **Leon19960120** — Suggested splitting the README into English / Chinese with a top language switcher ([#6](https://github.com/Awesome-Embedded-Learning-Studio/micro-forge/issues/6)).
