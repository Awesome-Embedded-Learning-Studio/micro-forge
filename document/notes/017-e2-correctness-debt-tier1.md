# 017 — E2 Tier 1:残余正确性债清理(ADC/SBC 全形式 + SDIV/0)

> E2「指令 conformance + 清债」里程碑 · Tier 1。第二波外设中断端到端(06 C1/C2/C3)收口后,焦点回到项目 #1 支柱「CPU/诊断可信度」。本批清掉覆盖矩阵名义「已完成」、实测仍有 bug 的三处残余正确性债。ctest **312/312 绿**(301 + 11 新)。

## 背景:文档漂移下的「真债」定位

读-only 审计(4 agent 并行 + 人工逐条核实)发现:覆盖矩阵 §1/§6 头表过时(描述 T0 前状态、行号全错),把已修项仍标 partial/missing;而 06 §真实性债#5 点名的 SVC/CLZ 早在 T2/T3 落地。**真正的残余债只有三处**,且两处在「.W 已实现」的假象下藏着:

## 三处真债

### T1a · 32-bit `adc.w`/`sbc.w` shifted-register 形式整条缺失(F32-6)
`t32_dataproc_reg` 的 `switch(op)`(`cortex_m3_thumb32_dataproc.cpp:179`)只有 `0/1/2/3/4/8/13/14`,**无 case 10(ADC)/11(SBC)** → `adc.w/sbc.w reg,reg,shift` 落 `default → IllegalInstruction`。矩阵/旧审计误判「.W 已实现」(那只指 imm 形式 :86-112)。补 case 10/11。
- objdump 确认编码:`adcs.w r3,r1,r2,lsl#4 = 0xEB51 0x1302`,`sbcs.w = 0xEB71`(S 位 = hw1 bit4;**adc.w(S=0) 不写 flag**,flag 测试必须用 adcs.w)。

### T1b · 16-bit `adcs`/`sbcs`(op 5/6)漏 C/V(F16-3)
`cortex_m3_thumb16.cpp` op5/6 算对了 result(含进位),但只走共享 epilogue 的 `update_nz(result)`(`:343`),**从不写 C/V**。op5/6 改为早返回 + 显式置全 flag。

### T1c · INT_MIN/-1 有符号溢出(UB)guard;SDIV/0 语义由 oracle 钉死
两件事:
1. **`INT_MIN / -1` 是 C 有符号溢出(UB)** —— 旧 `a/b` 在该输入下是 UB(可能崩溃/乱值)。ARMv7-M 饱和到 INT_MIN(`0x80000000`)。加 guard(与 QEMU 一致)。
2. **SDIV/0 结果 = 0(双符号)**。初版误记成「负被除数→INT_MIN」并实现成 INT_MIN —— **被 Tier-2 QEMU oracle(`scripts/qemu_cortex_m3_oracle.sh`,mps2-an385)当场否决**:Cortex-M3 SDIV/0 对正负被除数都返回 0。已改回 0;UDIV/0 = 0。这正是 oracle 存在的意义(「不靠记忆」,以工具链证据为准)。

## 设计点

- **进位感知 flag helper(替代「合并操作数」trick)**:imm 形式旧法把 `op2+Cin` 当 `b` 传给 `update_flags(Add/Sub)`,在 `op2=0xFFFFFFFF` 且有 Cin 时 `b` 环绕到 0,C 算错。新增 `set_adc_flags`/`set_sbc_flags`(64 位和算 C,标准公式算 V),三处(32-imm/32-reg/16-reg)统一改用,SBC 用 `a + ~b + cin` 的进位出 = NOT-borrow。彻底消除环绕边角。
  - SBC 的 V:由 `a + ~b + cin` 加法溢出推导,化简为 `((a^b)&(a^result))&sign`,与 SUB 一致(已数学验证)。
- **`DIV_0_TRP` trap 暂不建模**:configurable-fault 特性(06 列 ~60% 债,涉及 UsageFault 路径 + CCR 可写)。复位默认 `DIV_0_TRP==0` 是固件实际所见,故 predictable-result(=0)即「正确模拟」当前态;trap 作单独 configurable-fault 批跟进。
- **QEMU oracle 已跑通并锁定全部 Tier-1 语义**(`scripts/qemu_cortex_m3_oracle.sh`):SDIV/0=0、INT_MIN/-1=INT_MIN、ADC/SBC 全形式 result+flag 全与 `qemu-system-arm -M mps2-an385` 一致(DIV/ALU/xPSR 三行逐字对齐)。差分不再是「待办」而是「已过」。
- **flag 测试必须 S=1**:首版测试误用 `adc.w`(S=0)断言 flag → 全红;改 `adcs.w`/`sbcs.w`。教训:objdump 看 `adc.w` vs `adcs.w` 区分 S 位,断言 flag 前先确认。

## 验证

- 11 新单测(ADC.W shifted-reg carry/overflow、SBC.W borrow、16-bit ADCS/SBCS、SDIV/0 neg/nonneg/INT_MIN-over-(-1)/normal、UDIV/0),全 `arm-none-eabi-as` + objdump 定编码,断言**具体 result 值 + N/Z/C/V 位**(经 `MRS R0,APSR`),非 roundtrip。
- `ctest` 全量 **312/312 绿**,固件 E2E(3 AC6 + gcc hal_uart)/ CLI / 中断抢占无回归。

## 成果与后续

Tier 1 把覆盖里程碑「名义完成」下的最后三处正确性 bug 清零;ADC/SBC 全形式(16-reg / 32-imm / 32-shifted-reg)flag 统一正确,**经 QEMU 差分逐字确认**。Tier 2(同分支):差分 oracle 已落地并通过(见上);`-O0/-O2/-Oz` 固件回归门 + armcc 语料构建 pwsh 脚本(`test/firmware/armcc/build_corpus_opt.ps1`,NTFS 端用户执行)见 notes 018。
