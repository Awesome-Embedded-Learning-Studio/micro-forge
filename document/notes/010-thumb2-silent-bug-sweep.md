# 010 — Thumb-2 §2 高危静默错误清零(T1c)

> Thumb-2 全覆盖里程碑 · T1c。继 T1a(shift Carry)、T1b(reg-offset load/store,notes 009)之后,清零 matrix §2 剩余 9 处静默错误(#2–#8、#10、#11)。**§2 高危 11/11 全部修复**。

## 背景

matrix [`document/ai/thumb2-coverage-matrix.md`](../ai/thumb2-coverage-matrix.md) §2 列出 11 处 🔴「静默错误」——指令不 fault,但结果悄悄算错/写错。本批一口气修完剩余 9 处,每条配 objdump 核验 + 针对性单测。

## 修复(逐条 + 修复点)

| # | bug | 修复 |
|---|-----|------|
| 2 | ORN.W reg 丢 Rn | `t32_dataproc_reg` op=3:`~shifted` → `Rn \| ~shifted`(Rn=15 退化为 MVN)。 |
| 3 | RSB.W 标志颠倒 | RSB(op=14)被减数是 shift 操作数;reg 与 imm 两处 `update_flags` 改传 `(Sub, shift-op, Rn)`。 |
| 4 | LSR/ASR shift-by-32 | `dataproc_reg` imm3:imm2==0:LSR→0、ASR→符号扩展(原返回 rm_val 不变)。 |
| 5 | CPSID/IE f 拨错寄存器 | `0xFFF0` 掩码忽略 bit[0] → `0xFFE0` 掩码 + bit4(E/D)/bit1(i)/bit0(f) 分发,FAULTMASK 正确。 |
| 6 | BKPT 静默 NOP | `0xBExx` 落 hints 的 NOP 兜底 → `trigger_hardfault()`(vector 3)。 |
| 7 | MUL.W Ra=15 叠 raw PC | MLA/MLS 块:`rr(15)` 加进乘积 → Ra=15 视作「无累加」(acc=0)。 |
| 8 | ADR.W off-by-2 | `t32_addsub_plain_imm`:ADDW/SUBW Rn=PC 用 `rr(15)`(raw PC)→ `Align(PC+4,4)`。 |
| 10 | TBH 掩码漏 H 位 | TBB/TBH dispatch hw2 掩码 `0xF0F0` 检查了 bit4(H)→ 改 `0xFFE0`(放开 [4:0]=H+Rm)。 |
| 11 | LDREX/STREX 撞 STRD/LDRD | 新 `t32_ldrex_strex`,mask `0xFF60==0xE840`(exclusive 空间 P=0&W=0;STRD/LDRD 必有 P 或 W,故不撞);单核 sim 简化为普通 LD/ST(STREX 总成功 Rd=0)。 |

## 关键陷阱:#10 mask 首版写错,E2E 抓到

#10 第一版把 hw2 掩码写成了 `0xFF0F`(检查 hw2[3:0])。但 **TBB 的 Rm 字段就在 hw2[3:0]**。gcc hal_uart 固件的 `tbb [pc,r4]`(hw2=`0xF004`,Rm=4≠0)因此**不匹配** TBB/TBH dispatch → fall through 到新加的 #11 LDREX dispatch(`0xE8DF & 0xFF60 == 0xE840`)→ tbb 被当 LDREX(load,Rd=hw2[15:12]=15)→ `wr(15, 读到的字节)` → **PC 跳飞到 GPIOA(0x40010804)**。

`E2E.HalUartTransmit` 以 `InstructionFetchFault` 抓到(PC 跑到外设区)。反汇编 hal_uart 定位到 `tbb [pc,r4]`,才锁定是 #10 mask 而非 #11。正确 mask 是 `0xFFE0`(检查 [15:5],放开 [4:0]=H+Rm)。

**教训**:
- mask 改动必须 objdump 验证所有变体,尤其 Rm≠0 的 TBB(之前只验证了 `tbh [pc,r0]` 这种 Rm=0 的)。
- 新加的「前置 dispatch mask」(LDREX)要确认不吞掉共享 hw1 空间的其它指令——TBB/TBH/LDREXB/H 同居 `0xE8Dx`。**dispatch 顺序(TBB/TBH → LDREX → STRD/LDRD)是 load-bearing**。
- 间接测试(roundtrip / 0-fault 固件)掩盖静默错;**针对性单测(断言具体值/地址/标志)才是安全网**。这次正是 hal_uart E2E 救了场。

## 验证

- `ctest` 全量 **263/263 绿**(254 + 9 新单测,每条 bug 一个针对性断言:`test_cortex_m3_advanced.cpp`)。
- 全部编码 `arm-none-eabi-as -mcpu=cortex-m3` + `objdump -d` 核验。
- 固件 E2E(3 份 AC6 + gcc hal_uart)全绿,证明修复不破坏真实固件启动(且 hal_uart 的 tbb 反向验证了 #10)。

## 未竟(下一里程碑 T2)

- §3 缺失指令(M3 范围内):**LDRSB.W/LDRSH.W(0xF9xx 整族)**、ORN.W/MVN.W imm、ROR(shifted-reg)+RRX、SMLAL/UMLAL(长乘累加)、CLZ/RBIT/REV.W 族、SSAT/USAT(+Q 标志)、CLREX/NOP.W hint 族、MCR/MRC 策略。
- §4 作用域门禁(ARMv7E-M DSP 指令 clean-fault 验证)、§5 测试缺口 sweep。
- 行 255 SBFX/UBFX dispatch 的 `|| (hw1&0xFB70)==0xF3C0` 是 tautological 死代码(pre-existing,clangd 报但 gcc 不报,功能靠 `is_unsigned` 正确)——可顺手清,非阻塞。
