# armcc/AC6 固件语料 — 重生成指南 (T5c)

`test/firmware/armcc/*.ac6.axf` 是 STM32CubeF1 **STM32F103RB-Nucleo** 示例在
**Keil MDK + Arm Compiler 6 (armclang)** 下预编译的 ELF fixture,作为 ctest E2E
回归门禁。CI 无 Keil,故二进制提交进仓库;仅在需要刷新 codegen 时本地重编。

## 当前语料

| fixture | CubeF1 示例 | 覆盖 |
|---|---|---|
| `nucleo_f103rb_tim_timebase.ac6.axf` | TIM/TIM_TimeBase | TIM UIF / 中断 |
| `nucleo_f103rb_uart_printf.ac6.axf` | UART/UART_Printf | USART TX (printf 重定向) |
| `nucleo_f103rb_gpio_iotoggle.ac6.axf` | GPIO/GPIO_IOToggle | GPIO 翻转 |

每份均 `ELF32 / ARM / entry=0x80000ed`,与 Keil F103.axf 同款,`Stm32f103Soc::load_elf` 直接加载。

## 环境

- Keil MDK + AC6 (armclang) + **STM32F1xx_DFP 2.4.1** + CMSIS 6.x
- Pack 仓库自定义在 `D:\MDK-Pack`;UV4 在 `D:\MDK\UV4\UV4.exe`
- headless 构建:`UV4.exe -b <project.uvprojx> -o <log>`(退出码 0=干净 / 1=警告但成功出 .axf / 2+=错)

## 为什么不能在 WSL 文件系统上编

Keil 的编译器响应文件(`.__i`)在 WSL fs(9p,经 `\\wsl.localhost` 或映射盘符)上
**创建失败**。必须编在**本地 NTFS**。故把最小子树复制到 `D:\mf\STM32CubeF1\`
(保留 `..\..\..\..\..\Drivers` 的相对目录深度),在那里编。

## CubeF1 AC5→AC6 迁移补丁(每个 .uvprojx 必做 3 处)

CubeF1 示例工程是 AC5 配置;新版 MDK 只有 AC6,需三处补丁:

1. **选 AC6** — 在 `<Target>` 层(紧跟 `<ToolsetName>ARM-ADS</ToolsetName>` 之后)
   插入 `<uAC6>1</uAC6>`。(注意:必须在 `<Target>` 直接子级,不是 `<TargetCommonOption>` 内。)
2. **删 `--C99`** — `<VariousControls><MiscControls>--C99</MiscControls>` 清空
   (AC5 flag,AC6 不认;C99 由 AC6 默认提供)。
3. **删语言标准字段** — 删 `<v6Lang>`/`<v6LangP>`/`<v6Lto>`。
   否则 GUI 切 AC6 时 Keil 写入的值会强制 C90(`inline` 报错);删掉走 AC6 默认 gnu11。

GUI 等价操作:Options for Target → Target 页 → ARM Compiler 选 **AC6**;
C/C++ 页 → Language C 选 **gnu11**(勿选 C90);不要勾 AC5 的 C99 mode。

## 重编步骤

1. 复制最小子树到 `D:\mf\STM32CubeF1\`:
   `Drivers/{STM32F1xx_HAL_Driver, BSP, CMSIS}`(CMSIS 可去 DSP)+ 选定的 `Projects/STM32F103RB-Nucleo/Examples/<ex>`。
2. 对每个 `.uvprojx` 应用上述 3 补丁。
3. 跑 `D:\mf\build_corpus.bat`(遍历编 GPIO/TIM/UART)。
4. 把产出的 `MDK-ARM\STM32F103RB_Nucleo\STM32F103RB_Nucleo.axf` 拷成
   `test/firmware/armcc/nucleo_f103rb_<ex>.ac6.axf`。
5. 新增示例:在 `test/test_firmware_armcc.cpp` 加一个
   `TEST(FirmwareArmcc, <Name>BootsClean)`。

## 常见坑

- **WSL→Windows interop 偶发挂**(`exec format error`):挂了就在 Windows 原生跑 `.bat`(双击或 PowerShell)。`/mnt/d/mf` 从 WSL 仍可正常读写,故编完拷回不阻塞。
- **免费版(无 license)** 32KB 代码大小限制;Nucleo 小示例远低于此。
- 第三方 `third_party/STM32CubeF1` 的 `.uvprojx` 补丁**不入库**(vendored 子模块保持干净);本文件即权威配方。
