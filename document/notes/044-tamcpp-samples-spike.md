# 044 — TAMCPP C++ HAL 样例 spike(线 A)

> 让 micro-forge 加载运行 TAMCPP(`~/Tutorial_AwesomeModernCPP/code/stm32f1-tutorials`)的 C++ HAL 样例(1_led/2_button/3_uart)。TAMCPP 是 C++(main.cpp + device/ + system/),micro-forge 之前只加载过 C 样例(hal_blink/hal_uart)。

## 已修 3 个模拟器 bug

1. **elf_loader .data LMA**(043):按 `p_vaddr` 加载 .data 到 SRAM,漏了 flash LMA 副本;裸机 startup `CopyDataInit` 从 `_sidata`(flash LMA)复制,读到 0 覆盖 SRAM → `SystemCoreClock=0` → SysTick 不配 → `HAL_Delay` 死循环。修:PT_LOAD 若 `p_paddr≠p_vaddr`,额外写副本到 `p_paddr`(flash LMA)。hal_blink 因用 busy-loop `delay_ms`(不碰 SysTick)从未暴露。
2. **cortex_m3 SysTick 线程优先级**(043):线程模式 `active_preempt` 用 0xFF,被 `preempt_priority`(0xF)截断,SysTick(0xF0)无法抢占线程 → `uwTick` 不涨。修:线程用 0xFF(不截断)。
3. **nvic IPR 字节写**(本批):`HAL_NVIC_SetPriority` 用 `strb` 写 `IP[IRQn]`,原 NVIC write 只支持 Word 返回 `Unaligned` → `DataAccessFault`。修:IPR 分支提前 + 按 width 更新字节,ISER/ICER 等仍 Word。TAMCPP 3_uart `enable_interrupt` 因此通。

## spike 状态

- **1_led_control 通**:PC13 toggle=1(A1 生死验证,C++ elf 能在模拟器跑)。
- **3_uart_logger TX 通**:`UART Logger Ready!`(20 字节,USART1 TX 链路与 hal_uart E2E 同)。**RX 收命令**:注入 `LED ON\r\n` → main 回 `ERR: unknown command\r\n`(RXNE 中断 + ring + `handle_command` 链路通;命令解析不匹配,疑 `\r` 处理细节,待查)。
- **2_button_control 卡**:`poll_events` 去抖状态机,PC13_ODR 不变(toggle=0)。CPU 读 GPIOA IDR 正确(idle=1/press=0/release=1),`uwTick` 涨,但 button 没触发 Pressed/Released 的 on/off。深层诊断中(疑 `on()` 写 RESET=0 但 LED 初始 ODR=0 → 0→0 不触发 edge;或 Released 去抖没过)。

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

- ctest 368/368 绿(nvic 修不破坏现有)。
- led spike:PC13 toggle=1。
- uart spike:TX `UART Logger Ready!` + RX 收命令(回 ERR)。
- button spike:卡(诊断中)。

## 下一步

- button `poll_events` 去抖深挖(可能第 4 bug;或 LED active-low ODR 初始值问题导致 on/off 不触发 edge)。
- uart 命令解析 `\r`(回 `OK: LED ON` 而非 ERR)。
- ctest E2E:tamcpp led/uart 进 ctest(参考 hal_uart E2E,test/CMakeLists.txt 加 `E2E_TAMCPP_*_ELF` 宏 + `test_e2e.cpp` #ifdef TEST)。
