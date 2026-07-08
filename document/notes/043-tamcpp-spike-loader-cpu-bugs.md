# 043 · 线 A A1 点灯 spike——C++ HAL 样例跑通 + 两处 loader/CPU bug

> 日期: 2026-07-08
> 分支: `feat/run-tamcpp-samples`(worktree,从 `feat/deepen-gui-and-core` 97c90d3 分)
> 基线: ctest 367 绿
> 状态: A1 spike 通(PC13 toggle=1>0),修 elf_loader + cortex_m3 两 bug,ctest 368 绿
> 后置: A2 串口(3_uart_logger TX→EventBus) / A3 样例卫生 / A4 跨仓库文档

## 背景

线 A 任务:让 micro-forge 加载并运行 `~/Tutorial_AwesomeModernCPP` 的 C++ HAL 样例,读者不买板子也能跑教程。A1 点灯 spike 是生死验证——确认 TAMCPP `1_led_control` 的 C++ elf 能在模拟器跑(GPIO PC13 toggle>0)。

最大未知:C++ 程序 main 前要 `__libc_init_array` 初始化全局对象,且 `HAL_Delay` 依赖 SysTick 中断递增 `uwTick`。micro-forge 的 elf_loader 之前只加载过 C 样例,这两条链路没验证过。TAMCPP `third_party/STM32F1` 是空目录,样例编译借 micro-forge 自带的 STM32CubeF1 HAL(不改 TAMCPP,跨仓库纪律)。

## 卡点 1:elf_loader .data 段漏 flash LMA 副本

**现象**:spike 跑出 toggle=0,PC 卡在 `HAL_Delay` 的 `while(HAL_GetTick()-start<wait)` 死循环,SysTick `ctrl=0/load=0/val=0`(从未被配置)。

**诊断**(实测,非猜):

| 时刻 | SystemCoreClock@SRAM | flash LMA@0x08000bc4 |
|---|---|---|
| elf 加载后、run 前 | 0x7A1200(8M,初值正确) | 0x0(副本缺失) |
| run 后 | 0x3D09000(64M,clock config 改的) | 0x0 |

elf_loader 按 `p_vaddr` 加载 .data(初值到 SRAM=8M ✅),但 flash LMA(`p_paddr`=0x08000bc4)没加载。裸机 startup 的 `CopyDataInit` 从 `_sidata`(=p_paddr)复制 .data 到 SRAM——读到 0,把 SRAM 初值覆盖成 0。SystemCoreClock=0 → `HAL_InitTick` 的 `udiv 0/1000=0` → `SYSTICK_Config(0)` 走 ERROR(`ticks-1=0xFFFFFFFF` 越界)不写 SysTick → HAL_Delay 死循环。

Reset_Handler 反汇编铁证:`CopyDataInit` 从 `r2=0x08000bc4`(_sidata)读 → 写 `r0=0x20000000`(_sdata),循环 3 次(12 字节 .data)。

**为什么 hal_blink E2E 没暴露**:它用 busy-loop `delay_ms`(`volatile count--`),不碰 SysTick/uwTick。C++ HAL 样例用 `HAL_Delay` 才首次踩中——正说明 A1 spike 的价值。

**修法(B 法,按 p_paddr 加载)**:PT_LOAD 段加载到 `p_paddr`(当 `p_paddr!=0` 且 `!=p_vaddr` 时),否则 `p_vaddr`。初值落 flash LMA,SRAM 留给 CopyDataInit 复制——与真实烧录器行为一致(JTAG/SWD 把 .data 烧到 flash LMA,上电 startup 复制)。.text 段 `p_vaddr==p_paddr` 无变化。

**为何不双加载(A 法)**:A 法把 .data 预填 SRAM(方便但不真实——真实 SRAM 上电是 0),且会掩盖"startup 必须调 CopyDataInit"的真实要求(用户写忘 CopyDataInit 的 startup,真硬件坏但模拟器能跑=模拟器撒谎)。B 法让模拟器行为=真硬件,对教育用途(读者学嵌入式)重要。前提已验证:hal_blink(startup.c)、TAMCPP(startup.s)都调 CopyDataInit。

## 卡点 2:SysTick 线程优先级被截断

elf_loader 修好后,SystemCoreClock 经 CopyDataInit 正确复制(8M→HAL_Init 配 SysTick→clock config 改 64M,SysTick `ctrl=0x10007 load=63999` 配上了),`uwTick` 仍=0。

**诊断**:SysTick 在 tick(COUNTFLAG=1,实际到 0 了 312 次),irq_cb 调了 312 次,`pending_sys_tick_` 设了 312 次,但 SysTick_Handler 没进(uwTick=0)。

`check_and_handle_interrupt` 的优先级比较:`active_preempt = preempt_priority(0xFF)`(线程模式),4 位截断成 0xF;SysTick 优先级 0xF0(HAL 设 TickPriority=15)→ `preempt_priority(0xF0)=0xF`。`sp(0xF) < active_preempt(0xF)` 为 **false** → 不抢占。

**违反 ARMv7-M**:线程模式优先级最低,应被**任何** exception 抢占(即使最低优先级)。`preempt_priority(0xFF)` 截成 0xF 后与最低 exception 相等,挡住 SysTick。与 prigroup 无关(0xFF 和 0xF0 高位相同)。

**修法**:线程模式 `active_preempt` 用 0xFF(不截断),任何 exception preempt(0..0xF)均 < 0xFF → 抢占。handler 模式 nesting 不变(`preempt_priority(current_priority_)`),NVIC E2E 不破坏(更高优先级中断仍 < 0xFF)。

**为何 Timer/USART/EXTI 没踩中**:它们优先级更高(<0xF0),preempt < 0xF,现状下 `sp < 0xF` 成立能抢占。SysTick 默认最低优先级 0xF0 才踩中——`examples/systick/runner` 不进 ctest(只 add_executable 无 add_test),所以这条链路从未被 ctest 验证。

## 验证

- `test_elf_loader`:加 `DataSegmentLoadedToFlashLmaWhenVaddrDiffers`(.data 初值落 flash LMA,SRAM 保持 0)。全量 ctest **368/368 绿**(含新测试 + NVIC E2E 不破坏)。
- A1 spike:重跑 `examples/tamcpp_blink/runner`,`GPIO PC13 toggled 1 times`(toggle=1>0)。uwTick=0x4E3=1259(>1000,第一个 HAL_Delay(500) 返回 + led.on() + 第二个 HAL_Delay(500) + led.off())。

## 陷阱小结

- **busy-loop 掩盖 bug**:hal_blink 用 `delay_ms` 不碰 SysTick,掩盖了 .data LMA + SysTick 优先级两个 bug。spike 必须用真实 HAL_Delay 链路才暴露。
- **COUNTFLAG 不代表次数**:Ctrl bit16 COUNTFLAG 置位不清(读 VAL/CTRL 才清),只说明"到过 0",不说明次数。诊断时一度误以为只到 0 一次,实际 312 次。
- **TAMCPP third_party 空**:`~/Tutorial_AwesomeModernCPP/third_party/STM32F1` 是空目录,样例编译借 micro-forge 的 STM32CubeF1(A0 用 `/tmp/tamcpp-build/build.sh` 手动编出 elf)。
- **worktree submodule**:git worktree 的 `git submodule update --init` 报 "Unable to find current revision"(worktree gitdir 链接问题),symlink 主仓 STM32CubeF1 临时跑 ctest(commit 不含 symlink,只 add 具体文件)。

## 后置

- **A2 串口**:加载 TAMCPP `3_uart_logger`,TX 经 EventBus(UartByte)→ GUI serial 面板可见;进 ctest E2E(参考 `test/CMakeLists.txt:175` hal_uart 模式)。tamcpp_blink 的 firmware build 正规化(借 STM32CubeF1 + TAMCPP 源码路径)。
- **A3 样例卫生**:`examples/hal_blink` `examples/hal_uart` 的 `CUBE_BASE` 参数化 + README + 重编验证仍通。
- **A4 跨仓库文档**:TAMCPP `documents/vol8-domains/embedded/` 补"用 micro-forge 无硬件跑教程"(跨仓库改动先报用户确认)。
