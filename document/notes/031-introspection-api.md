# 031 — 结构化 introspection API(GUI/CLI 单一事实来源,G1)

> 日期: 2026-06-30
> 阶段: v0.7.0 GUI dashboard 前置([milestone 04](../milestones/04-gui-debug-dashboard.md))
> 状态: ✅ ctest 323→327 双构建绿 + bench advisory PASS

## 背景

milestone 04(GUI dashboard)硬约束:GUI 只消费统一 introspection,绝不直接读 C++ 内部状态(否则 CLI/AI/GUI 三套事实来源)。但原 [snapshot.cpp](../../src/cli/snapshot.cpp) 只有 `write_snapshot_json`(JSON **文本**),GUI 不该每帧 parse JSON。需要结构化 introspection API 作单一事实来源。

实测发现 GUI 面板要展示的字段大面积缺失:CPU 的 7 个状态/掩码/栈寄存器(`xpsr_/primask_/basepri_/faultmask_/control_/msp_/psp_`)是私有成员、**无 public getter**;snapshot 的 fault 只序列化 5 字段(缺 opcode/bus_error/access_addr);peripherals 只有 usart_output。

GUI 技术栈定为 **Qt6 Widgets**(用户决策:AELS 技术栈对齐 + Windows 部署;详见 memory `gui_qt_decision`)。本批与 Qt 无关,是 CLI/GUI 共同的后端地基。

## 做了什么(G1 第一批)

1. **CortexM3CPU 加 7 个 const getter**(inline 类内):`xpsr()/primask()/basepri()/faultmask()/control()/msp()/psp()`。零成本,不进 sim 热路径(只被 read_introspection 调,每 UI tick 一次)。
2. **新建 `cli/introspection.hpp` + `src/cli/introspection.cpp`**:`read_introspection(Soc&, usart_output) noexcept` 返回 `IntrospectionSnapshot{CpuSnapshot, FaultSnapshot, PeripheralsSnapshot, cycles}`。纯只读,不改 sim 状态。
3. **snapshot.cpp 重构**:`write_snapshot_json` 改为基于 `read_introspection` 序列化。JSON 输出是**超集**(+ xpsr/primask/basepri/faultmask/control/msp/psp + fault 的 opcode16/opcode16_2/access_addr/bus_error),旧字段不变 → test_cli 断言全守住。
4. **test_introspection(4 测试)**:FreshSoc 默认值 / PureRead 不改状态 / UsartOutput 转发 / RunningFirmware(hello.elf run 后 cycles>0、pc 在 flash)。

## 设计要点

- **单一事实来源**:CLI(`write_snapshot_json`)和未来 GUI 都调 `read_introspection`,不各自读 cm3 → 字段不可能不一致。加字段只需在此一处填值。
- **库 vs 可执行**:`introspection.cpp` 进 `micro_forge` 静态库(test 可链接);`snapshot.cpp` 留 `micro-forge` 可执行,include 头调库符号。
- **零依赖守**:introspection.hpp 不依赖 `SnapshotExtras`/snapshot.hpp(参数用 `string_view`),不依赖 `memory/bus.hpp`(bus_error 用 raw uint + `has_*` flag)。诊断路径的 `value_or(0)` 与 snapshot.cpp 同风格(读不到显示 0,非热路径掩错)。
- **behavior-preserving**:ctest 双构建 327 绿 + oracle 对齐 + bench advisory PASS。唯一行为差异:cm3 无效时 JSON 现在输出默认 cpu 区(原省略)——死路径,SoC 不变式保证 cm3 总有效。

## 坑

- **类名 `Stm32f103Soc`**(stm32f103 全小写),凭记忆写成 `Stm32F103Soc`(大写 F)编译失败一次。同类教训见 `cortex-m3-underscore-path` 记忆:别凭记忆写标识符,以头文件为准。

## 验证

- ctest **327/327**(Debug + RelWithDebInfo 双构建;原 323 + 4 新 Introspection 测试)。
- bench `--baseline` advisory PASS(0 regression;gpio/uart/tim ratio 86–92% 是 WSL2 噪声,没碰热路径)。
- test_cli JSON 断言全过(SnapshotJsonHasCpuRegion / RunHello / UnmappedPcFaults)。

## 下一步

- **G2**:peripherals 摘要扩展(GPIO pin / SysTick / NVIC 从 `parts()` 读)+ EventBus 接线(GPIO edge / Uart byte → RingSink,目前**声明未接线**)。
- **G3**:GuiSession 控制封装(step/pause/reset API,把 CLI 的 100k-chunk run loop 抽成 GUI 可调)+ scb/nvic coverage 收口(29%/62%)。
- **G4**:Qt6 Widgets GUI MVP(QMainWindow+QDockWidget,CPU 面板+运行控制,GPIO blink 验收)。
