# 008 — armcc/AC6 固件语料 E2E (T5c)

> 2026-06-21。把真实固件语料从「1 份 Keil F103.axf」(见 [007](007-cortex-m3-f103-keil-firmware.md))扩到「3 份 CubeF1 Nucleo 示例」,作为 ctest E2E 回归门禁,补 armcc codegen 多样性。

## 背景

007 证明 Keil/MDK 固件能跑、且一次就暴露 9 处 Cortex-M3 缺口。但只有 1 份 armcc 样本(用户自写 CubeMX 工程)。要验证「任意真实固件」,需要一批 armcc 编译的标准示例。

## 目标

- 用 Keil MDK headless 批量编 CubeF1 **STM32F103RB-Nucleo** 小示例(GPIO/TIM/UART)。
- 产出的 `.axf` 作 ctest E2E fixture,**一次编译、提交进仓**,之后改动指令自动回归(CI 无 Keil)。

## 关键决策

1. **走 Keil headless(`UV4 -b`),不重生成工程**。CubeF1 自带 `.uvprojx`,直接编。
2. **复制最小子树到本地 NTFS(`D:\mf\`),不在 WSL 文件系统编**(见陷阱 3)。
3. **二进制 fixture 提交**,配方进 `test/firmware/armcc/REGENERATE.md`;vendored 子模块补丁不入库。

## 陷阱(本次核心价值,全是不靠运气撞出来的)

1. **AC5→AC6 编译器墙**:CubeF1 示例工程是 **AC5(armcc)** 配置,新版 MDK 只剩 **AC6(armclang)**(AC5 已退役)。`UV4 -b` 报 `uses ARM-Compiler 'Default Compiler Version 5' which is not available`,直接 abort。
2. **`<uAC6>` 的正确位置是 `<Target>` 直接子级**(紧跟 `<ToolsetName>`),**不是** `<TargetCommonOption>` 内。手插错位置会被静默忽略——这步靠 Keil GUI 切一次 AC6 才学到正确写法。
3. **WSL 文件系统(9p)建不了 Keil 的 `.__i` 响应文件**。经 `\\wsl.localhost` 或映射盘符(`net use Z:`)都不行(同一 9p 后端)。**必须编在本地 NTFS**——把 Drivers + 选定 Examples 复制到 `D:\mf\STM32CubeF1\`,保留 `..\..\..\..\..\Drivers` 相对深度。
4. **`--C99` 是 AC5 flag,AC6 不认**(`armclang: error: unknown argument: '--C99'`)。藏在 `<MiscControls>` 里,删掉即可(C99 由 AC6 默认给)。
5. **`<v6Lang>` 陷阱**:GUI 切 AC6 时 Keil 会写入一个语言标准字段,实测强制成 C90 → `cmsis_armclang.h` 的 `inline` 报 `unknown type name`。**删 `<v6Lang>`/`<v6LangP>`/`<v6Lto>`**,走 AC6 默认 gnu11。
6. **WSL→Windows interop 偶发挂**(`exec format error`,UV4/cmd.exe/net.exe 全挂)。挂了就在 Windows 原生跑 `.bat`。`/mnt/d/mf` 从 WSL 仍可读写,故编完拷回不阻塞。

## 验证

- 3 份 `.axf` 全 `ELF32 / ARM / entry=0x80000ed`,`Stm32f103Soc::load_elf` 直接加载。
- `FirmwareArmcc.{GpioIoToggle,TimTimeBase,UartPrintf}BootsClean` 三测全 **0 fault**(2,000,000 步)。
- 全量 ctest **247/247** 绿。
- **当前模拟器对 armcc codegen 零 fault**——和 F103.axf 一致,未提前暴露新指令缺口(那些等 T1/T2 主动挖)。

## 后续

- 想扩语料:照 `REGENERATE.md` 加示例 + 在 `test_firmware_armcc.cpp` 加 `BootsClean` 测试。
- armcc 多样性的真正价值在 **T1/T2 修完指令后**——那时这些固件成了活体验收器。
