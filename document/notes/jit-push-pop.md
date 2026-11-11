# JIT push/pop reg64 落地收工笔记（2026-11-11，`ef32a93`）

## 结果

- 老内核 b58e271 覆盖 78.1779% → **78.3302%**；新内核 a234e96 → **78.3083%**
- 双内核 console 与解释器逐字节一致；469 项测试、九段验收门禁全绿
- 新增 7 个差分用例（reg/imm push、pop reg、push rsp / pop rsp 特例、rsp 回平衡）

## 模板

- kPushRegHelper（33 字节）：`mov rsi,[rdi+slot]` **模板自己读值**，helper 收值不收槽
- kPushImmHelper（35 字节）：`movabs rsi,imm64`，imm 在解码侧已符号扩展进 ops[0].disp
- kPopRegHelper（30 字节）：`mov esi,slot`，helper 写回槽
- helper 全部薄封装 `push_value/pop_value`——栈序、宽度、pop rsp 特例与解释器逐位同源

## 坑（本轮四个）

1. **kPushVal 收值不收槽**：首版 helper 签名 `jit_push_reg(slot)` 但模板给 rsi 的是寄存器
   **值**，`regs_[kStored/8]` 越界 → SIGSEGV。诡异在 mini 程序（O0）越界读不崩、差分
   child 崩，差点误判"harness 特有"。定位：helper 加一行 fprintf 打印入参，slot 竟是
   kStored=0x8877665544332211，立刻明白。
2. **imm push 被 kReg 守卫拦截**：translate_insn 中段有 `ops[0].kind != kReg → nullopt`
   的总闸，push imm 的 kImm 操作数根本到不了我的分支——**新分支要检查上游守卫**，
   必要时上移到守卫之前。
3. **DiffCase 种子顺序**：seed 先设 rsp=0xF000，再 `set_gpr(result_reg, initial)` /
   `set_gpr(source_reg, source)`——**result_reg/source_reg 若是 kRsp 会被 initial/source
   覆盖**，push 到不可写地址直接 fault。rsp 用例必须显式 initial=0xF000 / source=0xF000。
4. **嵌套 brace-init 的尾逗号**：`.patches = {{.offset=…}}` 双层展开后 tidy 要求**内层
   闭合也带逗号**；clang-format 又会重排。终解：抽中间变量 `const JitPage::Patch p{…,};`
   再 `.patches = {p}`，绕开嵌套字面量。
5. push reg 模板 slot 洞在 **@5**（`48 8B B4 27` 4 字节前缀之后），又按"前缀长度=3"错算
   过一次 @4——has_valid_layout 的零占位检查这次直接拦住了，校验器好使。

## 覆盖率走势

33% → 50%+ → 75.00% → 77.40% → 77.41% → 78.18% → **78.33%**

## 说明

push/pop 静态占 9628 条（6.2% 指令流）但本轮只 +0.15pp：jit_insns 是**动态执行**计数，
push/pop 多在非热块；且 push [mem]、16 位 push 等形态未做。剩余大头仍是 jcc-in-block。

## 下一步候选

- movups ~2.8%：SSE 128 位搬移（xmm 两半 qword 走现有 load/store）
- call ~3.9% / ret ~3.8%：ret 可先做（pop rip + jmp），call 依赖块链接
- jcc-in-block ~61%：块退出设计与块间直接跳转，hard 档
