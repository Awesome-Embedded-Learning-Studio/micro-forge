# movups 家族 JIT lowering（2026-11-11，13e4926）

## 动机

静态统计（新内核）：movaps 1108 + movdqa 809 + movdqu 777 + movups 1607 ≈ 4296
条指令（~2.8% 流量），形态几乎全部是 xmm ↔ [base+disp]（rbp 负位移、rsp、rbx）。

## 设计

- 模板 `kMovxLoadSlot`/`kMovxStoreSlot`（各 45 字节）：ABI rdi=cpu、rsi=vaddr、
  edx=xmm 槽字节偏移；helper ABI 与 kLoadSlot 同构（movabs r11 + call r11）。
  load 版 slot 洞 @27（BA），cpu 洞 @18；store 版交换顺序（slot @17、cpu @23）。
- helper `Cpu::jit_load_xmm/jit_store_xmm`：两次 8 字节，完全复用
  jit_load/jit_store 的 translate_access 故障路径；任一半故障即中止
  （检查 exception_injected_），语义与解释器 sse.cpp 的 read_vec/write_vec
  逐半访问一致。
- translate 分支放在 translate_insn 中段 kReg 守卫之前（xmm 寄存器枚举
  ≥16 过不了 GPR 的 operand_slot），xmm 槽 = (reg-16)*16。

## 坑

1. 差分 harness 的 seed 顺序：`set_gpr(result_reg, initial)` 在
   `set_gpr(base_reg, base)` 之前，同一个寄存器两个身份时 base 赢。
   result_reg 挑旁观寄存器（kR9），否则 expected 检查挂。
2. cpu.hpp 恰好 600/600 行：两个声明挤进去靠合并成员注释（fs_base/gs_base
   同行、tss/ldt 同行、console 注释上提为行尾注释）。
3. translate.cpp 涨到 643 行超限 → 拆 `jit_translate_mov.cpp`（mov 族
   store/load/rip 全部）。**拆分丢了 load 分支的 memory_width 门**（原先由
   中段 kReg+memory_width 守卫兜底），宽度 0/3/16 一度漏进 JIT，
   IndexedMov 布局测试逮住。教训：从 translate_insn 里搬走的分支，要逐条
   核对它原本隐式依赖的外层守卫。
4. 差分 harness 新增 `DiffCase::vec/vec_reg`（set_xmm 播种）与 State 的
   xmm 终值比对——load 用例不比对 xmm 等于白测。

## 结果

老内核 78.33% → 78.63%，新内核 78.31% → 78.61%（+0.30pp，动态流量低于
静态占比，因差分/热块分布）。281 → 285 差分用例，473 项 GoogleTest 全绿，
九段验收通过，双内核 console 逐字节一致。
