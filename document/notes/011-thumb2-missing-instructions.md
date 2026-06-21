# 011 — Thumb-2 §3 缺失指令补全(T2)

> Thumb-2 全覆盖里程碑 · T2。补全 matrix §3「M3 范围内缺失指令」。继 T1(§2 静默错误 11/11 清零)之后,把模拟器从「能跑现有固件」推向「覆盖 ARMv7-M base 指令集」。全部 objdump 核验 + 单测,**ctest 271/271 绿**。

## 新增指令

| 指令 | 实现 |
|------|------|
| ORN.W / MVN.W imm | `dataproc_imm` 加 case 3:`Rn \| ~imm32`(Rn=15 退化为 MVN)。逻辑标志走 update_nz。 |
| ROR / RRX(shifted-reg operand) | `dataproc_reg` shift 加 case 3:`shift_n==0` → RRX(`(C<<31)\|(Rm>>1)`,读 PSR_C);否则 ROR by n。 |
| SMLAL / UMLAL | 扩长乘块(0xFBC0/0xFBE0):`RdHi:RdLo += signed/unsigned(Rn*Rm)`,read-before-write 累加。 |
| LDRSB.W / LDRSH.W | dispatch mask `0xFF00→0xFE00`(含 0xF9xx);handler `hw1[8]` 判 sign,load 后 byte/half sign-extend。sign store → IllegalInstruction。 |
| CLZ / RBIT / REV.W / REV16.W / REVSH.W | 新 `t32_misc_reverse`:CLZ=`std::countl_zero`,RBIT=位反转,REV/REV16/REVSH 复用 16 位族逻辑。CLZ vs REV.W 用 `hw1[7:4]`(0xB vs 0x9)区分。 |
| SSAT / USAT | 新 `t32_ssat_usat`:饱和到有/无符号范围,越界写 **APSR.Q**。sat 宽度 `hw2[4:0]`(SSAT +1),shift imm5=`(hw2[14:12]<<2)\|hw2[7:6]`。 |
| CLREX / NOP.W / YIELD.W / SEV.W | barrier handler 加 op=2(CLREX);新 `hw1==0xF3AF` handler(hints 全 no-op)。 |
| MCR / MRC | 无 handler → fall through 末尾 IllegalInstruction(架构上应 NoCoproc UsageFault;CPUError 无 NoCoproc,IllegalInstruction 为合理 clean fault)。M3 无协处理器,两份固件 0 命中。 |

## 关键设计点

- **PSR_Q(bit27)**:新增(def.h),APSR 的 MRS/MSR(sysm 0x00)读写含 Q,SSAT/USAT 饱和时置位。
- **dispatch 顺序 load-bearing**(延续 T1c #10 教训):
  - SSAT/USAT(`0xF3xx`)必须**早于** dataproc-imm —— 否则 SSAT `0xF301` 命中 `(hw1&0xF800)==0xF000` 被当 ADD-imm。mask `0xFFD0` 放开 hw1[5](shift type)。
  - CLZ/RBIT/REV(`0xFA00` with op2≠0)在 shift_reg 之后 —— shift_reg 只收 op2==0。
  - LDRSB/SH 的 `0xFE00` mask 含 0xF8xx(无符号)+ 0xF9xx(符号)。
  - MCR/MRC(`0xEExx`)不匹配任何前置 dispatch,末尾兜底。
- **字段位**(objdump 权威):`mov.w r3,r1,rrx = ea4f 0331` —— **Rd 在 hw2[11:8]、imm3 在 hw2[14:12] 是独立字段**(不是 hw2[15:12])。`usat r2,#5,r1 = f381 0205`(Rd=hw2[11:8])。这类掩码位错配是 T1c #10 的根因,本次全部 objdump 确认到位,一次通过。

## 验证

- `ctest` 全量 **271/271 绿**(263 + 8 新单测,`test_cortex_m3_advanced.cpp`)。
- 全部编码 `arm-none-eabi-as -mcpu=cortex-m3` + `objdump -d` 核验。
- 固件 E2E(3 AC6 + gcc hal_uart)全绿,不破坏真实固件启动。

## 未做(策略性)

- **BLX imm(T1)**:现 IllegalInstruction,M3 无 ARM 态,正确 fault(matrix §3:「现即可,补语义说明」)。
- **ARMv7E-M DSP**(QADD/QSUB/QDADD/QDSUB、PKHBT/PKHTB、SEL、SXTAH/UXTAH/SXTB16、UMAAL、SMLAD 族、USAD8 族):`arm-none-eabi-as -mcpu=cortex-m3` 拒绝 = M3 没有,保持 fault(matrix §4,作用域外)。需验证它们 clean-fault(不误解码进现有 handler)——这是 T3(§4 门禁)。
- **T3/§4**:作用域外指令的 clean-fault 验证;**T4/§5**:测试缺口 sweep(post-index、LDRD 全模式、flag sweep)。

## 成果

matrix §3 缺失指令基本补全(M3 范围内),模拟器指令覆盖从「够跑现有固件」提升到「ARMv7-M base 指令集覆盖」。剩余 §4(作用域门禁)+ §5(测试缺口)为收尾验证类工作。
