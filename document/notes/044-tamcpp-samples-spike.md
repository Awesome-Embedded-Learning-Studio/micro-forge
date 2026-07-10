# 044 — TAMCPP C++ HAL 样例 spike(线 A)

> 让 micro-forge 加载运行 TAMCPP(`~/Tutorial_AwesomeModernCPP/code/stm32f1-tutorials`)的 C++ HAL 样例(1_led/2_button/3_uart)。TAMCPP 是 C++(main.cpp + device/ + system/),micro-forge 之前只加载过 C 样例(hal_blink/hal_uart)。

## 已修 6 个模拟器 bug

1. **elf_loader .data LMA**(043):按 `p_vaddr` 加载 .data 到 SRAM,漏了 flash LMA 副本;裸机 startup `CopyDataInit` 从 `_sidata`(flash LMA)复制,读到 0 覆盖 SRAM → `SystemCoreClock=0` → SysTick 不配 → `HAL_Delay` 死循环。修:PT_LOAD 若 `p_paddr≠p_vaddr`,额外写副本到 `p_paddr`(flash LMA)。hal_blink 因用 busy-loop `delay_ms`(不碰 SysTick)从未暴露。
2. **cortex_m3 SysTick 线程优先级**(043):线程模式 `active_preempt` 用 0xFF,被 `preempt_priority`(0xF)截断,SysTick(0xF0)无法抢占线程 → `uwTick` 不涨。修:线程用 0xFF(不截断)。
3. **nvic IPR 字节写**(本批):`HAL_NVIC_SetPriority` 用 `strb` 写 `IP[IRQn]`,原 NVIC write 只支持 Word 返回 `Unaligned` → `DataAccessFault`。修:IPR 分支提前 + 按 width 更新字节,ISER/ICER 等仍 Word。TAMCPP 3_uart `enable_interrupt` 因此通。
4. **IT mask 解码**:`HAL_GPIO_ReadPin` 的 `ite ne` 被当成两个 THEN 槽,导致 `movne`/`moveq` 都执行、返回值恒为 0。修正 slot 对应的 mask bit 后,高低电平可正确返回 1/0。
5. **异常未保存 ITSTATE**:模拟器把 IT 条件保存在独立 `it_conditions_` 中,SysTick 若打断 `HAL_GPIO_ReadPin` 的 `ITE` 块,handler 会消费线程的条件,异常返回也不恢复。表现为 IDR 始终为 1 但 `ReadPin` 偶发返回 0,每次假按下都重置 release 去抖。修:异常 entry 按嵌套层级保存并清空 IT 状态,return 恢复;新增精确回归测试。
6. **t16_pop `pop {pc}` SP writeback 顺序**(本批):`POP {pc}` 弹出 EXC_RETURN 时,`write_pc` 先触发 `interrupt_return`(从 SP 弹 8 字异常帧并恢复 SP),但 t16_pop 随后用局部 `sp`(尚未 +4)writeback,**覆盖了 interrupt_return 刚恢复的 SP** → 每次 exception 净漏 0x20。3_uart_logger 的 USART1 RXNE 中断每收一字节 sp 偏 0x20 → `line_buf`(@sp+16)地址漂移 → `strb` 写错位置 → `handle_command` 的 `cmd=="LED ON"` 比较恒失败 → 回 `ERR`。修:`pop {pc}` 先 writeback `sp+4`、再 `write_pc`,让 interrupt_return 从正确位置弹帧。SysTick handler 不经 `pop {pc}`(走 `bx lr`),故此前一直平衡、未被本题暴露。

## spike 状态

- **1_led_control 通**:PC13 toggle=1(A1 生死验证,C++ elf 能在模拟器跑)。
- **2_button_control 通**:PA0 idle=1 → press=0 → release=1；Pressed 写 PC13 BSRR reset (`0x20000000`),Released 在 20ms 后写 set (`0x2000`),runner 验证 `on=1 off=1 ... [PASS]`。
- **3_uart_logger 通**:TX `UART Logger Ready!\r\n`(USART1 TX 链路与 hal_uart E2E 同);RX 注入 `LED ON\r\n` → RXNE 中断 → ring → main `handle_command` 回 `OK: LED ON\r\n`(全链路通)。

## firmware build 正规化

- `examples/tamcpp_blink/firmware/build_sample.sh`:参数化编 TAMCPP 1/2/3 C++ elf(借 micro-forge 的 STM32CubeF1 HAL —— TAMCPP `third_party/STM32F1` 是空目录,不改 TAMCPP)。gcc 编 .c、g++ 编 .cpp(`-std=c++23 -fno-exceptions -fno-rtti`)、g++ 链接(libstdc++ init)。
- `examples/tamcpp_blink/firmware/CMakeLists.txt`:参数化 `foreach(led/button/uart)`,`TAMCPP_ROOT` cache 变量(默认 `$ENV{TAMCPP_ROOT}` 或 `$HOME/Tutorial_AwesomeModernCPP/...`)+ STM32CubeF1 存在性门禁,绝不进 git。
- 链接脚本用 TAMCPP 的 `STM32F103C8TX_FLASH.ld`(带 `.init_array`,C++ 全局构造必需;不能用 micro-forge 的 `STM32F103_HAL.ld`——它无 `.init_array`)。

## 陷阱

- **TAMCPP third_party/STM32F1 空目录**:借 micro-forge 的 STM32CubeF1(不改 TAMCPP,跨仓库纪律)。
- **-std=c++23**:`uart_driver.hpp` 用 `std::expected`(C++23),build_sample.sh 必须加(1/2 不用 std::expected,c++17 够,但统一加无害)。
- **worktree STM32CubeF1 submodule 未 init**:hal_blink/hal_uart/E2E 被 skip(ctest 不破);tamcpp firmware 需 symlink 主仓的 STM32CubeF1。
- **C++ 启动**:startup `__libc_init_array` 调 `.init_array`(Meyers singleton `ClockConfig::instance()`);1_led 已验证通。

## 验证

- ctest 全绿,新增 `ExceptionPreservesInterruptedItBlock` 防止 ITSTATE 跨异常回归。
- led spike:PC13 toggle=1。
- button spike:`on=1 off=1 PA0=1 PC13=1 state=0 [PASS]`。
- uart spike:TX `UART Logger Ready!` + RX `OK: LED ON`(`TX banner=1 RX cmd=1`)。

## 状态(线 A 完成)

`E2E.Tamcpp{Led,Button,Uart}` 三 TEST 进 ctest(ctest 372 绿)。线 A 三样例全通,6 个模拟器 bug 修完:elf_loader `.data` LMA / SysTick 线程优先级 / nvic IPR 字节写 / IT mask 解码 / ITSTATE 异常保存 / `t16_pop` pop{pc} SP writeback。分支 `feat/run-tamcpp-samples`(本批 commit 本地未 push)。
