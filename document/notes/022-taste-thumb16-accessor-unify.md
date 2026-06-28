# 022 — taste 战役开张:thumb16 访问器 lambda 统一(DRY)

> 2026-06-28。taste 维度战役第一篇。承接 perf 收尾(notes 019-021),转 CODING-TASTE 标尺治理。第一批吃掉爆破半径最小、最干净的反例:`rr/wr/br/bw` 访问器双份(原则2 DRY)。

## 反例:thumb16 lambda vs thumb32 member(逐字相同)

CODING-TASTE 原则2 标的反例:`rr/wr/br/bw` 访问器在 thumb16(`execute_16bit` 局部 lambda)与 thumb32(成员函数)各一份、逐字相同。

**真相比标尺写的更整洁**:thumb32 **早已统一**——`execute_32bit` 的局部 lambda 在拆 dataproc/loadstore 时已提升为成员函数(`cortex_m3_thumb32.cpp:14-47`),注释明写 "Promoted from execute_32bit-local lambdas... Bodies unchanged"。thumb16 只是把同一套 lambda 又抄了一份(`cortex_m3_thumb16.cpp:15-50`),没跟上那次提升。

逐行比对确认逐字相同:`rr` = `regs_.unchecked(idx)`;`wr` = `write_reg` + 错误传播;`br`/`bw` = `bus_` 判空 + `bus_->read/write` + `record_bus_fault`。四者一字不差。

## 改动:删 lambda,调用点零改

删 thumb16 的 4 个 lambda(原 15-50),加 4 行去向注释。**80+ 个调用点零改动**——`rr(rn)` / `wr(rd,...)` / `br(addr,w)` / `bw(addr,v,w)` 对 lambda 和成员函数是同一调用语法,删 lambda 后解析到成员函数,行为不变。

### 删除前 shadowing audit(关键)
thumb16 里有两处 `auto wr = write_reg(13, sp);`(原 556/641)—— 这是**局部变量**接收 `write_reg` 返回值,后面 `if (!wr) return wr;` 检查它,**从不调用 `wr()`**。删 lambda `wr` 后这两处照常定义局部变量 `wr`,语义不变。其余 80+ 处全是函数调用。audit 通过才敢删。

## 验证

- **ctest 321/321 双构建全绿**(Debug `build/` + RelWithDebInfo `build-rel/`)—— 行为保持。
- **bench `--baseline` advisory PASS**(exit 0,0 regression)。guard ratio 87/93/92% < 100% 是 WSL2 跨次运行噪声(基线另一时刻锁的),非改动回归——删逐字相同的 lambda 物理上只持平/微增,不可能退化。

行数:`cortex_m3_thumb16.cpp` **744 → 711**(净 -33)。仍略超 ≤700 软上限 → 治本需下一批 thumb16 按族拆分。

## 价值

1. 消除 DRY 双份(原则2)—— 四套访问器逻辑现在单一归属(成员函数)。
2. 为 thumb16 按族拆分**铺路**:拆出族 handler 后直接用成员函数,不必担心 lambda 作用域/捕获。
3. 微小热路径增益(省每次 `execute_16bit` 的 4 次 lambda 构造),但落噪声地板、不可单独采信。

## 后续 taste 批次候选(均 propose-then-execute,跨子系统大改)

- **thumb16 按族拆分**:711 行单函数 → 多文件(对齐 thumb32 的 `thumb32_{dataproc,loadstore}.cpp` 拆分模式),治 ≤700 超限 + 原则1 职责堆叠。**前置:QEMU oracle 伴跑**(`scripts/qemu_cortex_m3_oracle.sh` 已就绪,`qemu-system-arm` + `arm-none-eabi-gdb` 均装)。
- **step() 拆 sub-function**:4 职责(中断门控→fetch+fault 升级→IT 门控→dispatch+PC 增量→fault 后处理)拆成命名子函数,治原则1。
- **execute_32bit 表驱动**:~20 级掩码梯(SSAT 须在 dataproc-imm 前、TBB/LDREX 须在 STRD 前)→ `(mask,value,handler)` 有序表,治原则4(顺序即语义)。
- **t32_dataproc_imm vs reg 近克隆合并**:~110 行只差操作数来源,治原则2。
