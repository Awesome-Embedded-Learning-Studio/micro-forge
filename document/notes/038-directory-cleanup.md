# 038 · 目录整理四批（GUI / CLI / 后缀 / stm32f1）

> 日期: 2026-07-07
> 范围: 物理目录与概念职责对齐；纯重组，零行为变化（唯一 API 变更是 introspection 命名空间 `cli::` → `introspection::`）。
> 提交: e0dcb9c / 4a7a7cc / d7c80b3 / c885854
> 验证: 每批 ctest 355/355 + GUI build；批 2 加 CLI SIGINT smoke。

## 背景

PROJECT-MAP 四层扫描发现一批"物理位置跟概念职责打架"的目录：消费者代码（GUI）坐进核心库 `src/`；库代码（introspection）坐在消费者目录（`src/cli/`）；`.h`/`.hpp` 后缀混用（50 个 `.hpp` vs 7 个 legacy `.h`）；`stm32f1/` 下 11 个外设器件与 SoC 组装文件平铺。功能都正常（CMake 编译边界已隔干净），但代码组织与 §E「GUI 是 consumer 不是 core」等自身定义不一致。

## 目标

让每层目录的物理位置跟它的概念职责对齐：核心库代码全在 `src/`，消费者在顶层各占一格，后缀统一，子目录按角色分。

## 设计（四批）

**批 1 · GUI 挪顶层 `gui/`**（e0dcb9c）。`src/gui/` 三个文件移到顶层 `gui/`，新建 `gui/CMakeLists.txt` 持有 Qt 接线（`find_package(Qt6)` + `AUTOMOC` + `target_include_directories PRIVATE ${CMAKE_SOURCE_DIR}`），根 CMakeLists 的内联 if 块换成 `add_subdirectory(gui)`。GUI 跟 `examples/`/`test/`/`bench/` 平级成为第四个顶层消费者目录。

**批 2 · CLI/introspection 拆分**（4a7a7cc）。`introspection.{hpp,cpp}` 是核心库代码（在 `MICRO_FORGE_SOURCES`、是 §E 单一事实来源、GUI 也靠它），却坐在 `cli/` 消费者目录。挪到独立库子模块 `src/introspection/` + `include/introspection/`，命名空间 `micro_forge::cli` → `micro_forge::introspection`。CLI 正身（`main`/`snapshot`）挪顶层 `cli/`，`snapshot.hpp` 成为跟 `.cpp` 同目录的内部头。`add_executable(micro-forge ...)` 留在根 CMakeLists（只改源路径），二进制仍是 `build/micro-forge`——README 的 `./build/micro-forge` 路径不断。

**批 3 · 后缀统一 + def.h 改名**（d7c80b3）。7 个 legacy `.h` 全改 `.hpp`：`weak_ptr` 系列 3 个、`toy` 2 个、两个 `def.h`。两个 `def.h` 顺手起说得清的名：`arch/arm/cortex_m3/def.h` → `cortex_m3_defs.hpp`（PSR 位标志 + 寄存器表）、`loader/utils/def.h` → `elf_defs.hpp`（ELF 魔数 + `Elf32_*` 结构）。零 `.h` 残留。

**批 4 · stm32f1 分 periph/soc**（c885854）。`src/chips/stm32f1/` 11 文件按角色分两层：`periph/`（7 个外设器件 `stm32f1_{afio,exti,flash,gpio,rcc,timer,usart}`）、`soc/`（5 个配置/组装 `{clock_domains,interrupt_config,memory_bus,peripheral_config,stm32f103_soc}`，其中 `clock_domains` header-only）。找一个器件 vs 一个组装逻辑不再 grep 平铺目录。`MICRO_FORGE_SOURCES` 12 条路径 + ~51 处 include 同步。

## 关键决策

- **GUI 不进 `extension/`**。`extension` 在 05 验收里有专用语义（新 CPU 架构/芯片适配，如 RISC-V/GD32），GUI 不是这类，塞进去会偷换概念。顶层 `gui/` 跟 `examples/test/bench` 对称才是正解。
- **introspection 命名空间跟着独立**。只挪文件不改命名空间会留半成品（文件在 `src/introspection/` 却叫 `micro_forge::cli`）。`cli::read_introspection` → `introspection::read_introspection`；snapshot 的 `cli::SnapshotExtras`/`write_snapshot_json` 留 `cli`（CLI 专属）。
- **toy 不删**。曾误判它"没测试"，核实 `test/test_cpu.cpp`（`ToyCpuTest`）注册且在 355 测试内、活着。它演示 05 扩展边界，结构不动，只顺带改后缀。
- **CLI 二进制路径不挪**。`add_executable` 留根 CMakeLists 而非挪 `cli/CMakeLists.txt`，避免 `build/micro-forge` → `build/cli/micro-forge` 断 README 6 处路径。

## 陷阱

- **`find ... -name x -o -name y` 优先级坑**（批 4 首次 sed）。`-o` 优先级低于隐含 AND，导致 `.hpp` 搜索范围异常、大片文件漏改。改用 `git ls-files '*.hpp' '*.cpp'` 取可靠文件列表。
- **相对路径 include sed 漏网**（批 3 两次）。`cortex_m3.hpp` 的 `#include "def.h"`、`weak_ptr_factory.hpp` 的 `#include "weak_ptr.h"` 是同目录相对引用，sed 模式带目录前缀匹配不上。教训：sed 前先 grep 全部 include 形式（带前缀 + 相对），分模式处理。
- **命名空间改动的连锁**（批 2）。`snapshot.cpp` 在 `cli` 命名空间内无限定调用 `read_introspection`/`IntrospectionSnapshot`，符号挪到 `introspection` 后报 `does not name a type`（连带触发三个匿名 helper 的 `-Werror=unused-function`）。修法：`const auto snap = introspection::read_introspection(...)`。

## 验证

每批独立 `cmake -B build` + `cmake --build build -j$(nproc)` + `ctest --test-dir build`（355/355）+ `cmake -B build-gui -DMICRO_FORGE_GUI=ON` 编 `micro-forge-gui`。批 2 额外 CLI smoke：`timeout -s INT 4 ./build/micro-forge run F103.axf --snapshot-json`，SIGINT 正常退出、stderr 出 `state=Running stop=Interrupted`、JSON 完整（cpu/pc/regs/xpsr/primask 全字段）——证 `read_introspection` 端到端、命名空间改动零行为影响。

## 后续

- **DIRECTIVES §B 子空间列表**该补 `introspection`（已本机更新，gitignored 不入库）。
- README 测试数/工具链描述仍脱节（属另一档「README 对齐」，见 PROJECT-MAP §4 第一档）。
- `set_nvic` 等架构债、probe mode 位置散落——未在本轮范围。
