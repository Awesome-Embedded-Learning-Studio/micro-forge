# Phase 7.6 · 进度校准与路线重排

> 日期: 2026-06-19
> 阶段: v0.1.0 → v1.0.0(对 [00-v1-roadmap](00-v1-roadmap.md) 的实测校准)
> 状态: 待评审
> 依赖: 当前 main 分支代码实测(217/217 测试通过)、01–05 各阶段目标文档

---

## 本文档定位

[00-v1-roadmap](00-v1-roadmap.md) 给出了 v0.1.0 → v1.0.0 的**线性版本路线**,01–05 各阶段文档写了每个版本的**目标与验收**。但它们都停留在「目标」层(状态一律"待评审"),缺少一份基于**当前代码实测**的进度校准。

本文档补这个缺口,只做三件事:

1. 用代码证据校准每个里程碑的**实际完成度**。
2. 诊断当前进度与 00 线性顺序的**错位**。
3. 给出基于"真实可靠"目标的**重排路线与理由**(不重抄 01–05 的目标细节,只引用并标注 delta)。

它不替代 00–05,而是夹在它们之间的一层「现在站在哪、下一步该先走哪」的判断层。

## 「真实可靠」的两个支柱

用户的目标是"模拟一个真实可靠的单片机"。把它拆成两个可验收的支柱,后续进度都按它们衡量:

- **真实性(Fidelity)**:真实固件跑进来**行为正确**。核心是 CPU/异常/中断语义符合 ARMv7-M 规范子集——指令解码、异常入口/返回、栈切换、中断屏蔽、抢占嵌套、bit-band。
- **可信度(Trust)**:模拟结果**可被验证、可被诊断**。核心是结构化观测(CLI + JSON snapshot)、指令级 conformance 测试、支持矩阵文档。没有可信度,"跑过了"不等于"正确"。

这两个支柱既独立又互相需要:真实性是前提,可信度是保证真实性的手段。下文的进度校准和重排都围绕它们展开。

## 实测进度校准(2026-06-19)

基线:`main` 分支 `402d726`,全量 `ctest` **217/217 通过**,E2E(hello / gpio_blink / systick / HAL UART)全部真实跑通。

| 里程碑 | 对应版本 | 实测完成度 | 关键证据 / 缺口 |
|--------|---------|-----------|----------------|
| CLI 入口 | 01 / v0.2.0 | **0%** | 无统一 `micro-forge run`,只有各 example 的 `runner.cpp`;src/ 无 CLI/argparse 模块 |
| JSON snapshot / introspection | 01 / v0.3.0 | **~15%** | `MmioAccess` 结构化 trace、`FaultRecord` 可复用;无 JSON 序列化、无统一 snapshot 聚合 |
| 异常语义(MSP/PSP/EXC_RETURN) | 02 / v0.4.0 | **~40%** | MRS/MSR 全系统寄存器已实现;xPSR、VTOR 联动、EXC_RETURN 识别齐;**缺 CONTROL.SPSEL 栈切换、缺 PSP 返回路径** |
| 中断屏蔽 | 02 / v0.4.0 | **~70%** | PRIMASK / FAULTMASK / BASEPRI 屏蔽齐全(`cortex_m3_interrupt.cpp:23-49`);**缺 AIRCR.PRIGROUP 优先级分组** |
| 中断抢占/嵌套 | 02 / v0.5.0 | **~20%** | `current_priority_` 有跟踪;**handler mode 直接 return 不抢占**(`cortex_m3_interrupt.cpp:20-22`)、无 tail-chaining、无 late-arrival |
| Fault 分流 | 02 / v0.4.0 | **~60%** | HardFault 升级、`FaultRecord` 保留原始原因、`try_escalate_fault`;**缺 configurable fault(MemManage/BusFault/UsageFault)细分** |
| bit-band 别名区 | 02 / v0.4.0 | **0%** | `bus.cpp` 无 0x22/0x42 别名转换 |
| 指令覆盖 | 02 / 持续 | **~85%** | phase6 第一关超额完成(CBZ/IT/CPSIE/DMB-DSB-ISB/MOVW-MOVT/BFI/SBFX/TBB/UDIV/MLA/SMULL/STM-LDM 等);**缺 CLZ、SVC、个别 MUL.W/MOV.W 变体**;SDIV 有已知问题、ADC/SBC 进位待修 |
| EXTI 外部中断 | 03 / v0.6.0 | **0%** | AFIO EXTICR 占位但无 EXTI 控制器 |
| USART RX / IRQ | 03 / v0.6.0 | **~30%** | TX polling 完整且 HAL 友好;**缺 RX 注入、RXNE/TXE 中断路径** |
| Timer IRQ 端到端 | 03 / v0.6.0 | **~50%** | UIF 产生、PSC/ARR/CNT 与 VirtualClock 联动;**UIF→NVIC→handler 端到端未在 E2E 验证** |
| RCC / FLASH | 03 / v0.6.0 | **~60%** | 轮询友好模型完整(就绪标志立即就绪、SR 永不忙);**缺 FLASH 真实 unlock/erase/program 流程** |
| SPI / DMA / I2C | 03 / v0.8.0 | **0%** | 仅 clock_domains 占位;无控制器实现 |
| GUI dashboard | 04 / v0.7.0 | **0%** | (依赖 JSON snapshot 先落地) |
| 扩展边界 | 05 / v0.9.0 | **~50%** | Machine/Bus/Device 已架构中立;**`CPU::set_nvic()` 等 Cortex-M 专用泄漏进通用接口** |

**整体向 v1.0 的进度估计:约 35–40%。** 但分布极不均匀,见下节。

## 进度错位诊断

00 的线性顺序是「v0.2 CLI → v0.3 snapshot → v0.4 异常 → v0.5 NVIC → v0.6 外设 → …」,即**产品化入口先于正确性,正确性先于外设**。

实测进度完全相反:

```
工程线(真实性的"能跑"部分):   ████████████████░░░  超前(指令 ~85%、HAL UART 跑通、外设轮询模型 ~60%)
产品线(可信度的"入口"部分):   ░░░░░░░░░░░░░░░░░░░  滞后(CLI 0%、snapshot 15%)
正确性深水区(抢占/嵌套/栈):   ████░░░░░░░░░░░░░░░  滞后(抢占 20%、MSP/PSP 切换 40%)
```

含义:模拟器已经"能跑 HAL UART",但**用不了**(没有入口)、**不可信**(无法结构化验证)、**在非平凡固件上会偷偷出错**(中断嵌套/RTOS 会行为错误,却不报 fault)。

这正是"看起来离 v1 很近(工程线超前),实际离'真实可靠'还有硬骨头(正确性深水区 + 入口空白)"的根本原因。

## 距离「真实可靠」的差距清单(按影响排序)

### 真实性债(影响"行为正确")

1. **中断抢占/嵌套未实现**——最深的真实性债。handler mode 下不进新中断,`current_priority_` 形同虚设。任何有中断嵌套的真实固件、或 FreeRTOS,会静默地按错误顺序执行。参照 [02 验收](02-cortex-m-correctness.md):低优先级 TIM2 IRQ 中到达高优先级 USART IRQ 应能抢占。
2. **MSP/PSP 双栈是空壳**——`msp_/psp_/control_` 字段齐、EXC_RETURN 认得 `0xFFFFFFFD`,但 `exception_entry_common` 把 exc_return 硬编码为 `0xFFFFFFF9`(`cortex_m3_interrupt.cpp:96`),Thread mode 用 PSP 的语义不存在。RTOS 移植必然失败。
3. **bit-band 缺失**——CMSIS `BITBAND_PERIPH` 和部分 HAL/用户代码会用,目前直接落到未映射区。
4. **PRIGROUP 优先级分组缺失**——AIRCR 有占位但 NVIC 不分组,抢占语义不完整。
5. **零散指令 + 已知正确性债**——CLZ、SVC、SDIV、ADC/SBC 进位。单条不致命,但累积影响可信度。

### 可信度债(影响"可验证")

1. **没有 CLI**——模拟器无法被直接使用,每次跑固件都要改 example runner。这是 00 里 v0.2.0 的头号主轴,完全空白。
2. **没有 JSON snapshot**——诊断只有人类文本(trace/dump),无法被 AI/GUI/自动化消费,无法做回归对比。
3. **指令正确性主要靠 E2E 黑盒**——缺针对 ARMv7-M 规范的指令级 conformance 测试,"跑过了"无法证明"每条都对"。优化级别回归(-O0/-O2/-Os)也未覆盖。

### 外设深度债(影响"能跑的固件范围")

1. 缺 EXTI(GPIO 中断模式必备)、DMA(UART/SPI 的 DMA 路径核心,且总线仲裁是模拟器难点)。
2. 现有外设是"轮询友好"浅模型——**中断路径(USART RX IRQ、Timer UIF→IRQ)未在 E2E 端到端验证**,这正是上面"抢占"债的下游表现。
3. 缺 SPI / I2C / ADC(I2C 状态机复杂,按 03 定为 v1 stretch)。

## 重排路线(推荐)

### 核心理由

00 假设从零线性推进,但实测工程线已超前、产品线与正确性深水区滞后。因此**不是沿 v0.2→v0.3→…→v1.0 线性走,而是先补两个最阻塞的地基,并让外设中断紧跟正确性做端到端验证**:

- 地基 A(抢占/栈正确性)解决最大的**真实性**债。
- 地基 B(CLI + snapshot)解决最大的**可信度/可用性**债。
- A、B **可并行**(A 改 CPU 内部、B 加外壳,耦合低),不必串行。
- 随后让外设中断路径紧跟 A,因为"中断在真实外设上端到端跑通"是 A 正确性的**最好验证手段**,也能顺手推进 03。

### 分波次

**第一波 · 地基(并行)**

| 工作项 | 目标 | 验收参照 |
|--------|------|---------|
| A. 中断抢占 + MSP/PSP + tail-chaining + PRIGROUP | 让"真实"成立 | [02 验收](02-cortex-m-correctness.md)(抢占、BASEPRI、PSP/SVC 返回) |
| B. `micro-forge run` CLI + JSON snapshot(5 区) | 让"可信/可用"成立 | [01 验收](01-cli-observability-ai.md)(run/dump-mem/--snapshot-json) |

**第二波 · 打通中断路径(验证 A,推进 03)**

- C1. EXTI + AFIO EXTICR(参照 [03 EXTI](03-stm32f1-hal-peripherals.md))。
- C2. Timer UIF→NVIC→handler 端到端 E2E(补齐现有 Timer 的最后一公里)。
- C3. USART RX 注入 + RXNE/TXE 中断(参照 [03 USART](03-stm32f1-hal-peripherals.md))。
- C4. bit-band(SRAM + Peripheral 别名,参照 [02 bit-band](02-cortex-m-correctness.md))。

**第三波 · 扩大固件覆盖**

- D1. DMA 最小通道模型(内存↔外设,含总线仲裁取舍)。
- D2. SPI1 polling + loopback(参照 [03 SPI](03-stm32f1-hal-peripherals.md))。
- D3. FLASH unlock/erase/program 流程(参照 [03 RCC/FLASH](03-stm32f1-hal-peripherals.md))。

**第四波 · 产品化 + 清债**

- E1. GUI dashboard(复用 B 的 snapshot,参照 [04](04-gui-debug-dashboard.md))。
- E2. 指令 conformance 测试 + 清 SDIV/ADC-SBC/CLZ/SVC 债。

**第五波 · 冻结**

- F1. API/文档冻结 + 通用层边界清理(`set_nvic` 等泄漏收口,参照 [05](05-extension-architecture.md))。
- F2. 支持矩阵文档(明确 v1 支持范围与非目标)。

### 与 00 原线性顺序的差异

| 00 原顺序 | 本重排 | 变化理由 |
|-----------|--------|---------|
| CLI → snapshot → 异常 → NVIC → 外设 | 抢占/栈 **与** CLI/snapshot **并行**起步 | 工程线已超前,异常正确性是当前最大债,不应排在入口之后串行等待 |
| 异常(v0.4)在 NVIC 抢占(v0.5)前 | 合并为第一波 A | MSP/PSP 栈切换与抢占强耦合,分开做会返工,合并可一次设计正确 |
| 外设(v0.6)在正确性后 | 外设**中断路径**紧贴抢占做端到端验证 | 中断路径是抢占正确性的最佳验证手段,且能顺势推进 03 |
| GUI(v0.7)在示例(v0.8)前 | GUI 放第四波,先清正确性债 | 00 自己也说"GUI 必须复用 snapshot",既然 snapshot 刚落地,GUI 不急于先于 DMA/SPI |

## 取舍与风险

- **抢占模型是第一波最大风险点**。每步轮询最高 pending 优先级 vs 维护优先级队列,性能与正确性的取舍需先定(参照 [.claude/phase6-hal-roadmap.md](../../.claude/phase6-hal-roadmap.md) 讨论点 3)。建议先按"每步扫描 + current_priority_ 比较"实现正确性,再按 profile 优化。
- **A/B 并行的协调成本**:A 会改 `check_and_handle_interrupt` / `exception_entry_common`,B 的 snapshot 又要读取 CPU 状态(含新加的 PSP/active_stack)。建议 B 的 snapshot 字段预留 active SP 来源标记,避免 A 落地后 snapshot 二次返工。
- **不为了赶外设广度牺牲正确性**。第三波的 DMA/SPI 若发现抢占模型仍有边角问题,应先回头修 A,而不是用外设测试绕过。这呼应 02 的风险:"MSP/PSP 和优先级若没有统一规则,优先级高于新增外设。"
- **可信度债与真实性债的优先级**:如果只能选一条线先做,**真实性(第一波 A)优先**——因为没有正确的中断模型,后续所有中断类外设验收都建立在错误地基上;但 CLI/snapshot(第一波 B)不应被无限推迟,因为它同时也是 A 的验证工具(用 snapshot 调试抢占场景)。

## 实施记录(2026-06-19)

**第一波 A 条「中断抢占 + MSP/PSP + PRIGROUP」已落地**([005 指导](../notes/005-interrupt-preemption-msp-psp.md) 已实施):

- 抢占判定 + 嵌套优先级栈(`active_priorities_`),`check_and_handle_interrupt` 不再在 handler mode 提前返回;`step()` 改用深度变化判定 entry。
- MSP/PSP 双栈:统一不变量「R13 == 当前活动 SP」,`push/pop` 走 `write_reg(13)` 同步影子;异常入口/返回按 EXC_RETURN 切栈(含 `0xFFFFFFFD` PSP 路径)。
- PRIGROUP 优先级分组:SCB AIRCR 写回调注入 CPU,`preempt_priority()` helper 统一抢占比较;SysTick 优先级经 SCB SHPR 查询。
- 阶段 2 优化:NVIC `highest_priority_pending_irq()` 改 lazy cache,状态变更失效、查询命中 O(1),行为与扫描版 bit 一致。
- 新增 5 个测试(抢占嵌套 / 同优先级不抢占 / BASEPRI / PSP / PRIGROUP),**全量 222/222 通过**,HAL UART E2E 无回归。

**02 仍剩**:tail-chaining(late-arrival 在同步模拟器天然退化,无需处理)。**bit-band 别名区已落地**(`src/memory/bus.cpp` 的 `bitband_read`/`bitband_write`,外设 `0x42xxxxxx` / SRAM `0x22xxxxxx` word 访问映射单 bit,read 返 0/1、write 原子 RMW),见第二波 C4。

## 实施记录补充(2026-06-19 · 第二波及之后)

继第一波 A/B 之后,本批次补齐真实固件跑通的「最后一公里」,并补上事件观测层:

- **Cortex-M3 Thumb-2 指令覆盖修复 + b.n 自循环**:修 9 处解码缺口,使真实 Keil/MDK-ARM STM32F103 HAL 固件端到端跑通。缺口:LDR.W `[PC,#imm12]` literal / BLX `Rm` 未设 LR / ADR 缺失 / SUB.W·SUBW plain imm12 / Load·Store immediate 忽略 hw1[7] 寻址模式 / shifted-reg CMP·CMN 误写 PC / data-proc register Rm 字段取错位 / b.n 自循环被 step 误判。验证路径:reset → `__main` scatter-load → `main` → `HAL_Init` → `SystemClock_Config` 切 PLL → `MX_GPIO_Init` → `HAL_GPIO_WritePin(PA1)` → `while(1)`,SysTick 中断正常。回归载体:`examples/F103/MDK-ARM/F103/F103.axf`(真实 Keil/MDK-ARM 固件)。
- **bit-band 别名区(第二波 C4)**:见上节,02 中 bit-band 债清零。
- **事件总线 / Hooks**:`include/hooks/`(`signal.hpp` / `ring_sink.hpp` / `event_bus.hpp` / `events.hpp`)。`Signal<E>` 类型化观察者、`RingSink<E>` 非阻塞 SPSC 环(O(1) push / 批量 drain)、`EventBus`,`GpioEdge`/`UartByte` 携带 CPU-cycle 时戳(`Stm32f1Gpio` 在 ODR 变化 emit GpioEdge,SoC 注入 CPU cycles 作时戳)。立场:单线程确定性,「不阻塞」靠 slot 轻量 + RingSink 离线 drain,不靠线程池。这补上了 02/04 观测层的可订阅事件源,GUI dashboard(04)可直接复用。
- **全量 244/244 通过**(第一波 A 落地后 222 → 本批次补测后 244)。

**02 仍剩**:仅 tail-chaining(同步模拟器天然退化,见上)。**03 外设中断路径**(EXTI / Timer UIF→IRQ E2E / USART RX-RXNE-TXE IRQ)进入第二波 C1–C3。

## 实施记录(2026-06-23 · 第二波 C2a/b)

继 Thumb-2 全覆盖里程碑收口(T0-T4 + T5c,notes 012/013)后,焦点转第二波外设中断端到端。C2 Timer 落地两件事:

- **打通 `raise_irq` 公共通道**:原 `CortexM3CPU::raise_irq` 是 no-op(`return {};`)—— **整个外设→NVIC 注入通道从未接通**(SysTick 靠独立 `sys_tick_irq()` 绕过)。实现为 `nvic_->set_pending(irq)` 一行;这是所有 MMIO IRQ(TIM/USART/EXTI)的公共入口。
- **Timer UIF → NVIC → handler 端到端**:Timer 仿 SysTick `set_irq_callback` 模式,`tick()` 在 UIF **0→1 edge + `DIER.UIE`** 时回调一次;SoC 接 `raise_irq(kTim2Irqn=28)`;`kTim2Irqn` 常量落 interrupt_config.hpp(vector index 44)。
- 验证:单元(edge/UIE 语义)+ `TimerUifRoundtrip` E2E(coordinator Apb1 驱动 tick → handler 往返)。**289/289 绿**,固件 E2E/CLI 无回归。细节见 notes 014。
- **C2 进度**:Timer UIF→IRQ E2E 由 ~50%(UIF 产生但无端到端)提升到通道打通 + 往返验证。剩 C2c 抢占/嵌套场景(检验第一波 A 的 `active_priorities_`),再 C1 EXTI / C3 USART RX-IRQ。


## 结论 / 下一步

- 当前站在 v0.1.0 的扎实基线上(217 测试全绿、HAL UART 真跑通),但**整体向 v1 约 35–40%**,且工程线与产品线/正确性线严重错位。
- 距离"真实可靠"的硬骨头是三块:**中断抢占/栈正确性、CLI+snapshot 入口、外设中断端到端路径**。
- 建议先确定第一波两条线(A 抢占/栈、B CLI/snapshot)是否并行,以及抢占模型的性能取舍。这两点定下后,再分别细化 02 / 01 的实现指导。

> 本文档是规划层判断,不含实现代码。选定方向后,按 [.claude/discussion-workflow.md](../../.claude/discussion-workflow.md) 输出对应方向的实现指导文件,由用户手工实现。
