# 015 — EXTI 外部中断 + AFIO EXTICR(C1)

> 06 第二波 C1。新增 EXTI 控制器(IMR/EMR/RTSR/FTSR/SWIER/PR),GPIO 边沿经 AFIO EXTICR 路由 → 触发 → raise NVIC(复用 C2 的 raise_irq 通道,EXTI 是其第二个消费者)。GPIO `simulate_input` 改为也 emit edge(外部输入边沿喂 EXTI)。ctest **296/296 绿**(289 + 7 新)。

## 背景

06 第二波 C1:GPIO 外部中断模式。AFIO EXTICR 寄存器已存但无消费者,缺 EXTI 控制器(06 实测 EXTI 0%)。复用 C2 打通的 raise_irq 通道 —— EXTI 作为第二个消费者,证明该通道通用(TIM/EXTI/USART 都走它)。

## 实现

1. **`Stm32f1Exti : Device`**(@0x40010400):6 寄存器 IMR/EMR/RTSR/FTSR/SWIER/PR(PR rc_w1,写 1 清)。`on_gpio_edge(GpioEdge)`:线=pin → EXTICR 路由校验 port → IMR 使能 + RTSR/FTSR 沿匹配 → set PR + raise(经 `exti_irq_for_line`)。
2. **AFIO `exti_line_port(line)`**([stm32f1_afio.hpp](include/chips/stm32f1/stm32f1_afio.hpp)):暴露 EXTICR 路由(线 N → 端口 0=PA,1=PB,…)。原 `exticr_[4]` 私有无 getter。
3. **GPIO `simulate_input` emit edge**([stm32f1_gpio.cpp](src/chips/stm32f1/stm32f1_gpio.cpp)):原只改 `idr_` 不 emit;EXTI 监听外部输入边沿,故现也 emit edge_signal(与 ODR 边沿同路径)。
4. **线→IRQ**(`exti_irq_for_line`,EXTI hpp):0-4=6-10,5-9=23(EXTI9_5),10-15=40(EXTI15_10)。
5. **SoC 接线**([stm32f103_soc.cpp](src/chips/stm32f1/stm32f103_soc.cpp)):`exti.set_afio(afio)` + `gpioa/b/c.edge_signal().connect(exti slot)` + `exti.set_irq_callback(raise)`。

## 验证(7 新单测)

- **单元**([test_stm32f1_periph.cpp](test/test_stm32f1_periph.cpp)):寄存器 R/W、PR w1c、上升沿触发 + IRQ 号、错端口不触发、IMR 屏蔽、FTSR-only 不响应上升沿。
- **E2E**([test_interrupt_roundtrip.cpp](test/test_interrupt_roundtrip.cpp) `ExtiGpioEdgeRoundtrip`):`simulate_input` PA2 上升沿 → EXTI → raise IRQ8 → handler 进入 → BX LR 返回。
- `ctest` 全量 **296/296 绿**,固件 E2E/CLI/中断抢占无回归。

## 陷阱

- **`MICRO_FORGE_SOURCES` 是显式 `set()` 列表**(CMakeLists.txt 行 25),**非 DIRECTIVES A 说的 `GLOB_RECURSE`**:新 `src/*.cpp` 须手动加该列表,否则 `vtable undefined` link error(reconfigure 也不会自动扫)。
- **IPR word 对齐**:EXTI 线优先级寄存器 `0xE000E400+N`(byte N);word 写须 4 对齐(IRQ8 在 0xE000E408 byte0;IRQ9 在 byte1 需 `<<8`)。测试选 IRQ8(word 对齐)避 unaligned。
- **simulate_input vs ODR edge**:原 `edge_signal` 只 ODR 变化 emit;EXTI 要外部输入,故补 `simulate_input` edge emit(输入边沿同路径喂 EXTI)。
- **EXTICR 路由**:EXTI 线 N 路由到 `EXTICR[N/4]` bits[(N%4)*4 : +4] 选的端口;错端口的 edge 不触发。
- **C2c 跳过**:抢占/嵌套验证边际价值低(Thread 被抢占已由 `TimerUifRoundtrip` 验证 + `active_priorities_` 已被 test_interrupt 5 测试覆盖),本里程碑不补。

## 成果

EXTI 外部中断端到端(GPIO 边沿 → EXTI → NVIC → handler),AFIO EXTICR 终于有消费者,raise_irq 通道第二个用户(证通用)。剩 C3 USART RX-IRQ(第三消费者,需先设计串口输入注入)。
