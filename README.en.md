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
- **GUI Dashboard** — Optional Qt6 debug dashboard (`-DMICRO_FORGE_GUI=ON`): step / run / reset with visual CPU registers and GPIO output; the simulator runs synchronously on the Qt main thread, preserving single-threaded determinism
- **Event Bus & Hooks** — Typed observer subsystem (`Signal<E>`, non-blocking `RingSink<E>`, `EventBus`) emitting `GpioEdge` / `UartByte` events with CPU-cycle timestamps — single-threaded and deterministic, drained offline rather than via a thread pool
- **Real-Firmware Proven** — Boots **real Keil/MDK-ARM STM32F103 HAL firmware** end-to-end: reset → `__main` scatter-load → `main` → `HAL_Init` → `SystemClock_Config` (PLL switch) → `MX_GPIO_Init` → `HAL_GPIO_WritePin(PA1)` → `while(1)` with SysTick IRQ
- **Well Tested** — 353 test cases across 19 test files with GoogleTest

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

### GUI Dashboard (optional)

The default build omits the GUI (core library and CLI stay Qt-free). For the
visual debug dashboard:

```bash
cmake -B build-gui -DMICRO_FORGE_GUI=ON
cmake --build build-gui -j$(nproc)
./build-gui/gui/micro-forge-gui firmware.axf   # step / run / reset, inspect registers & GPIO
```

The GUI is a pure consumer of the core library, reusing the same
`read_introspection` data; the simulator runs synchronously on the Qt main
thread for deterministic replay.

## Project Structure

```
include/
  core/           Base types and interfaces (IPeripheral, types)
  cpu/            CPU framework (ICore, RegisterFile, FaultRecord)
  memory/         Memory system (FlatMemory, Bus, Region, bit-band aliases)
  periph/         Peripheral abstractions (Device, Gpio, SerialPort, Timer)
  hooks/          Event bus (Signal, RingSink, EventBus, GpioEdge/UartByte)
  introspection/  Structured state export (read_introspection — single source of truth shared by CLI/GUI)
  util/           WeakPtr lifecycle management
src/              Core library implementation (arch / chips / cpu / memory / periph / sim / loader / tools / util)
cli/              CLI executable (main, snapshot) — top-level consumer
gui/              Qt6 dashboard executable (main, main_window) — top-level consumer, opt-in
test/             GoogleTest suite (353 tests)
examples/         Firmware examples (bare-metal + HAL + Keil/MDK + hook demos)
document/
  milestones/     Version roadmap (v0.1.0 → v1.0.0)
  notes/          Design notes by topic
scripts/          Utility scripts (test-count floor / QEMU oracle / venv gateway)
bench/            Performance benchmarks (with baseline regression gate)
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

## Development Tooling

- **Coverage** — Build gcov-instrumented binaries with `-DMICRO_FORGE_COVERAGE=ON` and generate reports via `gcovr` (currently lines 81.6% / branches 61.4%).
- **Benchmarks** — `bench/` with a baseline regression gate; the perf campaign lifted GPIO / USART / TIM instruction throughput by 51-57%.
- **QEMU differential oracle** — `scripts/qemu_cortex_m3_oracle.sh` checks instruction semantics (SDIV / UDIV / ADC / SBC + xPSR) against real QEMU, byte-for-byte.
- **Test-count floor** — `scripts/check_test_count.sh` pins the baseline so refactors can't silently drop tests.

## Roadmap

See [document/milestones/](document/milestones/) for the full version roadmap.

Already landed:

- **CLI & observability** — `micro-forge run` + `--snapshot-json` + `--trace-mmio`; structured introspection is the single source of truth shared by CLI and GUI
- **Cortex-M3 correctness foundation** — interrupt preemption and nesting, MSP/PSP dual-stack, PRIGROUP priority grouping, NVIC priority cache, bit-band alias regions
- **Full Thumb-2 coverage** — real Keil/MDK-ARM HAL firmware boots end-to-end
- **Three interrupt paths end-to-end** — Timer UIF / EXTI GPIO edge / USART RXNE, all via the common `raise_irq` channel
- **Event-bus hooks** — typed observers with CPU-cycle timestamps
- **Qt6 GUI dashboard** — step / run / reset, visual CPU registers and GPIO (opt-in)

Key milestones ahead: DMA / SPI / FLASH peripheral depth, extended HAL peripheral coverage, and more GUI debug panels (full fault detail, serial terminal, interrupt-system visualization).

## License

This project is licensed under the [MIT License](LICENSE).

## Acknowledgements

Thanks to everyone who has shaped micro-forge with ideas, fixes, and feedback:

- **geqianQWQ** — Keil compatibility suggestions and example firmware.
- **Leon19960120** — Suggested splitting the README into English / Chinese with a top language switcher ([#6](https://github.com/Awesome-Embedded-Learning-Studio/micro-forge/issues/6)).
