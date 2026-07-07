# 035 — GUI CPU 面板 + run/pause(G5b)

> 日期: 2026-06-30
> 阶段: v0.7.0 milestone 04([GUI dashboard](../milestones/04-gui-debug-dashboard.md))
> 状态: ✅ run loop 端到端 offscreen 验证
> 前置: G5a(notes 034)

## 做了什么(G5b)

`MainWindow` 现在拥有一个 `Stm32f103Soc` 并从 Qt 主线程驱动它:

- **`QTimer`(50ms / ~20Hz)** → `onTick()` → `SoC::run(20000 步)` → `cli::read_introspection()` → 刷新 CPU 寄存器表。单线程:sim 在 QTimer 回调里同步跑,**绝不 QThread**(DIRECTIVES §E)。
- **控制**:Run/Pause 切换 timer;Step 跑 1 步;Reset 重建 SoC + 重 load firmware。
- **CPU 面板**:16 行表(r0-r12 / sp / lr / pc,hex)+ 状态条(`state | mode | cycles | FAULT`)。
- **firmware 加载**:`argv[1]`(elf 或 bin,与 CLI 同判定);USART 输出经 `SerialPort::set_output` 收集(与 CLI 同)。
- **`MICRO_FORGE_GUI_AUTORUN` env 钩子**:offscreen/CI 启动时自动开 run loop,让无头验证能跑 QTimer→run→refresh 全链路。交互启动(未设 env)不受影响。

## 设计要点

- **CMake**:GUI target 加 `target_include_directories(micro-forge-gui PRIVATE src/)`——`main_window.hpp` 放 `src/gui/`(GUI 内部头,非公开 API),PRIVATE 不污染核心。
- **AUTOMOC**:`main_window.hpp` 有 `Q_OBJECT`,CMake 自动生成 moc(G5a 已 `set(CMAKE_AUTOMOC ON)`)。
- **run loop 自动验证**:`QT_QPA_PLATFORM=offscreen MICRO_FORGE_GUI_AUTORUN=1 micro-forge-gui hello.elf` 跑 3s,exit=124(=run loop 端到端没 crash)。寄存器是否真推进靠 SoC::run(G1-G3 已 test)+ introspection(G1-G3 已 test)的组合保证;最终视觉确认在 G5c(blink 翻转)。

## IDE diagnostics 说明

IDE 用 `build/`(GUI OFF)的 compile_commands,不知道 `micro-forge-gui` target,所以对 `src/gui/*.cpp` 报一堆 false-positive(`gui/main_window.hpp not found` / `QString unknown`)。实际编译走 `build-gui`——`[100%] Built target micro-forge-gui` + offscreen 启动证明代码正确。后续若要 IDE 高亮正确,生成 `build-gui/compile_commands.json` 并指向它。

## 验证

- `cmake --build build-gui`:`micro-forge-gui_autogen` + `mocs_compilation.cpp` + main_window.cpp + main.cpp 全编译链接成功。
- offscreen + AUTORUN + hello.elf:run loop 跑 3s,exit=124(不 crash)。

## 下一步

- **G5c**:GPIO 面板(显示 A/B/C 端口 ODR 16-bit,bit 翻转可视化)+ blink.elf 视觉验收(用户看 PA ODR bit 闪)。这是 milestone 04 的临门一脚。
