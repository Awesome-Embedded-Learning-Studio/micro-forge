# 042 · ABC 深化批次——GUI 加肉 + 工程收尾

> 日期: 2026-07-08
> 分支: `feat/deepen-gui-and-core`(12 commit,本地未 push)
> 基线: main `8dee335`(v0.2.0 + GUI 第一批 PR#10,notes 040)
> 状态: 9 项 done + 3 follow-up,ctest 367 绿
> 后置: B2 DMA/SPI + 反汇编(用户倾向自己手写避幻觉)

## 背景

v0.2.0 发版 + GUI 第一批(数值调试器骨架:model/view/panels + dock + 调速,notes 040)合入 main 后,这一轮把骨架"加肉":深化 GUI 可视化 + 补工程债 + 扩 CLI。任务拆解见 `document/ai/HANDOFF.md` §A/B/C。

## 做了什么(9 项 + 3 follow-up)

**A 组 GUI 深化**
- **A1 芯片板级可视化**:`Stm32BoardWidget`(QPainter 自定义 widget,`gui/view/widgets/`)作 central,画 STM32F103 + 引脚 + LED。演进:初版固定 PA5/PA0/PC13(gpio_blink 用 PA5),用户实测 F103 示例翻 PA1 看不到 → 改 **PA0-PA7 一排 LED**,任何固件翻哪个 PA pin 都显示。LED 按 GPIO ODR bit 亮灭——模拟器知道每个引脚电平,相对真机调试器的独门卖点。
- **A2 输入控件**:SerialPanel 加 USART RX 输入框(回车→`inject_rx`)。GPIO toggle 按钮**后移除**——`simulate_input` 改 IDR(输入寄存器),但显示看 ODR + 演示固件不读 IDR,无可见效果(A2 设计失误)。`Session::simulate_gpio_input` 保留(语义没错,给读 IDR 的固件)。
- **A3 dock 布局持久化**:QSettings `saveState`/`restoreState` + `saveGeometry`。toolbar `setObjectName("main_toolbar")` 消 saveState 警告。
- **A4 probe mode CLI**:`micro-forge probe <fw>`——`enable_probe_mode` 后 missing opcode 被 skip + 记录(不 fault),跑完去重列 unique 未实现指令 + 计数 + first PC。真机做不到的卖点。

**B 组核心/外设**
- **B1 时钟树**:`ClockSnapshot{sysclk,hclk,apb1,apb2}` 加 IntrospectionSnapshot,`read_introspection` 经 `SimulationCoordinator→VirtualClock` 取三域频率;HCLK 暂报 SYSCLK(AHB prescaler 未建模)。`ClockPanel`(SYSCLK→HCLK→APB1/APB2 MHz 树)。时钟树是 STM32 最难讲的概念,教学利器。

**C 组工程收尾**
- **C2 set_nvic 架构债收口**:调研后判定裸指针正确(NVIC/SCB 与 CPU 同属 SoC 同生共死、无循环、异常路径用),不机械改 WeakPtr(那纯属 churn 无收益)。`set_nvic`/`set_scb` 加注释 + DIRECTIVES §A 加例外澄清 + notes 041。`bus_` 注释早已论证同 pattern。
- **C3 CI qemu**:`ci.yml` apt install `qemu-system-arm`,`oracle_cortex_m3` 一致性测试进 CI(之前只本机跑)。
- **C4-mem 内存视图**:`Session::read_memory(addr,len)` 经 `tools::memory_dump`;`MemoryPanel`(addr 输入 + hex dump,每 tick 刷)。反汇编留下(XL)。

**follow-up**(用户实测反馈)
- BoardView → `Stm32BoardWidget` 重构(挪到 `gui/view/widgets/`,用户要 widget 独立目录)+ LED PA0-7 通用化 + toolbar objectName。
- 移除 GPIO toggle 按钮(IDR vs ODR 设计失误)。
- `E2E.GpioBlink` 断言收紧 ≥6→≥25 + 打印实际 toggle 数(实测 40 翻/4M 步 = 10 万步/翻,1× 下 250ms/翻 ~4Hz)。

## 设计决策

- **单一事实来源**:所有面板/Widget 只读 `IntrospectionSnapshot`(`Session::snapshot()`),不碰 `soc_` 内部。Session Qt-free 可 gtest。
- **BoardView PA0-7 通用化**:固定 pin 映射不匹配多固件(gpio_blink PA5 vs F103 PA1)→ 画整排 PA0-7,任何 PA pin 可见。
- **GPIO 输入注入语义**:`simulate_input` 改 IDR(输入),不影响 ODR(输出)。LED/面板显示 ODR,固件要主动读 IDR 才看到注入;演示固件不读 → toggle 无效果 → 移除。
- **set_nvic 裸指针**:无循环(铁律目的是断环)+ 同生共死 + 异常路径。WeakPtr 徒增 control-block deref + IsValid。详见 notes 041。
- **DMA/SPI + 反汇编后置**:易幻觉的硬件对齐实现,用户倾向自己手写保真实。

## 陷阱

- **clangd Qt 头误报**:`QMainWindow`/`QWidget`/`QPainter` not found 等,是 clangd include path cache 问题,以 gcc 为准(HANDOFF §1 标注)。
- **offscreen 只验不崩**:`MICRO_FORGE_GUI_AUTORUN` + `QT_QPA_PLATFORM=offscreen` 验 GUI 启动 + run loop 不崩,但**不渲染像素**——LED 实际颜色/翻转要本地 WSLg 跑看。
- **GpioBlink 断言松**:原 `EXPECT_GE(6)` 远低于实际(40 翻/4M 步),藏回归。收紧 ≥25 + 打印实际值。
- **git mv + Write**:`git mv` 后新路径文件要先 Read 才能 Write(harness 要求),否则报 "File has not been read yet"。

## 验证

- ctest **367/367** 全绿(core + build-gui 双 build,从 359 → 367,+8 测试:Session inject/read_memory + introspection clock + CLI probe)。
- offscreen smoke:每 GUI commit 过(F103.axf / blink.elf 跑 2-3s 不崩)。
- 手动:`probe F103.axf` 报 full coverage;`dump-mem` / `run` 现有功能不回归。
- test floor 357→360(A2 input-injection tests)。

## 后置(排队,用户自己手写)

- **B2 DMA/SPI**:CubeF1 `DMA_FLASHToRAM`(M2M HAL)作设计参考;模拟器现无 SPI/DMA(从零建)。用户决定后置(幻觉风险)+ 倾向自己手写裸寄存器(避 HAL 构建复杂性)。详见 memory `complex-impl-self-written`。
- **反汇编器(C4 后半)**:XL,与 thumb-2 全覆盖同档(200+ 指令格式化 + objdump 当 oracle 对拍)。用户要求"记得迟早要做"。
