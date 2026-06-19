# 007 — 跑通真实 Keil/MDK-ARM STM32F103 固件

> 2026-06-19。把 `examples/F103`(STM32CubeMX + Keil MDK-ARM 生成的 HAL GPIO 工程)的编译产物在 micro-forge 上完整跑通,暴露并修复 Cortex-M3 模拟器的 7 处缺口。

## 背景

`examples/F103/` 是一个真实的 Keil 工程(STM32CubeMX 生成 `.uvprojx`),固件逻辑极简:

```c
HAL_Init();              // 配 NVIC 优先级分组 + 启动 SysTick
SystemClock_Config();    // 配 RCC: HSE → PLL×9 → SYSCLK, Flash latency
MX_GPIO_Init();          // HAL_GPIO_Init 配 PA1 输出
HAL_GPIO_WritePin(GPIOA, GPIO_PIN_1, 1);
while (1) {}             // 死循环
```

工程已编译出 `MDK-ARM/F103/F103.axf`(Keil 可执行)与 `F103.hex`(Intel HEX)。

## 目标

验证 micro-forge 能否加载并跑通这个真实固件。用户最初建议"支持跑 hex"。

## 关键判断:不需要 HEX loader

`F103.axf` 是**标准 ELF32**(magic `\x7fELF`、machine=ARM、entry=`0x080000ED`)。现有 `loader::load_elf` 零改动直接加载——代码段、SP、复位向量全对,CPU 确实从 `0x08000ED` 跑起来了。所以"能不能加载"的答案是 **YES**,HEX loader 只是格式便利(纯 parser,不影响"能不能跑"),优先级降为可选后续。

真正拦路的是 **Cortex-M3 的执行能力**——真实 C/HAL 代码暴露了一连串指令覆盖与核心特性缺口。

## 修复的 7 处缺口(按发现顺序)

每处都配 GoogleTest 单测,E2E 迭代到 `state=Running stop=MaxSteps`(main 的 `while(1)`),**零 fault**。

1. **`LDR.W Rt, [PC, #imm12]` (literal pool, T2)** — `cortex_m3_thumb32.cpp` 的 Load/Store immediate 块没对 `Rn==PC` 特判。这是 `LDR Rd, =const` 的标准编译产物,HAL 第一句就撞。
2. **`BLX Rm` 没设 LR** — `case 0b01000` 用 `op=(insn>>8)&3` 区分 BX/BLX,但二者 bits[9:8] 都是 `0b11`,漏看 bit[7]。结果 BLX 当 BX,直接跳转不写返回地址。Keil `Reset_Handler` 的 `blx r0` 调 `SystemInit` 必经此路。
3. **ADR 缺失** — `case 0b10101` 只覆盖 `ADD Rd, SP`(key=21),ADR 是 `ADD Rd, PC`(key=20),整个缺失。`__scatterload_rt2` 用 ADR 取 scatter table。
4. **`SUB.W`/`SUBW` plain imm12** — modified-immediate 块把 `insn[25]=1`(plain imm12,如 `subw lr, pc, #9`)误吞进去,`op2=5` 不在 switch。
5. **Load/Store immediate 忽略 `hw1[7]`** — 这是最系统的一处。`hw1[7]` 区分两种形式:`1`=imm12 偏移(`hw2[11:0]`),`0`=imm8 + 寻址模式(`op=hw2[11:8]`: `0/C` offset ±、`B/9` post ±、`F/D` pre ±)。旧代码完全忽略 `hw1[7]`,误把 imm12 当 8-bit 模式。整个 `str/ldr/strb/ldrb/strh/ldrh .W` 家族受影响。整体重写为统一的模式表。
6. **位带别名区(bit-band)** — Cortex-M3 核心特性。`0x42xxxxxx`(外设)/`0x22xxxxxx`(SRAM) 的 word 访问映射到对应 bit。`HAL_RCC_OscConfig` 用位带写 RCC CR 的 PLLON 位(`str.w r8, [0x42420060]` = 写 `0x40021000` bit24)。Bus 无翻译 → unmapped fault。在 `Bus::read/write` 入口加位带翻译,word RMW 目标 bit。
7. **data-proc shifted register 的 CMP/CMN/TST/TEQ 误写 PC** — `S=1, Rd=15` 应只更新 flags。modified-imm 块有 `if (s_bit && rd==15) return {}` 保护,shifted-register 块漏了,`cmp.w r0, r1, lsl #2` 把 `r0-(r1<<2)`(下溢成 `0xFFFFFFFx`)写进 PC。

## 设计决策与陷阱

- **ARM 编码不靠记忆**:plain-imm12 和 load/store 寻址模式的位域极易记错。用 `arm-none-eabi-as` 汇编已知语义的指令、`objdump` 读字节级真相,作为权威依据(`addw/subw #0x555`、各寻址模式的 `op` 字段都是这么定的)。CPU 解码正确性敏感,猜不得。
- **PC 口径**:`read_pc_raw()` = 当前指令地址(非流水线值),故 16 位 PC-relative 用 `+2`、32 位与 literal 用 `(PC+4) & ~3`。BLX 的 LR = `PC+2 | 1`(16 位 next instruction),不是流水线 `+4`。
- **位带用 Word RMW**:位带写本质是目标 word 的原子 read-modify-write。必须用 Word width 调底层,否则外设 `write` 的 `w != Word → Unaligned` 拒绝(位带常用于外设寄存器 bit)。
- **诊断纪律**:迭代期加 env-gated(`MF_TRACE_PC`/`MF_TRACE_EXC`)的临时 trace 到 `step`/异常 entry·return,定位后**全部撤除**,生产代码不留调试桩。故障定位靠 `arm-none-eabi-objdump -d` 反汇编 + PC 序列对照,不靠打印堆砌。
- **加载器复用**:`.axf` 即 ELF,`Stm32f103Soc::load_elf` 已含 `cortex_m3_reset`(从向量表读 SP/Reset)与 `launch()`。新增 `Machine::load_hex` 是纯增量(若日后要 hex),不动 ELF 路径。

## 验证

- `ctest --test-dir build --output-on-failure`:**241/241** passed(本次净增 ~15 项指令/位带单测)。
- E2E:`micro-forge run examples/F103/MDK-ARM/F103/F103.axf --max-steps 2000000` → `state=Running stop=MaxSteps`,**零 fault**。固件完整执行 reset→`__main`(scatter-load/zero-init)→`main`→`HAL_Init`→`SystemClock_Config`(RCC HSE/PLL,含位带写 PLLON/轮询 PLLRDY)→`MX_GPIO_Init`→`HAL_GPIO_WritePin(PA1)`→`while(1)`。
- 全量 `cmake --build build -j$(nproc)` `-Werror` 通过;`rg` 确认无遗留 `getenv`/trace 桩。

## 后续

- HEX loader(`loader::hex_loader` + `Machine::load_hex` + CLI 识别):纯格式 parser,~100 行,不影响"能否跑",按需再做。
- 本次暴露的缺口本质是"自写 startup 的示例没覆盖到的真实编译器产物"。可考虑把 F103.axf 纳入 E2E 测试套件,作为真实固件回归门禁。
