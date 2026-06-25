# 018 — E2 Tier 2:QEMU 差分 oracle + 优化级固件回归门

> E2 conformance 里程碑 · Tier 2(与 Tier 1 同分支 `feat/e2-conformance`)。06/02 把「指令 conformance」定为可信度支柱的关键,本批把它从「-O0/-O2/-Os + 差分 oracle」的**设计**落成**可执行物**。两件:(1) QEMU Cortex-M3 差分 oracle 跑通并锁定 Tier-1 语义(notes 017);(2) armcc 多优化级语料构建 pwsh 脚本交付(NTFS 端,用户执行)。

## 1. QEMU 差分 oracle(已落地 + 通过)

`scripts/qemu_cortex_m3_oracle.sh`:汇编一个微型 Thumb 程序(SDIV/UDIV 四边角 + ADC/SBC shifted-reg 四例带 flag),在 `qemu-system-arm -M mps2-an385`(Cortex-M3)下 `-S -gdb tcp::PORT` 挂起,`arm-none-eabi-gdb` 连上 `break done; continue`,dump 结果区(0x2000/0x2010)与 xPSR 快照区(0x2020),与期望逐行比对。

- **本机工具链就绪**:`qemu-system-arm` 11.0.1 + `arm-none-eabi-gdb` 均在 `/usr/sbin`(装机可行性 = 已确认,不再「待评估」)。
- **结果(notes 017 已引)**:DIV/ALU/xPSR 三行与 QEMU 逐字一致。**oracle 当场否决了 Tier-1 初版「SDIV/0 负→INT_MIN」的误记**,改成正确的 SDIV/0=0(双符号)。这是「不靠记忆、以工具链为准」的直接收益,也是 oracle 存在的最佳广告。
- **坑(全趟过,记给后续)**:
  - QEMU `-monitor stdio` 对 piped 输入做 readline 行编辑,`pmemsave` 的文件名参数被解析成表达式报 `invalid char 't'` —— **改走 gdbstub + gdb 读内存**,比 monitor pmemsave 稳得多。
  - `arm-none-eabi-ld` 链接脚本**必须有空格**(`SECTIONS { . = 0x0; .vectors : {...} }`,紧凑无空格版 syntax error)。
  - 分支目标 label **必须前缀 `.thumb_func`**(否则 `Unknown destination type (ARM/Thumb)` 重定位错)。`.thumb_func` 只标记**下一个**符号,每个被 branch/.word 引用的 thumb 入口前都要加。

## 2. 优化级固件回归门(设计 + pwsh 脚本交付)

### 动机
不同 `-O` 级发射不同 Thumb-2 指令组合(-O2 可能发 `adc.w`/`sbc.w`/`sdiv` 而 -O0 不发)。只跑单优化级语料(notes 008 的 3 份 AC6 .axf),无法证明「指令对的」在 codegen 变体下成立。多优化级语料是最便宜的信任放大器(02 §验收 line 76)。

### 脚本:`test/firmware/armcc/build_corpus_opt.ps1`
PowerShell,把现有 3 个 CubeF1 示例(GPIO/TIM/UART)在 AC6 的 **-O0/-O2/-Oz** 三级各编一份,产出 `nucleo_f103rb_<ex>.ac6-<opt>.axf`。
- 复用 REGENERATE.md 的 AC5→AC6 三补丁(`uAC6` / 删 `--C99` / 删 `v6Lang` 族),**新增第 4 补丁**:清 MiscControls 里旧 `-O` token,追加 `-O<level>`(armclang 直接认)。**注意:AC6 无 `-Os`;最小体积是 `-Oz`**(AC5/旧 armcc 的 `-Os` 在 AC6 映射到 `-Oz`)。脚本内 `Os→Oz`,注释说明。
- **必须在 Windows NTFS 跑**(WSL 9p 建不了 Keil `.__i`);产物落 `D:\mf\out\`,用户从 WSL `cp /mnt/d/mf/out/*.ac6-*.axf test/firmware/armcc/` 回库。
- opt 级若被 Keil 结构化 Optimization 字段覆盖(非 MiscControls),在 GUI 同步设或在脚本里调 —— 这块以用户(Keil 侧专家)实测为准,脚本为权威起点。
- `-DryRun` 只 patch+打印不编,便于先核对 .uvprojx 改动。
- **结构事实(实测 vendored `third_party/STM32CubeF1`,纠正 REGENERATE 口径)**:工程文件是 **`Project.uvprojx`**(不是 `STM32F103RB_Nucleo.uvprojx`);`<TargetName>/<OutputName>=STM32F103RB_Nucleo`,`<OutputDirectory>STM32F103RB_Nucleo\` → 产物 `MDK-ARM\STM32F103RB_Nucleo\STM32F103RB_Nucleo.axf`;include 路径 `../../../../../../Drivers` 是 **6 级**(不是 5),故 Drivers/ 与 Projects/ 必须在 CubeF1 根顶层。脚本据此修了初版两 bug(工程名错 + 跨优化级产物撞),并**按优化级复制整个 MDK-ARM 目录**(`MDK-ARM-<opt>`)做产物/对象隔离;opt 标志经 XML **只注入 Cads(C 编译器)** 的 MiscControls(不碰汇编)。
- **重建 D:\mf**:`scripts/prepare_corpus.sh`(WSL,写 `/mnt/d/mf`)从 vendored 树拷最小子树(Drivers/BSP + HAL_Driver + CMSIS/{Device,Core,Include},**砍掉 DSP/Lib/docs/NN 等 ~111M 无关大块**;3 个 Examples),保持 6 级深度。`/mnt/d` 从 WSL 可读写(只有 Keil *build* 必须 Windows 原生跑)。实测重建后 22M,深度校验通过。

### 门禁集成(Tier 2 收尾,待 .axf 就位)
`test/test_firmware_armcc.cpp` 每个 opt 变体加一个 `TEST(FirmwareArmcc, <Ex>Opt<O>BootsClean)`,加载对应 `.ac6-<opt>.axf` 跑 `reset→main→while(1)` 断言 0-fault(复用现有 `Stm32f103Soc::load_elf`)。.axf 由用户用脚本产出后提交(CI 无 Keil,二进制入库,同现有 3 份)。这部分代码待 .axf 落地再写,避免空 fixture。

## 3. 状态

- ✅ QEMU oracle:落地、跑通、Tier-1 全语义锁定(含纠错 SDIV/0)。
- ✅ pwsh 构建脚本:交付,待用户 NTFS 端执行产 .axf。
- ⬜ opt 级门禁测试:待 .axf 就位后补 `test_firmware_armcc` 用例(本批不阻塞)。
- 余:`DIV_0_TRP` trap(configurable-fault 批)、全量 matrix §6 re-baseline(文档债),均非本批。
