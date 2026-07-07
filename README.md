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
- **GUI 仪表盘** —— 可选的 Qt6 调试仪表盘（`-DMICRO_FORGE_GUI=ON`），单步 / 运行 / 复位，可视化 CPU 寄存器与 GPIO 输出；模拟器同步跑在 Qt 主线程，保持单线程确定性
- **事件总线与 Hooks** —— 类型化观察者子系统（`Signal<E>`、非阻塞 `RingSink<E>`、`EventBus`），发射带 CPU 周期时戳的 `GpioEdge` / `UartByte` 事件 —— 单线程、确定性，离线 drain 而非线程池
- **真实固件验证** —— 端到端跑通**真实 Keil/MDK-ARM STM32F103 HAL 固件**：reset → `__main` scatter-load → `main` → `HAL_Init` → `SystemClock_Config`（切 PLL）→ `MX_GPIO_Init` → `HAL_GPIO_WritePin(PA1)` → `while(1)`，SysTick 中断正常
- **测试完善** —— 19 个测试文件共 353 个 GoogleTest 用例

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

### GUI 仪表盘（可选）

默认构建不含 GUI（核心库与 CLI 零 Qt 依赖）。需要可视化调试仪表盘时：

```bash
cmake -B build-gui -DMICRO_FORGE_GUI=ON
cmake --build build-gui -j$(nproc)
./build-gui/gui/micro-forge-gui firmware.axf   # 单步 / 运行 / 复位，看寄存器与 GPIO
```

GUI 是核心库的纯消费者，复用 `read_introspection` 同一份数据；模拟器同步跑在 Qt 主线程，确定性可重放。

## 项目结构

```
include/
  core/           基础类型和接口（IPeripheral、types）
  cpu/            CPU 框架（ICore、RegisterFile、FaultRecord）
  memory/         内存系统（FlatMemory、Bus、Region、位带别名）
  periph/         外设抽象（Device、Gpio、SerialPort、Timer）
  hooks/          事件总线（Signal、RingSink、EventBus、GpioEdge/UartByte）
  introspection/  结构化状态导出（read_introspection —— CLI/GUI 共享的单一事实来源）
  util/           WeakPtr 生命周期管理
src/              核心库实现（arch / chips / cpu / memory / periph / sim / loader / tools / util）
cli/              CLI 可执行（main、snapshot）—— 顶层消费者
gui/              Qt6 仪表盘可执行（main、main_window）—— 顶层消费者，opt-in
test/             GoogleTest 测试套件（353 个测试）
examples/         固件示例（裸机 + HAL + Keil/MDK + hook 演示）
document/
  milestones/     版本路线图（v0.1.0 → v1.0.0）
  notes/          按主题组织的设计笔记
scripts/          工具脚本（测试数地板门 / QEMU oracle / venv 网关）
bench/            性能基准（含 baseline 回归软门）
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

## 开发工具链

- **覆盖率** —— `-DMICRO_FORGE_COVERAGE=ON` 编 gcov 插桩版，配 `gcovr` 出报告（当前 lines 81.6% / branches 61.4%）。
- **性能基准** —— `bench/` 附 baseline 回归软门；perf 战役让 GPIO / USART / TIM 指令吞吐 +51-57%。
- **QEMU 差分 oracle** —— `scripts/qemu_cortex_m3_oracle.sh` 跟真机 QEMU 逐字对照指令语义（SDIV / UDIV / ADC / SBC + xPSR）。
- **测试数地板门** —— `scripts/check_test_count.sh` 锁基线，防重构静默吞测试。

## 路线图

完整版本路线图见 [document/milestones/](document/milestones/)；v0.2.0 支持范围见 [支持矩阵](document/SUPPORT.md)，变更历史见 [CHANGELOG](CHANGELOG.md)。

已落地：

- **CLI 与可观测性** —— `micro-forge run` + `--snapshot-json` + `--trace-mmio`；结构化 introspection 作为 CLI / GUI 共享的单一事实来源
- **Cortex-M3 正确性基础** —— 中断抢占与嵌套、MSP/PSP 双栈、PRIGROUP 优先级分组、NVIC 优先级缓存、位带别名区
- **Thumb-2 指令全覆盖** —— 真实 Keil/MDK-ARM HAL 固件端到端跑通
- **三条中断通道端到端** —— Timer UIF / EXTI GPIO 边沿 / USART RXNE，全经公共 `raise_irq` 通道
- **事件总线 hooks** —— 类型化观察者，带 CPU 周期时戳
- **Qt6 GUI 仪表盘** —— 单步 / 运行 / 复位，可视化 CPU 寄存器与 GPIO（opt-in）

后续关键里程碑：DMA / SPI / FLASH 外设深化、HAL 外设覆盖扩展、GUI 调试面板继续兑现（完整 fault 详情、串口终端、中断系统可视化）。

## 许可证

本项目基于 [MIT 许可证](LICENSE)授权。

## 鸣谢

感谢所有用想法、修复和反馈帮助塑造 micro-forge 的朋友：

- **geqianQWQ** —— Keil 兼容性建议及示例固件。
- **Leon19960120** —— 提出 README 中英文分离、顶部加语言切换链接的建议（[#6](https://github.com/Awesome-Embedded-Learning-Studio/micro-forge/issues/6)）。
