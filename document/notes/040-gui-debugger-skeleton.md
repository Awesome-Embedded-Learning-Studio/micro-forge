# 040 · GUI 第一批——数值调试器骨架（model/view/panels 分层 + dock + 调速）

> 日期: 2026-07-07 ~ 07-08
> 分支: `feat/start_gui_panel`（已合入 main `8dee335`，PR #10）
> 提交: efa7471 / f81ab7d / d61a808 / fc23f6c / 7b3ba2f / 438df09 / a1d713c / e0f0ef1 / 124f090
> 状态: 已合入 main，ctest 359 全绿，CI 双绿
> 验证: ctest 359 + offscreen smoke（`MICRO_FORGE_GUI_AUTORUN=1` + `QT_QPA_PLATFORM=offscreen`）

## 背景

GUI 在 notes 034–036 立了骨架（Qt 选型 + CPU/GPIO 面板），但那时 MainWindow 直接持有 SoC、面板跟 sim 内部耦合、布局是 central widget 硬拼——能跑，但不可测、不可扩展。这一批把它重构成「数值调试器骨架」:model/view/panels 三层分离 + QMainWindow dock 框架 + 调速选择器 + 持续 blink 演示 + Session 单测。目标是为后续 A 组深化（芯片可视化 / 输入控件 / 时钟树面板）铺好地基——骨架不稳就往上堆肉会塌。

## 设计决策

**1. Qt-free model::Session —— 可测试的模型层（efa7471）。** 把 SoC 持有、run/step、固件加载、UART 累积从 MainWindow 抽出，做成纯 C++ 的 `gui::model::Session`。不依赖 QWidget/QTimer/QApplication，所以能直接 gtest（`test/test_gui_session.cpp`），不必拉起 Qt。`rebuild()` 返回 `std::expected<void, std::string>` 上报创建/读取/加载错误；`snapshot()` 在 invalid 时返回 default 构造（state=Halted / 全零），让 view 永远拿到合法结构。**这是这批的核心决策**——model 与 view 解耦，sim 逻辑进了测试网，不再是「只有跑起 GUI 才能验证」。

**2. 单一事实来源 —— 面板只读 IntrospectionSnapshot（DIRECTIVES §E 铁律）。** 所有面板暴露 `refresh(const IntrospectionSnapshot&)`，从 `Session::snapshot()` 取数，绝不直接碰 `soc_` 内部。MainWindow 自己不持有任何 sim 状态（全在 Session）。这保证 view 永远走 introspection 干线，不绕过、不同步漂移。面板不搞抽象基类——每个是独立 QWidget，约定一个 refresh 方法就够，避免过早抽象。

**3. QMainWindow dock 框架（d61a808 / fc23f6c）。** 从 central widget 硬拼改成 QMainWindow + 6 个可拖拽 dock:serial 居中（主视觉）、registers 左、status/fault/peripherals 右、GPIO 下。用户能拖动/折叠/浮动任意面板。每个 dock 都 `setObjectName`（`regs_dock` / `status_dock` / …），为后续 A3 布局持久化（QSettings saveState）埋好钩子。

**4. 调速选择器（7b3ba2f）。** toolbar QComboBox `1×/5×/25×/100×`，`onTick` 按当前档位的步数跑步。固件用软件延时（gpio_blink 那种循环死等）时，低档几乎看不到进展，必须高档（100× = 每 tick 100 步）才能肉眼看见 LED/串口动。这也是模拟器相对真机调试器的便利——可任意调时间尺度，真机做不到。

**5. 持续 blink 演示（e0f0ef1 / 124f090）。** examples/gpio_blink 的 runner 改成无限循环 + 每次翻转打印 PA5 状态，让 GUI 启动后有持续的视觉输出（不是翻一下就停）。配合调速，演示效果直接，也是这批 GUI 能「看着像那么回事」的关键。

## 单线程模型

按 DIRECTIVES §E，sim 保持单线程——不做 Qt worker 线程跟 sim 线程的同步复杂度。view 的 QTimer 在 Qt 主线程里驱动 `Session::run()/step()`，每 tick 跑 N 步（N = 调速档位）然后 refresh 所有面板。简单、无锁、够用。`MICRO_FORGE_GUI_AUTORUN=1` 是 offscreen 测试钩子，让 GUI headless 启动就进 run loop，CI 上验证不崩。

## 验证

- ctest 359/359 全绿（含新增 `test_gui_session` 覆盖 rebuild/run/snapshot/USART 累积）。
- test floor 在 a1d713c bump 到 357，后续又到 359。
- offscreen smoke:GUI 无显示器启动 + 跑 run loop 不崩，每个 GUI commit 过这个门。

## 后续

这一批立了骨架，剩下都是「往骨架上加肉」——详见 `document/ai/HANDOFF.md`:
- **A 组**:A1 芯片板级可视化（主菜，模拟器独门卖点）、A2 输入控件、A3 dock 持久化、A4 probe mode。
- **B 组**:B1 时钟树 introspection+面板、B2 DMA/SPI（用 CubeF1 官方示例）。
- **C 组**:C2 set_nvic 架构债、C3 CI QEMU oracle、外加内存视图（hex dump 面板）。
- **反汇编器（C4 后半）后置**到下个里程碑——已确认迟早要做，工作量与当初 thumb-2 全覆盖战役同档（200+ 指令格式化 + objdump 当 oracle 对拍）。
