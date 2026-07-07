# micro-forge 支持矩阵 · v0.2.0

> 本文明确 micro-forge v0.2.0 **支持什么、不支持什么**。决策依据见
> [`document/milestones/00-v1-roadmap.md`](milestones/00-v1-roadmap.md) 与
> [`05-extension-architecture.md`](milestones/05-extension-architecture.md)。

## CPU / 指令集

- **支持**：ARM **Cortex-M3**（ARMv7-M base profile，无 DSP 扩展）。
- **指令集**：Thumb-16 全集 + Thumb-32 全集（ARMv7-M 范围）。ARMv7E-M DSP 指令
  （QADD / UMAAL / PKH / SEL / SXTAH 等）在 M3 上本应 `UndefinedInstruction`，
  已加 clean-fault 门禁。
- **异常/中断**：抢占与嵌套、PRIMASK / BASEPRI / FAULTMASK、PRIGROUP 优先级分组、
  MSP/PSP 双栈与 EXC_RETURN、bit-band 别名区。
- **非目标**：tail-chaining（同步模拟器天然退化，见 notes 005）、late-arrival
  （同理）。RISC-V / AVR / 8051 / Cortex-M4/M7（v1.0 不实现，但架构边界已预留）。

## 芯片 / SoC

- **支持**：**STM32F103**（首个 SoC package）。
- **非目标**：GD32 或其他 STM32 变体（v1.0 不实现，`SoC package` 边界已留）。

## 外设（STM32F103）

| 外设 | 状态 | 说明 |
|------|------|------|
| NVIC | 完整 | 抢占/嵌套/PRIMASK/BASEPRI/PRIGROUP，lazy priority cache |
| SCB | 完整 | VTOR / AIRCR 写回调注入 CPU |
| SysTick | 完整 | CTRL / LOAD / VAL，经 SHPR 查询优先级 |
| GPIO A/B/C | 完整 | ODR/IDR，bit-band 别名，边沿 emit 到事件总线 |
| USART1 | TX polling + RX 注入 | RXNEIE 中断；TXEIE 跳过（TX 即时，无延迟可消耗） |
| TIM2 | UIF → NVIC E2E | PSC/ARR/CNT + VirtualClock 联动 |
| RCC | 轮询友好 | 就绪标志立即就绪、SR 永不 BUSY |
| AFIO | 完整 | EXTICR 路由到 EXTI |
| EXTI | 完整 | IMR/EMR/RTSR/FTSR/SWIER/PR，线 → IRQ 路由 |
| FLASH | **外壳 only** | 寄存器读写 real；**无** unlock/erase/program 状态机 |
| SPI / I2C / ADC / DMA | **未实现** | 零代码（待具体固件需求驱动） |

## 工具链

- **编译**：GCC-14+（C++23 严格），CMake 3.25+，`-Wall -Wextra -Werror`。
- **测试**：GoogleTest，353 产品用例 + 2 meta-guard；E2E 含真实 Keil/MDK-ARM HAL 固件。
- **覆盖率**：gcov + gcovr（`-DMICRO_FORGE_COVERAGE=ON`），lines 81.6% / branches 61.4%。
- **正确性**：QEMU 差分 oracle（`qemu-system-gnuarmeless` mps2-an385 + gdbstub）。
- **CI**：GitHub Actions，GCC-14 build+test 与交叉编译 E2E 两条流水线。

## 固件格式

- **ELF32**：ARM machine、little-endian（loader 校验 magic / class / data / machine）。
- **Raw binary**：`--base` 指定加载地址。

## 设计上的非目标（v1.0）

- **周期精确**（cycle-accurate）模拟 —— 明确不追。
- **GDB remote protocol** / 交互式调试器协议 —— v0.2 仅 file/stdout JSON snapshot。
- **HTTP / WebSocket / MCP** 服务接口 —— 不实现常驻服务。
- **多线程模拟** —— 单线程确定性是设计目标（确定性可重放）。
- **插件 ABI 稳定** —— v1.0 只承诺 C++ package 级扩展边界清晰。
