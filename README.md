[简体中文](README.md) | [English](README.en.md)

# micro-forge

基于 C++23 的 ARM Cortex-M3（STM32F103）模拟器 —— 无需硬件即可运行和测试嵌入式固件。

[![CI Build & Test](https://github.com/Awesome-Embedded-Learning-Studio/micro-forge/actions/workflows/ci.yml/badge.svg)](https://github.com/Awesome-Embedded-Learning-Studio/micro-forge/actions/workflows/ci.yml)
[![Cross-Compile & E2E](https://github.com/Awesome-Embedded-Learning-Studio/micro-forge/actions/workflows/cross-compile.yml/badge.svg)](https://github.com/Awesome-Embedded-Learning-Studio/micro-forge/actions/workflows/cross-compile.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

## 特性

- **Cortex-M3 CPU** —— 完整的 Thumb-16 / Thumb-32 指令集，ARMv7-M 异常处理，以及位带（bit-band）别名区（通过外设 `0x42xxxxxx` 和 SRAM `0x22xxxxxx` 的字别名窗口实现单 bit 原子读改写）
- **STM32F103 SoC** —— 内存映射、时钟树、外设寄存器级模拟
- **外设套件** —— NVIC、SCB、SysTick、RCC、GPIO（A/B/C）、USART1、TIM2、AFIO、FLASH
- **固件加载** —— ELF 加载器和原始二进制加载器
- **CLI 与诊断** —— `micro-forge run` 驱动固件运行，支持 MMIO 追踪、内存转储、带上下文的故障记录，以及 `--snapshot-json` JSON 状态导出（CPU / 故障 / 外设 / 近期 MMIO）
- **事件总线与 Hooks** —— 类型化观察者子系统（`Signal<E>`、非阻塞 `RingSink<E>`、`EventBus`），发射带 CPU 周期时戳的 `GpioEdge` / `UartByte` 事件 —— 单线程、确定性，离线 drain 而非线程池
- **真实固件验证** —— 端到端跑通**真实 Keil/MDK-ARM STM32F103 HAL 固件**：reset → `__main` scatter-load → `main` → `HAL_Init` → `SystemClock_Config`（切 PLL）→ `MX_GPIO_Init` → `HAL_GPIO_WritePin(PA1)` → `while(1)`，SysTick 中断正常
- **测试完善** —— 19 个测试文件共 244 个 GoogleTest 用例

## 快速开始

### 前置条件

- GCC-14 或更高版本（需要 C++23 支持）
- CMake 3.25+
- 交叉编译示例需要：`arm-none-eabi` 工具链

### 编译

```bash
git clone --recursive https://github.com/Awesome-Embedded-Learning-Studio/micro-forge.git
cd micro-forge
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

### 运行测试

```bash
cd build
ctest --output-on-failure -j$(nproc)
```

### 运行固件

```bash
# 运行固件镜像。`run` 默认无限执行；
# Ctrl+C 优雅退出并打印状态报告。
./build/micro-forge run examples/F103/MDK-ARM/F103/F103.axf
```

### 导出状态与观测

```bash
# 退出时将 CPU / 故障 / 外设 / MMIO 状态导出为 JSON
./build/micro-forge run examples/F103/MDK-ARM/F103/F103.axf --snapshot-json state.json

# 运行时追踪 MMIO 访问
./build/micro-forge run firmware.elf --trace-mmio

# 捕获 GPIO 边沿：通过事件总线 hooks 子系统订阅，
# 见 examples/hook_demo/ 和 include/hooks/（Signal / RingSink / EventBus）
```

> 想实时观察引脚翻转？`examples/hook_demo` 订阅事件总线，
> 捕获 GPIO 外设发出的 PA1 翻转事件。

## 项目结构

```
include/
  core/       基础类型和接口（IPeripheral、types）
  cpu/        CPU 框架（ICore、RegisterFile、ToyCore）
  memory/     内存系统（FlatMemory、Bus、Region、位带别名）
  periph/     外设抽象（Device、Gpio、SerialPort、Timer）
  hooks/      事件总线（Signal、RingSink、EventBus、GpioEdge/UartByte）
  cli/        命令行界面（snapshot、main）
  util/       WeakPtr 生命周期管理
src/          实现文件
test/         GoogleTest 测试套件（244 个测试）
examples/     固件示例（裸机 + HAL + Keil/MDK + hook 演示）
document/
  milestones/ 版本路线图（v0.1.0 → v1.0.0）
  notes/      按主题组织的设计笔记
scripts/      工具脚本
```

## 示例

| 示例 | 说明 |
|---------|-------------|
| `hello_world` | 通过寄存器级 MMIO 输出 UART |
| `gpio_blink` | 直接访问寄存器翻转 GPIO 引脚 |
| `systick` | SysTick 定时器中断与计数 |
| `hal_blink` | 使用 STM32F1 HAL 库翻转 GPIO |
| `hal_uart` | 使用 STM32F1 HAL 进行 UART 发送 |
| `F103` | 真实 Keil/MDK-ARM STM32F103 HAL GPIO 固件 —— 端到端回归载体（`examples/F103/MDK-ARM/F103/F103.axf`） |
| `hook_demo` | 订阅事件总线，实时捕获 PA1 翻转 |

## 路线图

完整版本路线图见 [document/milestones/](document/milestones/)。

已落地：**`micro-forge run` CLI**（含 `--snapshot-json` 状态导出和 MMIO 追踪）、**Cortex-M3 正确性基础**（中断抢占、MSP/PSP 双栈、PRIGROUP、NVIC 优先级缓存、位带别名）、**Thumb-2 指令覆盖修复**（让真实 Keil/MDK-ARM HAL 固件端到端跑通），以及**事件总线 hooks 子系统**。

后续关键里程碑：完整的异常语义尾链（tail-chaining）、EXTI / Timer-IRQ / USART-RX-IRQ 端到端通路、DMA / SPI / FLASH 深化、HAL 外设覆盖扩展，以及 GUI 调试仪表盘。

## 许可证

本项目基于 [MIT 许可证](LICENSE)授权。

## 鸣谢

感谢所有用想法、修复和反馈帮助塑造 micro-forge 的朋友：

- **geqianQWQ** —— Keil 兼容性建议及示例固件。
- **Leon19960120** —— 提出 README 中英文分离、顶部加语言切换链接的建议（[#6](https://github.com/Awesome-Embedded-Learning-Studio/micro-forge/issues/6)）。
