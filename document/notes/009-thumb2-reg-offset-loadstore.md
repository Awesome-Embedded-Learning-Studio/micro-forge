# 009 — Thumb-2 寄存器偏移 load/store 修复(matrix §2 #9)

> Thumb-2 全覆盖里程碑 · T1b。修 matrix §2 高危静默 bug #9(F32-8):`ldr/str.w [Rn,Rm]` 静默算错地址。

## 背景

Thumb-2 全编码覆盖矩阵([`document/ai/thumb2-coverage-matrix.md`](../ai/thumb2-coverage-matrix.md))§2 列出 11 处 🔴 高危「静默错误」——指令不算 fault,但结果悄悄算错,固件 0-fault 跑通反而掩盖了它们。T1a 已修 #1(shift Carry 不更新)。本批 T1b 修 **#9**:寄存器偏移 load/store 静默算错地址。

## 问题

`ldr.w r0,[r1,r2]`、`str.w r0,[r1,r2]`、`strb.w`、`ldrh.w` 等所有 **register-offset** 形式(编码 hw1=0xF8x1、hw2=0002 之类)被 `t32_loadstore_single` 误当成 imm8 offset+,hw2[7:0] 当成 imm8,算出 `r1+2` 而非 `r1+r2`。**不 fault**,模拟器不自知结果错。

```
ldr.w r0,[r1,r2] → 期望 addr = 0x100 + 0x40 = 0x140
                 → 实际 addr = 0x100 + 0x02 = 0x102   ← 静默错
```

## 根因(objdump 裁定,非记忆)

dispatch `(hw1 & 0xFF00) == 0xF800` 覆盖 0xF8xx 全空间。函数内 `(hw1>>7)&1` 区分 imm12(bit7=1)与其余(bit7=0)。问题在 bit7=0 的 else 分支:

- 旧代码按 `op=hw2[11:8]` switch,`case 0x0` 当「`[Rn, #+imm8]`」。
- 但 `arm-none-eabi-as` 实测:正向小立即数(`ldr.w [r1,#4]`)永远折叠进 imm12(T3,F8D1),**不会**用 imm8 offset+ 编码。所以在 bit7=0 空间,`op==0` 的**唯一**含义就是 register-offset。
- 旧 `case 0x0` 既是死分支(无有效 imm8 offset+ 编码),又把 reg-offset 的 Rm/shift 字段当 imm8 解析 —— 双重错。

区分位(objdump 全形式实测裁定):

| hw1[7] | hw2[11:8]=op | 形式 |
|--------|------|------|
| 1 | — | imm12(T3):`addr = Rn + imm12` |
| 0 | 0x0 | **register-offset**(T2):`addr = Rn + (Rm << shift2)` |
| 0 | C / B / 9 / F / D | imm8 addressing modes(T4):off- / post± / pre± |
| 0 | 其他 | IllegalInstruction |

reg-offset 字段:`Rt=hw2[15:12]`、`shift=hw2[5:4]`(LSL 0–3)、`Rm=hw2[3:0]`,无 writeback。

## 修复

[`src/arch/arm/cortex_m3/cortex_m3_thumb32_loadstore.cpp`](../../src/arch/arm/cortex_m3/cortex_m3_thumb32_loadstore.cpp) `t32_loadstore_single`:

1. **base 对齐下沉**:Rn=15 的 `Align(PC+4,4)` 从「literal 专享特判」下沉为通用 base —— reg-offset 以 PC 为基(`ldr.w r0,[pc,r1]`)也正确受益。
2. **else 分支 carve out**:`op==0` 单独走 register-offset(`addr = base + (rr(rm) << shift)`);imm8 switch 删除无效的 `case 0x0`。
3. store-to-PC-relative(imm12 Rn=15 & !load)拒绝,语义不变。

## 测试

两个新单测([`test/test_cortex_m3_advanced.cpp`](../../test/test_cortex_m3_advanced.cpp)):

- `LoadStoreWideRegisterOffset`:`str.w [r1,r2]` 断言写到 **0x140**(+ regression guard 读 0x102 应无该值);`ldr.w [r1,r2,lsl#3]` 断言读 **0x300**(shift 是关键,bug 下会读 0x132)。
- `LoadStoreWideRegisterOffsetByteHalf`:`strb.w [r1,r2]`(byte→0x110)、`ldrh.w [r1,r3]`(half←0x140)。

**关键设计**:断言**具体地址**(`bus_.read(addr, Width)`),不用 roundtrip —— roundtrip 的 str/ldr 用同一(错误)地址会互相抵消、掩盖 bug。这正是 #9 长期没被现有测试抓到的原因(`LoadStoreWideWordAndHalfwordImmediateOffsets` 就是 roundtrip)。

## 验证

- 全部编码经 `arm-none-eabi-as -mcpu=cortex-m3` + `objdump -d` 核验。
- `ctest --test-dir build` 全量 **254 绿**(原 252 + 新增 2),无回归。
- `cmake --build build -j$(nproc)` 全量编译绿。

## 陷阱 / 未竟

- **LDRSB.W / LDRSH.W(0xF9xx)仍未 dispatch**(matrix §3 缺失):`(hw1&0xFF00)==0xF800` 不匹配 0xF9xx,fallthrough → IllegalInstruction。这是整个 0xF9xx 空间缺失,范围更大,**另起一批**,本批不动。
- matrix 引用的代码行号是 T0 拆分前的(`cortex_m3_thumb32.cpp:366-474`),拆分后实际落在 `cortex_m3_thumb32_loadstore.cpp`;不影响 #9 的判断与修复,行号全量更新属另一清理任务。
- §2 剩余高危:#2 ORN.W 丢 Rn、#3 RSB 标志错、#4 LSR#0/ASR#0、#10 TBH→LDRD、#11 LDREX/STREX→STRD/LDRD —— 下一批候选。
