# micro-forge

An ARM Cortex-M3 (STM32F103) simulator written in modern C++23 — run and test embedded firmware without hardware.

基于 C++23 的 ARM Cortex-M3 (STM32F103) 模拟器 — 无需硬件即可运行和测试嵌入式固件。

[![CI Build & Test](https://github.com/Awesome-Embedded-Learning-Studio/micro-forge/actions/workflows/ci.yml/badge.svg)](https://github.com/Awesome-Embedded-Learning-Studio/micro-forge/actions/workflows/ci.yml)
[![Cross-Compile & E2E](https://github.com/Awesome-Embedded-Learning-Studio/micro-forge/actions/workflows/cross-compile.yml/badge.svg)](https://github.com/Awesome-Embedded-Learning-Studio/micro-forge/actions/workflows/cross-compile.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

## Features

- **Cortex-M3 CPU** — Full Thumb-16 and Thumb-32 instruction set, ARMv7-M exception handling
- **STM32F103 SoC** — Memory map, clock tree, and peripheral register-level simulation
- **Peripheral Suite** — NVIC, SCB, SysTick, RCC, GPIO (A/B/C), USART1, TIM2, AFIO, FLASH
- **Firmware Loading** — ELF loader and raw binary loader
- **Diagnostics** — MMIO trace, memory dump, fault recording with context
- **HAL Support** — Runs STM32F1 HAL-based firmware alongside bare-metal code
- **Well Tested** — 217 test cases across 19 test files with GoogleTest

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

## Project Structure

```
include/
  core/       Base types and interfaces (IPeripheral, types)
  cpu/        CPU framework (ICore, RegisterFile, ToyCore)
  memory/     Memory system (FlatMemory, Bus, Region)
  periph/     Peripheral abstractions (Device, Gpio, SerialPort, Timer)
  util/       WeakPtr lifecycle management
src/          Implementation files
test/         GoogleTest suite (217 tests)
examples/     Firmware examples (bare-metal + HAL)
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

## Roadmap

See [document/milestones/](document/milestones/) for the full version roadmap.

Key milestones ahead: CLI diagnostics, AI state snapshot, full exception semantics, NVIC priority nesting, extended HAL peripheral coverage, and a GUI debug dashboard.

## License

This project is licensed under the [MIT License](LICENSE).

## Contributing
geqianQWQ: Provide the suggestions of Compatible of Keil, and provide some examples.

---

## 中文说明

**micro-forge** 是一个用 C++23 编写的 ARM Cortex-M3 模拟器，目标芯片为 STM32F103（Cortex-M3 内核，72 MHz，128 KB Flash，20 KB SRAM）。

### 项目简介

micro-forge 在主机上模拟 STM32F103 的 CPU 指令执行、内存总线和外设寄存器，让你无需真实硬件就能运行和调试嵌入式固件。支持裸机编程和 STM32 HAL 库两种风格。

### 核心能力

- **CPU 模拟**：完整 Thumb-16/Thumb-32 指令集，ARMv7-M 异常处理
- **外设模拟**：NVIC、SCB、SysTick、RCC、GPIO、USART、TIM2、AFIO、FLASH
- **固件加载**：支持 ELF 和原始二进制格式
- **诊断工具**：MMIO 追踪、内存转储、故障记录
- **测试覆盖**：217 个 GoogleTest 用例，包含端到端固件测试

### 构建方法

```bash
git clone --recursive https://github.com/Awesome-Embedded-Learning-Studio/micro-forge.git
cd micro-forge
cmake -B build
cmake --build build -j$(nproc)
cd build && ctest --output-on-failure -j$(nproc)
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
