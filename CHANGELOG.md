# Changelog

All notable changes to micro-forge are documented here.
The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [0.2.0] - 2026-07-07

### Added
- **CLI runner** — `micro-forge run` with `--snapshot-json` (5-region JSON state
  export: CPU / fault / run / peripherals / events), `--trace-mmio`, `--max-steps`,
  and `--base` for raw binaries.
- **`micro-forge dump-mem`** subcommand — loads firmware and dumps a memory window.
- **Structured introspection API** (`read_introspection`) — the single source of
  truth consumed by both the CLI JSON serializer and the GUI dashboard.
- **Cortex-M3 correctness foundation** — interrupt preemption and nesting,
  MSP/PSP dual-stack, PRIGROUP priority grouping, NVIC lazy priority cache,
  bit-band alias regions (SRAM `0x22……` + peripheral `0x42……`).
- **Full Thumb-2 instruction coverage** for the ARMv7-M base profile; real
  Keil/MDK-ARM STM32F103 HAL firmware boots end-to-end.
- **Three end-to-end interrupt paths** — Timer UIF, EXTI GPIO edge, USART RXNE
  — all wired through a common `raise_irq` channel.
- **Event-bus hooks subsystem** — typed `Signal<E>`, non-blocking `RingSink<E>`,
  `EventBus`, emitting cycle-timestamped `GpioEdge` / `UartByte` events.
- **Qt6 GUI dashboard** (opt-in, `-DMICRO_FORGE_GUI=ON`) — step / run / reset,
  visual CPU registers and GPIO output.
- **Coverage pipeline** (`-DMICRO_FORGE_COVERAGE=ON` + `gcovr`) — lines 81.6% /
  branches 61.4% / functions 90.2%.
- **QEMU differential oracle** (`scripts/qemu_cortex_m3_oracle.sh`) — checks
  SDIV/UDIV/ADC/SBC + xPSR semantics against real QEMU.
- **Test-count floor gate** (`scripts/check_test_count.sh`) — pins the baseline
  so refactors can't silently drop tests.
- **Install rules** — `cmake --install` deploys the library, CLI, and headers.
- Firmware corpus: 12 AC6 `.axf` images (3 examples × `-O0/-O2/-Oz`) for
  optimization-level regression.

### Changed
- **Project layout cleanup** — GUI and CLI moved to top-level consumer
  directories (`gui/`, `cli/`); introspection split into its own library module
  (`src/introspection/` + `include/introspection/`, namespace
  `micro_forge::introspection`); `src/chips/stm32f1/` split into `periph/`
  (devices) and `soc/` (assembly); all headers unified to `.hpp`; the two
  generic `def.h` renamed to `cortex_m3_defs.hpp` and `elf_defs.hpp`.
- Test count 217 → 353 product tests (+ 2 meta-guards).

### Fixed
- Thumb-2 decoding gaps that blocked real HAL firmware (`LDR.W` PC-literal,
  `BLX Rm` not setting LR, `ADR.W`, `SUB.W/SUBW` plain imm12, load/store
  `hw1[7]` addressing, shifted-reg `CMP/CMN` clobbering PC, `B.N` self-loop).
- `SDIV/0` returns 0 (initial implementation returned INT_MIN; corrected against
  the QEMU oracle); `ADC/SBC` C/V carry flags; 32-bit `ADC.W`/`SBC.W`
  shifted-register operands; INT_MIN / -1 saturation guards.

## [0.1.0] - 2026-05-20
- Initial release: Cortex-M3 Thumb/Thumb-2 decoder, FlatMemory + Bus + Region
  address routing, NVIC / SysTick / SCB, RCC / GPIO (A/B/C) / USART1 / TIM2 /
  AFIO / FLASH, ELF + BIN loaders, 217 GoogleTest cases + 4 end-to-end
  examples (hello / gpio_blink / systick / HAL UART).
