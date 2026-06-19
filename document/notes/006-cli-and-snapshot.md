# 006 - CLI 入口与 JSON snapshot(B 条 B1/B2)

> 日期: 2026-06-19
> 阶段: v0.2.0(对应 [01-cli-observability-ai](../milestones/01-cli-observability-ai.md))
> 状态: ✅ B1–B4 完成(CLI run + JSON snapshot 5 区 + MMIO trace + ctest 回归),226 全过。[01-cli-observability-ai](../milestones/01-cli-observability-ai.md) 验收达成

---

## 背景

之前只有各 example 独立的 `runner.cpp`,没有统一入口,诊断只有人类文本。[01-cli-observability-ai](../milestones/01-cli-observability-ai.md) 要求 `micro-forge run` CLI + JSON snapshot(5 区),让模拟器可被使用、可诊断、AI 可感知。这是产品化(「能用 + 社区反馈」)的关键一步。

## B1 · CLI run 入口

- 新建 `micro-forge` 可执行(`src/cli/main.cpp`),`run` 子命令。
- 自研参数解析(零依赖):`--chip`(仅 stm32f103)/ `<firmware.{elf,bin}>` 位置参数 / `--base`(BIN)/ `--max-steps` / `--trace-mmio` / `--snapshot-json`。
- ELF/BIN 自动识别(ELF magic `0x7F 'ELF'`)。
- **stdout = 固件输出(USART),stderr = 诊断**(状态/fault 摘要)——stdout 可被管道/测试直接断言。
- 退出码:fault/StepError → 1;否则 0;参数错误 → 2。

设计决策:CLI 解析自研、不引 CLI 库(项目零外部依赖倾向)。

## B2 · JSON snapshot

- `src/cli/snapshot.cpp` + `include/cli/snapshot.hpp`,手写序列化(零依赖,不引 nlohmann)。
- 5 区:`cpu`(state/mode/pc/lr/sp/regs R0-R12)/ `fault`(null 或 kind/pc/lr/sp/is_32bit)/ `run`(cycles)/ `peripherals`(B3)/ `events`(B3)。
- 地址/值用小写十六进制字符串,数字(cycles)十进制。
- `--snapshot-json FILE` 写文件。

陷阱:iostream `std::hex` 是 sticky——`hex_kv` 设 hex 后,寄存器编号 `r`(int)输出被污染(r10→"ra")。修法:编号前显式 `std::dec`。

## 附带修正:load_bin reset+launch

SoC `load_bin` 原本只加载不 reset/launch(与 `load_elf` 不一致),BIN 固件不会跑。改为同样 reset+launch。全量 222 测试无回归。

## 验证

- `micro-forge run hello.elf --max-steps N` → stdout `Hello from micro-forge!`,state=Running。
- `micro-forge run hal_uart.elf` → stdout `Hello from STM32 HAL UART`。
- `--snapshot-json` → 有效 JSON(`python json.load` 解析通过),regs r0–r12、fault 区正确。
- 故意 unmapped PC(0x10000000)→ state=Faulted,fault 区 kind=InstructionFetchFault/pc/lr/sp。
- `ctest` 全量 222/222 通过(load_bin 改动无回归)。

## B3 · MMIO events ring + peripherals

- CLI 持有 `std::vector<MmioAccess>` ring(容量 256,满则丢最旧=「最近 N 条」)。
- `--snapshot-json` 或 `--trace-mmio` 时 `tools::enable_mmio_trace` 收集;snapshot 的 `events` 区输出 ring,`peripherals.usart_output` 输出固件 USART 缓存(经 JSON 转义)。
- `--trace-mmio` 额外把 ring 按人类可读格式(`[RD/WR] addr=val (W) OK dev`)打到 stderr。
- 验证:hello 短步捕到 USART 写;hal_uart 初始化阶段捕到 RCC + USART。ring 是「最近 N」,固件进 loop 后被 fetch 占满属正常语义。

## B4 · CLI 回归测试

- `test/test_cli.cpp` 用 `popen` 驱动 `micro-forge`,断言:run 输出含 "Hello"、无参 usage exit 2、snapshot 含 cpu/regs 且 r10≠ra(hex-sticky 回归守卫)、unmapped PC → Faulted + InstructionFetchFault。
- 挂在 `test/CMakeLists.txt`,依赖 `micro-forge` + `hello_firmware`。ctest 全量 226/226。

## 下一步

B 条(01-cli-observability-ai)完整完成。候选:发版 v0.2.0(CLI+snapshot 是个发版点)、C 条外设中断(EXTI/Timer IRQ/USART RX)、或 GUI dashboard(复用 snapshot)。
